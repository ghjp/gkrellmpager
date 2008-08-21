/*  
 *  Copyright (C) 2005 Dr. Johann Pfefferl
 *
 *  Author: Dr. Johann Pfefferl pfefferl@gmx.net
 *
 *  This program is free software which I release under the GNU General Public
 *  License. You may redistribute and/or modify this program under the terms
 *  of that license as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 *  Requires GKrellM 2 or better
 *
 *  gcc -fPIC pkg-config gtk+-2.0 --cflags pkg-config gtk+-2.0 --libs -c gkrellmlaunch.c
 *  gcc -shared -Wl -o gkrellmlaunch.so gkrellmlaunch.o
 *  gkrellm -p gkrellmpager.so
 */

#include <X11/Xlib.h>
#include <X11/Xmd.h> /* CARD32 */
#include <X11/Xatom.h> /* XA_CARDINAL, ... */
#include <gdk/gdkx.h> /* GDK_DISPLAY, ... */
#include <gkrellm2/gkrellm.h>

/*
    * Make sure we have a compatible version of GKrellM
     * (Version 2+ is required due to the use of GTK2)
      */
#if !defined(GKRELLM_VERSION_MAJOR) || GKRELLM_VERSION_MAJOR < 2
#error This plugin requires GKrellM version >= 2
#endif

#define PLUGIN_PLACEMENT  MON_PLUGIN
#define CONFIG_TAB_NAME "Pager"
#define STYLE_NAME "pager"

static gchar *pager_help[] =
{
  "<b>Important!\n\n",
  "You need an EWMH conform window manager to get full functionality!\n\n",
  "<b>Usage\n\n",
  "- Use the desktop buttons to switch to the specific desktop number.\n",
  "- Use the scroll wheel to switch to the next or previous desktop.\n\n",
  "<b>Status indicator\n\n",
  " The LED which is switched on shows you the active desktop.\n",
  " The LEDs of the inactive desktops are off.\n",
};

static gchar pager_about[] = 
  CONFIG_TAB_NAME " Version " VERSION " (GKrellm2)\n"\
  "GKrellM plugin to give one-click access and status of the virtual desktops.\n\n"\
  "Copyright (c) 2005 by Dr. Johann Pfefferl\n"\
  "Release Date: " REL_DATE "\n"\
  "pfefferl at gmx.net\n"\
  "Released under the GNU Public License.\n";

static GkrellmMonitor  *monitor;
static GkrellmPanel *panel;
static GkrellmDecal *title_text;
static GPtrArray *ws_decal_arr, *status_decal_arr;

static Display *display;
static gint rootwin;
static gint curr_desktop, number_of_desktops;
static Atom xa_net_current_desktop, xa_net_number_of_desktops;

static gint style_id;

static gpointer get_ewmh_net_property_data(Atom prop, Atom type, gint *items)
{
  Atom type_ret = None;
  gint format_ret = 0;
  gulong items_ret = 0;
  gulong after_ret = 0;
  guint8 *prop_data = NULL;

  XGetWindowProperty(display, rootwin,
      prop, 0, 0x7fffffff, False,
      type, &type_ret, &format_ret, &items_ret,
      &after_ret, &prop_data);
  if(items)
    *items = items_ret;

  return prop_data;
}

static gint get_ewmh_net_current_desktop(void)
{
  gint num;
  CARD32 *ncd = get_ewmh_net_property_data(xa_net_current_desktop, XA_CARDINAL, &num);

  if(ncd) {
    num = *ncd;
    XFree(ncd);
  }
  else
    num = 0;
  return num;
}

static gint get_ewmh_net_number_of_desktops(void)
{
  gint num;
  CARD32 *ncd = get_ewmh_net_property_data(xa_net_number_of_desktops, XA_CARDINAL, &num);

  if(ncd) {
    num = *ncd;
    XFree(ncd);
  }
  else
    num = 0;
  return num;
}

