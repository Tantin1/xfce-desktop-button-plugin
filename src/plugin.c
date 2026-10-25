/*
 * plugin.c — registro del plugin, dibujo del botón y acciones
 *
 * El botón es un GtkDrawingArea en lugar de un GtkButton: así el tema
 * no le agrega padding ni bordes y se puede adelgazar hasta 1 px.
 */

#include <glib/gi18n-lib.h>

#include "plugin.h"

#define DRAG_HOVER_DELAY_MS 500

/* margen a cada lado del icono en el perfil Icono clásico */
#define CLASSIC_ICON_PADDING 4

/* ---------- helpers ---------- */

static gboolean
db_is_horizontal (DbPlugin *db)
{
    return xfce_panel_plugin_get_mode (db->plugin) == XFCE_PANEL_PLUGIN_MODE_HORIZONTAL;
}

void
db_plugin_redraw (DbPlugin *db)
{
    if (db->area != NULL)
        gtk_widget_queue_draw (db->area);
}

/* tamaño de icono efectivo: el del panel o el configurado */
gint
db_plugin_icon_size (DbPlugin *db)
{
    if (db->cfg.icon_follow_panel)
        return MAX (8, xfce_panel_plugin_get_icon_size (db->plugin));
    return db->cfg.icon_size;
}

void
db_plugin_update_size (DbPlugin *db)
{
    gint t;

    /* Icono clásico: el ancho acompaña al tamaño de iconos del panel */
    if (db->cfg.profile == PROFILE_CLASSIC)
        db->cfg.thickness = db_plugin_icon_size (db) + 2 * CLASSIC_ICON_PADDING;

    t = db->cfg.thickness;

    /* el eje delgado es siempre el que corre a lo largo del panel;
     * el otro eje (-1) lo llena el panel completo */
    if (db_is_horizontal (db))
        gtk_widget_set_size_request (GTK_WIDGET (db->plugin), t, -1);
    else
        gtk_widget_set_size_request (GTK_WIDGET (db->plugin), -1, t);

    db_plugin_redraw (db);
}

void
db_plugin_update_tooltip (DbPlugin *db)
{
    const gchar *text;
    AtkObject   *atk;

    if (db->area == NULL)
        return;

    if (!db_wm_available (db))
        text = _("Show desktop is not available in this session");
    else if (db->showing)
        text = _("Restore the minimized windows");
    else
        text = _("Minimize all open windows and show the desktop");

    /* accesibilidad: lectores de pantalla, aunque el tooltip esté apagado */
    atk = gtk_widget_get_accessible (db->area);
    if (atk != NULL)
    {
        atk_object_set_name (atk, _("Show Desktop"));
        atk_object_set_description (atk, text);
    }

    if (!db->cfg.show_tooltip)
    {
        gtk_widget_set_tooltip_text (db->area, NULL);
        gtk_widget_set_has_tooltip (db->area, FALSE);
        return;
    }

    gtk_widget_set_tooltip_text (db->area, text);
}

/* aplica la variante clara/oscura a los perfiles predefinidos */
void
db_plugin_apply_tone (DbPlugin *db)
{
    gboolean light;

    switch (db->cfg.panel_tone)
    {
    case TONE_DARK:  light = FALSE;                       break;
    case TONE_LIGHT: light = TRUE;                        break;
    case TONE_AUTO:
    default:         light = db_tone_detect_light (db);   break;
    }

    db->light_panel = light;

    if (db->cfg.profile != PROFILE_CUSTOM)
        db_config_apply_profile (&db->cfg, db->cfg.profile, light);

    db_dialog_sync (db);
    db_plugin_update_size (db);
}

/* ---------- acciones ---------- */

static void
peek_cancel_timeout (DbPlugin *db)
{
    if (db->peek_timeout_id != 0)
    {
        g_source_remove (db->peek_timeout_id);
        db->peek_timeout_id = 0;
    }
}

/* cualquier acción explícita del usuario deja de ser "peek" */
static void
db_toggle (DbPlugin *db)
{
    peek_cancel_timeout (db);
    db->shown_by_peek = FALSE;
    db_wm_set_showing (db, !db_wm_get_showing (db));
}

static void
db_set_showing (DbPlugin *db, gboolean show)
{
    peek_cancel_timeout (db);
    db->shown_by_peek = FALSE;
    db_wm_set_showing (db, show);
}