static void wa_send_xclimsg(Atom type, glong l0, glong l1, glong l2, glong l3, glong l4)
{
  XClientMessageEvent e;

  e.type = ClientMessage;
  e.window = rootwin;
  e.message_type = type;
  e.format = 32;
  e.data.l[0] = l0;
  e.data.l[1] = l1;
  e.data.l[2] = l2;
  e.data.l[3] = l3;
  e.data.l[4] = l4;

  XSendEvent(display, rootwin,
      False, SubstructureNotifyMask|SubstructureRedirectMask, (XEvent *)&e);
}

static gint panel_expose_event(GtkWidget *widget, GdkEventExpose *ev)
{
  /*g_message(__func__);*/
  gdk_draw_pixmap(widget->window,
      widget->style->fg_gc[GTK_WIDGET_STATE (widget)],
      panel->pixmap, ev->area.x, ev->area.y, ev->area.x, ev->area.y,
      ev->area.width, ev->area.height);
  return FALSE;
}

static void cb_button(GkrellmDecalbutton *button, gpointer data)
{
  curr_desktop = GPOINTER_TO_INT(data);
  /*g_message("%s %d", __func__, nr);*/
  wa_send_xclimsg(xa_net_current_desktop, curr_desktop, 0, 0, 0, 0);
}

gboolean cb_panel_scroll(GtkWidget *widget, GdkEventScroll *ev, gpointer data)
{
  /*g_message("%s curr_desktop=%d", __func__, curr_desktop);*/
  switch(ev->direction) {
    case GDK_SCROLL_UP:
    case GDK_SCROLL_RIGHT:
      if(0 > --curr_desktop)
        curr_desktop = number_of_desktops - 1;
      break;
    case GDK_SCROLL_DOWN:
    case GDK_SCROLL_LEFT:
      curr_desktop++;
      curr_desktop %= number_of_desktops;
      break;
  }
  /*g_message("%s 2. curr_desktop=%d", __func__, curr_desktop);*/
  wa_send_xclimsg(xa_net_current_desktop, curr_desktop, 0, 0, 0, 0);
  return TRUE;
}

static void create_plugin(GtkWidget *vbox, gint first_create)
{
  gint i, y, w, btn_w;
  GkrellmTextstyle *ts, *ts_alt;
  GPtrArray *vdesk_names = g_ptr_array_new();
  GkrellmStyle *style = gkrellm_meter_style(style_id);
  /* See examples below */
  if(first_create)
    panel = gkrellm_panel_new0();

  for(xa_net_number_of_desktops = None, i = 0; i < 10; i++) {
    xa_net_number_of_desktops = XInternAtom(display, "_NET_NUMBER_OF_DESKTOPS", True);
    if(None == xa_net_number_of_desktops) {
      g_warning("%s(" CONFIG_TAB_NAME "): Still waiting for X atom _NET_NUMBER_OF_DESKTOPS",
          __func__);
      g_usleep(G_USEC_PER_SEC / 3);
    }
  }
  if(None == xa_net_number_of_desktops) {
    g_critical("%s(" CONFIG_TAB_NAME "): Perhaps a non EWMH conform window manager",
        __func__);
    xa_net_number_of_desktops = XInternAtom(display, "_NET_NUMBER_OF_DESKTOPS", False);
  }
  xa_net_current_desktop = XInternAtom(display, "_NET_CURRENT_DESKTOP", False);
  curr_desktop = get_ewmh_net_current_desktop();
  number_of_desktops = get_ewmh_net_number_of_desktops();
  { /* Determine desktop names if available */
    gint n;
    gchar *s;
    Atom xa_net_desktop_names = XInternAtom(display, "_NET_DESKTOP_NAMES", False);
    Atom xa_utf8_string = XInternAtom(display, "UTF8_STRING", False);
    gchar *sdata = get_ewmh_net_property_data(xa_net_desktop_names, xa_utf8_string, &n);
    if(sdata) {
      gint k=0, offset = 0;
      for(k = 0; offset < n; k++) {
        gchar *s = g_strdup(sdata + offset);
        g_ptr_array_add(vdesk_names, s);
        offset += strlen(s) + 1;
      }
      XFree(sdata);
    }
    /* If there are less desktop names given than there are desktops available construct
       artifical names */
    for(n = vdesk_names->len; n < number_of_desktops; n++) {
      s = g_strdup_printf("vdesk%02d", n);
      g_ptr_array_add(vdesk_names, s);
    }
  }
  w = gkrellm_chart_width();
  btn_w = (w * 75) / 100;
  /* Each Style has two text styles.  The theme designer has picked the
     |  colors and font sizes, presumably based on knowledge of what you draw
     |  on your panel.  You just do the drawing.  You can assume that the
     |  ts font is larger than the ts_alt font.
   */
  ts = gkrellm_meter_textstyle(style_id);
  ts_alt = gkrellm_meter_alt_textstyle(style_id);

  title_text = gkrellm_create_decal_text(panel, "Ay", ts, style,
      -1,     /* x = -1 places at left margin */
      -1,     /* y = -1 places at top margin  */
      -1);    /* w = -1 makes decal the panel width minus margins */
  gkrellm_draw_decal_text(panel, title_text, CONFIG_TAB_NAME, -1);
  y = title_text->y + title_text->h + 2;


  g_ptr_array_set_size(ws_decal_arr, 0);
  g_ptr_array_set_size(status_decal_arr, 0);
  for(i = 0; i < number_of_desktops; i++) {
    gchar *vdsk_s;
    GkrellmDecal *ws_decal, *status_decal;

    /* Create the status decal */
    status_decal = gkrellm_create_decal_pixmap(panel,
        gkrellm_decal_misc_pixmap(), gkrellm_decal_misc_mask(), N_MISC_DECALS, style, 0, btn_w);
    //status_decal->x = w - status_decal->w;
    status_decal->y = y;
    gkrellm_draw_decal_pixmap(panel, status_decal,
        curr_desktop == i ? D_MISC_LED1 : D_MISC_LED0);
    g_ptr_array_add(status_decal_arr, status_decal);

    /* ==== Create a text decal and convert it into a decal button. ====
       |  Text decals are converted into buttons by being put into a meter or
       |  panel button.  This "put" overlays the text decal with special button
       |  in and out images that have a transparent interior and a non-transparent
       |  border. The "Hello" string is not an initialization string, it is just
       |  a vertical sizing string.  After the decal is created, draw the
       |  initial text onto the decal.
     */
    ws_decal = gkrellm_create_decal_text(panel, "Hello", ts_alt, style,
        status_decal->w + 2,
        y,      /* Place below the scrolling text1_decal     */
        btn_w);     /* w = 0 makes decal the sizing string width */
    vdsk_s = g_ptr_array_index(vdesk_names, i);
    gkrellm_draw_decal_text(panel, ws_decal, vdsk_s, -1);

    gkrellm_put_decal_in_meter_button(panel, ws_decal,
        cb_button,          /* Button clicked callback function */
        GINT_TO_POINTER(i), /* Arg to callback function      */
        NULL);              /* Optional margin struct to pad the size */
    /*gkrellm_draw_decal_text(panel, ws_decal[i], "hhhhh", -1);*/
    g_ptr_array_add(ws_decal_arr, ws_decal);

    y = ws_decal->y + ws_decal->h + 4;
  }

  /* Configure the panel to hold the above created decals, and create it. */
  gkrellm_panel_configure(panel, NULL, style);
  gkrellm_panel_create(vbox, monitor, panel);

  /* Note: the above gkrellm_draw_decal_text() call will not
     |  appear on the panel until a gkrellm_draw_panel_layers() call is
     |  made.  This will be done in update_plugin(), otherwise we would
     |  make the call here after the panel is created and anytime the
     |  decals are changed.
   */
  /*gkrellm_draw_panel_layers(panel);*/
  if(first_create) {
    g_signal_connect(G_OBJECT (panel->drawing_area), "expose_event",
        G_CALLBACK(panel_expose_event), NULL);
    g_signal_connect(G_OBJECT(panel->drawing_area), "scroll_event", G_CALLBACK(cb_panel_scroll), NULL);
  }
  g_ptr_array_free(vdesk_names, TRUE);
}