/* ---------- peek al pasar el mouse ----------
 *
 * Misma lógica que la opción "Mostrar al pasar el mouse" del plugin
 * original: tras el retardo se muestra el escritorio y, al salir, se
 * restauran las ventanas, pero solo si las ocultó el peek. Un clic
 * durante el peek lo vuelve permanente.
 */

static gboolean
peek_timeout_cb (gpointer data)
{
    DbPlugin *db = data;

    db->peek_timeout_id = 0;

    if (!db->showing && db->hovered && !db->pressed)
    {
        db->shown_by_peek = TRUE;
        db_wm_set_showing (db, TRUE);
    }
    return G_SOURCE_REMOVE;
}

static void
peek_start (DbPlugin *db)
{
    if (!db->cfg.peek_on_hover || db->showing || db->peek_timeout_id != 0
        || !db_wm_available (db))
        return;

    db->peek_timeout_id = g_timeout_add (db->cfg.peek_delay, peek_timeout_cb, db);
}

static void
peek_end (DbPlugin *db)
{
    peek_cancel_timeout (db);

    if (db->shown_by_peek)
    {
        db->shown_by_peek = FALSE;
        db_wm_set_showing (db, FALSE);
    }
}

/* ---------- dibujo ---------- */

static void
fill_rect_rgba (cairo_t *cr, const GdkRGBA *c, gdouble x, gdouble y, gdouble w, gdouble h)
{
    if (c->alpha <= 0.0)
        return;
    gdk_cairo_set_source_rgba (cr, c);
    cairo_rectangle (cr, x, y, w, h);
    cairo_fill (cr);
}

static void
draw_glass (cairo_t *cr, gboolean horizontal, gint w, gint h)
{
    cairo_pattern_t *pat;

    /* degradado desde el borde exterior del panel hacia el centro */
    if (horizontal)
        pat = cairo_pattern_create_linear (0, 0, 0, h);
    else
        pat = cairo_pattern_create_linear (0, 0, w, 0);

    cairo_pattern_add_color_stop_rgba (pat, 0.0,  1, 1, 1, 0.30);
    cairo_pattern_add_color_stop_rgba (pat, 0.5,  1, 1, 1, 0.06);
    cairo_pattern_add_color_stop_rgba (pat, 0.51, 1, 1, 1, 0.00);
    cairo_pattern_add_color_stop_rgba (pat, 1.0,  1, 1, 1, 0.04);

    cairo_set_source (cr, pat);
    cairo_rectangle (cr, 0, 0, w, h);
    cairo_fill (cr);
    cairo_pattern_destroy (pat);
}

static void
draw_line (DbPlugin *db, cairo_t *cr, gboolean horizontal, gint w, gint h)
{
    DbConfig      *cfg = &db->cfg;
    const GdkRGBA *color;
    gint           along, across, thick, len, pos_along, pos_across;

    if (cfg->line_visibility == LINE_HOVER && !db->hovered && !db->pressed)
        return;

    /* "along" = eje delgado del botón; "across" = alto del panel */
    along  = horizontal ? w : h;
    across = horizontal ? h : w;

    thick = MIN (cfg->line_width, along);
    if (cfg->line_length_pct)
        len = MAX (1, across * cfg->line_length / 100);
    else
        len = MIN (cfg->line_length, across);

    switch (cfg->line_align)
    {
    case ALIGN_START:  pos_along = 0;                    break;
    case ALIGN_END:    pos_along = along - thick;        break;
    case ALIGN_CENTER:
    default:           pos_along = (along - thick) / 2;  break;
    }
    pos_across = (across - len) / 2;

    /* presionado atenúa, hover aclara (estilo Fluent) */
    if (db->pressed)
        color = &cfg->line_pressed_color;
    else if (db->hovered)
        color = &cfg->line_hover_color;
    else
        color = &cfg->line_color;

    if (horizontal)
        fill_rect_rgba (cr, color, pos_along, pos_across, thick, len);
    else
        fill_rect_rgba (cr, color, pos_across, pos_along, len, thick);
}

static void
draw_icon (DbPlugin *db, GtkWidget *widget, cairo_t *cr, gint w, gint h)
{
    DbConfig        *cfg   = &db->cfg;
    GtkIconTheme    *theme = gtk_icon_theme_get_for_screen (gtk_widget_get_screen (widget));
    gint             scale = gtk_widget_get_scale_factor (widget);
    gint             size  = MIN (db_plugin_icon_size (db), MIN (w, h));
    cairo_surface_t *surf;

    if (size < 1)
        return;

    surf = gtk_icon_theme_load_surface (theme,
                                        cfg->icon_name ? cfg->icon_name : "user-desktop",
                                        size, scale, NULL,
                                        GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
    if (surf == NULL)
        surf = gtk_icon_theme_load_surface (theme, "user-desktop", size, scale, NULL,
                                            GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
    if (surf == NULL)
        return;

    cairo_set_source_surface (cr, surf, (w - size) / 2, (h - size) / 2);
    cairo_paint_with_alpha (cr, db->pressed ? 0.65 : db->hovered ? 1.0 : 0.85);
    cairo_surface_destroy (surf);
}

static gboolean
on_draw (GtkWidget *widget, cairo_t *cr, DbPlugin *db)
{
    DbConfig      *cfg        = &db->cfg;
    gint           w          = gtk_widget_get_allocated_width (widget);
    gint           h          = gtk_widget_get_allocated_height (widget);
    gboolean       horizontal = db_is_horizontal (db);
    const GdkRGBA *bg;

    /* fondo según estado: presionado > hover > normal.
     * Con el escritorio visible no queda resaltado, salvo que se pida. */
    if (db->pressed || (db->showing && cfg->highlight_shown))
        bg = &cfg->active_bg_color;
    else if (db->hovered)
        bg = &cfg->hover_bg_color;
    else
        bg = &cfg->bg_color;

    fill_rect_rgba (cr, bg, 0, 0, w, h);

    if (cfg->glass)
        draw_glass (cr, horizontal, w, h);

    switch (cfg->style)
    {
    case STYLE_LINE: draw_line (db, cr, horizontal, w, h);  break;
    case STYLE_ICON: draw_icon (db, widget, cr, w, h);      break;
    case STYLE_NONE:
    default:                                                break;
    }

    return FALSE;
}

/* ---------- eventos del mouse ---------- */

static gboolean
on_enter (GtkWidget *widget, GdkEventCrossing *ev, DbPlugin *db)
{
    (void) widget;
    if (ev->detail != GDK_NOTIFY_INFERIOR)
    {
        db->hovered = TRUE;
        db_plugin_redraw (db);
        peek_start (db);
    }
    return FALSE;
}

static gboolean
on_leave (GtkWidget *widget, GdkEventCrossing *ev, DbPlugin *db)
{
    (void) widget;
    if (ev->detail != GDK_NOTIFY_INFERIOR)
    {
        db->hovered = FALSE;
        db->pressed = FALSE;
        db_plugin_redraw (db);
        peek_end (db);
    }
    return FALSE;
}

static gboolean
on_button_press (GtkWidget *widget, GdkEventButton *ev, DbPlugin *db)
{
    (void) widget;

    if (ev->type != GDK_BUTTON_PRESS)
        return FALSE;

    if (ev->button == 1)
    {
        /* un clic antes de que venza el retardo no debe disparar el peek */
        peek_cancel_timeout (db);
        db->pressed = TRUE;
        db_plugin_redraw (db);
        return TRUE;
    }

    if (ev->button == 2)
    {
        switch (db->cfg.middle_action)
        {
        case MIDDLE_TOGGLE:       db_toggle (db);          return TRUE;
        case MIDDLE_MINIMIZE_ALL: db_wm_minimize_all (db); return TRUE;
        case MIDDLE_SHADE:        db_wm_shade_all (db);    return TRUE;
        case MIDDLE_NONE:
        default:                                        break;
        }
    }

    /* botón 3: lo maneja el panel (menú contextual) */
    return FALSE;
}

static gboolean
on_button_release (GtkWidget *widget, GdkEventButton *ev, DbPlugin *db)
{
    (void) widget;

    if (ev->button != 1 || !db->pressed)
        return FALSE;

    db->pressed = FALSE;
    /* como un botón real: solo actúa si se suelta encima.
     * Durante un peek, el clic lo deja fijo en lugar de restaurar. */
    if (db->hovered)
    {
        if (db->shown_by_peek)
            db->shown_by_peek = FALSE;
        else
            db_toggle (db);
    }

    db_plugin_redraw (db);
    return TRUE;
}

static gboolean
on_scroll (GtkWidget *widget, GdkEventScroll *ev, DbPlugin *db)
{
    gint dir = 0;   /* -1 = arriba, +1 = abajo */

    (void) widget;

    if (db->cfg.scroll_action == SCROLL_NONE)
        return FALSE;

    switch (ev->direction)
    {
    case GDK_SCROLL_UP:
    case GDK_SCROLL_LEFT:  dir = -1; break;
    case GDK_SCROLL_DOWN:
    case GDK_SCROLL_RIGHT: dir =  1; break;
    case GDK_SCROLL_SMOOTH:
        {
            gdouble dx = 0, dy = 0;
            gdk_event_get_scroll_deltas ((GdkEvent *) ev, &dx, &dy);
            if (dy < 0 || dx < 0) dir = -1;
            else if (dy > 0 || dx > 0) dir = 1;
        }
        break;
    default: break;
    }

    if (dir == 0)
        return FALSE;

    if (db->cfg.scroll_action == SCROLL_WORKSPACES)
        db_wm_switch_workspace (db, dir, ev->time);
    else if (db->cfg.scroll_action == SCROLL_SHOW_HIDE)
        db_set_showing (db, dir < 0);

    return TRUE;
}

/* ---------- arrastrar archivos encima ---------- */

static gboolean
drag_timeout_cb (gpointer data)
{
    DbPlugin *db = data;

    db->drag_timeout_id = 0;
    db_set_showing (db, TRUE);
    return G_SOURCE_REMOVE;
}

static gboolean
on_drag_motion (GtkWidget *widget, GdkDragContext *ctx,
                gint x, gint y, guint time_, DbPlugin *db)
{
    (void) widget; (void) x; (void) y;

    if (!db->cfg.drag_hover)
        return FALSE;

    if (!db->hovered)
    {
        db->hovered = TRUE;
        db_plugin_redraw (db);
    }

    if (db->drag_timeout_id == 0 && !db->showing)
        db->drag_timeout_id = g_timeout_add (DRAG_HOVER_DELAY_MS, drag_timeout_cb, db);

    /* no aceptamos el soltado: solo reaccionamos al paso */
    gdk_drag_status (ctx, 0, time_);
    return TRUE;
}

static void
on_drag_leave (GtkWidget *widget, GdkDragContext *ctx, guint time_, DbPlugin *db)
{
    (void) widget; (void) ctx; (void) time_;

    if (db->drag_timeout_id != 0)
    {
        g_source_remove (db->drag_timeout_id);
        db->drag_timeout_id = 0;
    }
    db->hovered = FALSE;
    db_plugin_redraw (db);
}

/* ---------- estado del escritorio (lo llama wm.c) ---------- */

void
db_plugin_showing_changed (DbPlugin *db)
{
    db->showing = db_wm_get_showing (db);

    /* si otra cosa restauró las ventanas, el peek ya no tiene nada que deshacer */
    if (!db->showing)
        db->shown_by_peek = FALSE;

    db_plugin_update_tooltip (db);
    db_plugin_redraw (db);
}

/* el tema GTK cambió: puede haber pasado de claro a oscuro */
static void
on_style_updated (GtkWidget *widget, DbPlugin *db)
{
    gboolean light;

    (void) widget;
    if (db->cfg.panel_tone != TONE_AUTO)
        return;

    light = db_tone_detect_light (db);
    if (light != db->light_panel)
        db_plugin_apply_tone (db);
}

/* ---------- señales del panel ---------- */

static gboolean
on_size_changed (XfcePanelPlugin *plugin, gint size, DbPlugin *db)
{
    (void) plugin; (void) size;
    db_plugin_update_size (db);
    return TRUE;
}

static void
on_mode_changed (XfcePanelPlugin *plugin, XfcePanelPluginMode mode, DbPlugin *db)
{
    (void) plugin; (void) mode;
    db_plugin_update_size (db);
}

/*
 * Atajos de teclado, igual que el plugin original:
 *   xfce4-panel --plugin-event=xfce-desktop-button-plugin:show            (alternar)
 *   xfce4-panel --plugin-event=xfce-desktop-button-plugin:show:bool:true  (mostrar)
 *   xfce4-panel --plugin-event=xfce-desktop-button-plugin:show:bool:false (restaurar)
 */
static gboolean
on_remote_event (XfcePanelPlugin *plugin, const gchar *name,
                 const GValue *value, DbPlugin *db)
{
    (void) plugin;

    if (g_strcmp0 (name, "show") != 0)
        return FALSE;

    if (value != NULL && G_VALUE_HOLDS_BOOLEAN (value))
        db_set_showing (db, g_value_get_boolean (value));
    else
        db_toggle (db);

    return TRUE;
}

static void
on_save (XfcePanelPlugin *plugin, DbPlugin *db)
{
    (void) plugin;
    db_config_save (db);
}

static void
on_configure (XfcePanelPlugin *plugin, DbPlugin *db)
{
    (void) plugin;
    db_dialog_show (db);
}

static void
on_about (XfcePanelPlugin *plugin, DbPlugin *db)
{
    (void) db;
    gtk_show_about_dialog (NULL,
                           "program-name", _("Desktop Button"),
                           "version",      PLUGIN_VERSION,
                           "comments",     _("A slim, customizable Show Desktop button"),
                           "logo-icon-name", "user-desktop",
                           "license-type", GTK_LICENSE_GPL_2_0,
                           "screen",       gtk_widget_get_screen (GTK_WIDGET (plugin)),
                           NULL);
}

static void
on_free (XfcePanelPlugin *plugin, DbPlugin *db)
{
    (void) plugin;

    if (db->dialog != NULL)
        gtk_widget_destroy (db->dialog);

    if (db->drag_timeout_id != 0)
        g_source_remove (db->drag_timeout_id);

    peek_cancel_timeout (db);

    db_wm_free (db);
    db_tone_free (db);

    db_config_clear (&db->cfg);
    g_free (db);
}

/* ---------- construcción ---------- */

static void
db_plugin_construct (XfcePanelPlugin *plugin)
{
    DbPlugin   *db;

    bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

    db = g_new0 (DbPlugin, 1);
    db->plugin = plugin;

    db_config_load (db);

    if (db_wm_init (db))
        db->showing = db_wm_get_showing (db);

    db_tone_init (db);

    /* el plugin ocupa todo el alto del panel, aun en paneles de varias filas */
    xfce_panel_plugin_set_small (plugin, FALSE);

    db->area = gtk_drawing_area_new ();
    gtk_widget_add_events (db->area,
                           GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                           GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK |
                           GDK_SCROLL_MASK);
    gtk_container_add (GTK_CONTAINER (plugin), db->area);

    g_signal_connect (db->area, "draw",                 G_CALLBACK (on_draw),           db);
    g_signal_connect (db->area, "enter-notify-event",   G_CALLBACK (on_enter),          db);
    g_signal_connect (db->area, "leave-notify-event",   G_CALLBACK (on_leave),          db);
    g_signal_connect (db->area, "button-press-event",   G_CALLBACK (on_button_press),   db);
    g_signal_connect (db->area, "button-release-event", G_CALLBACK (on_button_release), db);
    g_signal_connect (db->area, "scroll-event",         G_CALLBACK (on_scroll),         db);
    g_signal_connect (db->area, "style-updated",        G_CALLBACK (on_style_updated),  db);

    /* mostrar escritorio al arrastrar un archivo encima (como el original) */
    gtk_drag_dest_set (db->area, 0, NULL, 0, 0);
    gtk_drag_dest_set_track_motion (db->area, TRUE);
    g_signal_connect (db->area, "drag-motion", G_CALLBACK (on_drag_motion), db);
    g_signal_connect (db->area, "drag-leave",  G_CALLBACK (on_drag_leave),  db);

    /* clic derecho sobre el área abre el menú del panel */
    xfce_panel_plugin_add_action_widget (plugin, db->area);

    xfce_panel_plugin_menu_show_configure (plugin);
    xfce_panel_plugin_menu_show_about (plugin);

    g_signal_connect (plugin, "size-changed",     G_CALLBACK (on_size_changed), db);
    g_signal_connect (plugin, "mode-changed",     G_CALLBACK (on_mode_changed), db);
    g_signal_connect (plugin, "save",             G_CALLBACK (on_save),         db);
    g_signal_connect (plugin, "configure-plugin", G_CALLBACK (on_configure),    db);
    g_signal_connect (plugin, "about",            G_CALLBACK (on_about),        db);
    g_signal_connect (plugin, "remote-event",     G_CALLBACK (on_remote_event), db);
    g_signal_connect (plugin, "free-data",        G_CALLBACK (on_free),         db);

    db_plugin_update_tooltip (db);

    gtk_widget_show_all (GTK_WIDGET (plugin));

    /* con el widget ya en el panel, el tema y xfconf dan el tono real */
    db_plugin_apply_tone (db);
}

XFCE_PANEL_PLUGIN_REGISTER (db_plugin_construct);