static void update_plugin(void)
{
  gint i;
  gchar tstr[64];

  curr_desktop = get_ewmh_net_current_desktop();
  /*g_message(__func__);*/
  for(i = 0; i < ws_decal_arr->len; i++) {
    GkrellmDecal *status_decal = g_ptr_array_index(status_decal_arr, i);

    gkrellm_draw_decal_pixmap(panel, status_decal,
        curr_desktop == i ? D_MISC_LED1 : D_MISC_LED0);
  }
  g_snprintf(tstr, sizeof(tstr), CONFIG_TAB_NAME "[%d]", curr_desktop);
  /*g_snprintf(tstr, sizeof(tstr), CONFIG_TAB_NAME "[%s]",
      g_ptr_array_index(vdesk_names, curr_desktop));*/
  gkrellm_draw_decal_text(panel, title_text, tstr, -1);
  gkrellm_draw_panel_layers(panel);
}


/*
 * Create a Config tab with:
 * 1. Help tab 
 * 2. About tab 
 */ 
static void create_plugin_tab(GtkWidget *tab_vbox)
{
  GtkWidget *tabs;
  GtkWidget *vbox; 
  GtkWidget *text;
  GtkWidget *aboutLabel;
  GtkWidget *aboutText;

  /* 
   * Make a couple of tabs.  One for Config and one for info.
   */
  tabs = gtk_notebook_new();
  gtk_notebook_set_tab_pos(GTK_NOTEBOOK(tabs), GTK_POS_TOP);
  gtk_box_pack_start(GTK_BOX(tab_vbox), tabs, TRUE, TRUE, 0);

  /* 
   * Help tab
   */
  vbox = gkrellm_gtk_notebook_page(tabs, "Help");
  text = gkrellm_gtk_scrolled_text_view(vbox, NULL, GTK_POLICY_AUTOMATIC,
      GTK_POLICY_AUTOMATIC);
  gkrellm_gtk_text_view_append_strings(text, pager_help,
      (sizeof(pager_help) / sizeof(gchar *)));

  /*
   * About tab
   */
  aboutText = gtk_label_new(pager_about);
  aboutLabel = gtk_label_new("About");
  gtk_notebook_append_page(GTK_NOTEBOOK(tabs), aboutText, aboutLabel);
}

static GkrellmMonitor  plugin_mon  =
{
  CONFIG_TAB_NAME,         /* Name, for config tab.        */
  0,              /* Id,  0 if a plugin           */
  create_plugin,  /* The create_plugin() function */
  update_plugin,  /* The update_plugin() function */
  create_plugin_tab, /* The create_plugin_tab() config function */
  NULL,           /* The apply_plugin_config() function      */

  NULL,           /* The save_plugin_config() function  */
  NULL,           /* The load_plugin_config() function  */
  CONFIG_TAB_NAME,        /* config keyword                     */

  NULL,           /* Undefined 2  */
  NULL,           /* Undefined 1  */
  NULL,           /* Undefined 0  */

  PLUGIN_PLACEMENT, /* Insert plugin before this monitor.       */
  NULL,           /* Handle if a plugin, filled in by GKrellM */
  NULL            /* path if a plugin, filled in by GKrellM   */
};

GkrellmMonitor * gkrellm_init_plugin(void)
{
  display = GDK_DISPLAY();
  rootwin = GDK_ROOT_WINDOW();

  ws_decal_arr = g_ptr_array_new();
  status_decal_arr = g_ptr_array_new();
  curr_desktop = number_of_desktops = 0;
  /*g_message("%s curr_desktop=%d number_of_desktops=%d",
    __func__, curr_desktop, number_of_desktops);*/
  style_id = gkrellm_add_meter_style(&plugin_mon, STYLE_NAME);
  monitor = &plugin_mon;
  return monitor;
}

/* vim600: path+=/usr/include/glib-2.0/**,/usr/include/gtk-2.0/**
 */
