/*
 * plugin.c — registro del plugin, dibujo del botón y acciones
 *
 * El botón es un GtkDrawingArea en lugar de un GtkButton: así el tema
 * no le agrega padding ni bordes y se puede adelgazar hasta 1 px.
 */

#include <math.h>
#include <glib/gi18n-lib.h>
#include <libxfce4util/libxfce4util.h>

#include "plugin.h"

#define DRAG_HOVER_DELAY_MS 500

/* margen a cada lado del icono en el perfil Icono clásico */
#define CLASSIC_ICON_PADDING 4

/* duración de cada transición a velocidad Normal (ms).
 * Asimétricas a propósito: presionar casi inmediato, apagarse más lento. */
#define ANIM_HOVER_IN_MS   120
#define ANIM_HOVER_OUT_MS  200
#define ANIM_PRESS_MS       60
#define ANIM_RELEASE_MS    150
#define ANIM_SHOWN_IN_MS   150
#define ANIM_SHOWN_OUT_MS  200

/* ---------- helpers ---------- */

static gboolean
db_is_horizontal (DbPlugin *db)
{
    return xfce_panel_plugin_get_mode (db->plugin) == XFCE_PANEL_PLUGIN_MODE_HORIZONTAL;
}

/* ---------- animación ----------
 *
 * Tres valores (hover, presionado, escritorio mostrado) van de 0 a 1.
 * Cuando cambia el estado, un "tick" sincronizado con el refresco de la
 * pantalla los acerca a su objetivo con una curva exponencial (rápida
 * al principio, suave al final). Si el estado cambia a mitad de camino,
 * la transición sigue desde donde está, sin saltos. Al llegar, el tick
 * se desactiva: en reposo no hay temporizadores ni consumo.
 */

static gboolean
anim_enabled (DbPlugin *db)
{
    gboolean system = TRUE;

    if (!db->cfg.animate || db->area == NULL)
        return FALSE;

    /* respeta "habilitar animaciones" del sistema (accesibilidad) */
    g_object_get (gtk_widget_get_settings (db->area), "gtk-enable-animations", &system, NULL);
    return system;
}

static gdouble
anim_speed_factor (DbPlugin *db)
{
    switch (db->cfg.anim_speed)
    {
    case ANIM_FAST: return 0.6;
    case ANIM_SLOW: return 1.7;
    default:        return 1.0;
    }
}

static gdouble
anim_target_hover (DbPlugin *db) { return db->hovered ? 1.0 : 0.0; }
static gdouble
anim_target_press (DbPlugin *db) { return db->pressed ? 1.0 : 0.0; }
static gdouble
anim_target_shown (DbPlugin *db) { return (db->showing && db->cfg.highlight_shown) ? 1.0 : 0.0; }

/* acerca *value a target; devuelve TRUE si todavía no llegó */
static gboolean
anim_step (gdouble *value, gdouble target, gdouble dt_ms,
           gdouble in_ms, gdouble out_ms, gdouble factor)
{
    gdouble duration = (target > *value ? in_ms : out_ms) * factor;
    /* tau = duración / 3: a la duración indicada se recorrió el 95 % */
    gdouble k = 1.0 - exp (-dt_ms * 3.0 / duration);

    *value += (target - *value) * k;

    if (fabs (target - *value) < 0.004)
    {
        *value = target;
        return FALSE;
    }
    return TRUE;
}

static gboolean
anim_tick (GtkWidget *widget, GdkFrameClock *clock, gpointer data)
{
    DbPlugin *db  = data;
    gint64    now = gdk_frame_clock_get_frame_time (clock);
    gdouble   dt, f;
    gboolean  busy = FALSE;

    /* el primer cuadro solo toma la referencia de tiempo */
    dt = db->anim_last_time != 0 ? (now - db->anim_last_time) / 1000.0 : 0.0;
    db->anim_last_time = now;
    dt = MIN (dt, 100.0);   /* un cuadro muy demorado no debe "saltar" */

    f = anim_speed_factor (db);
    busy |= anim_step (&db->anim_hover, anim_target_hover (db), dt,
                       ANIM_HOVER_IN_MS, ANIM_HOVER_OUT_MS, f);
    busy |= anim_step (&db->anim_press, anim_target_press (db), dt,
                       ANIM_PRESS_MS, ANIM_RELEASE_MS, f);
    busy |= anim_step (&db->anim_shown, anim_target_shown (db), dt,
                       ANIM_SHOWN_IN_MS, ANIM_SHOWN_OUT_MS, f);

    gtk_widget_queue_draw (widget);

    if (!busy)
    {
        db->anim_tick_id   = 0;
        db->anim_last_time = 0;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

/* lleva la animación hacia el estado actual y redibuja */
void
db_plugin_redraw (DbPlugin *db)
{
    if (db->area == NULL)
        return;

    if (!anim_enabled (db))
    {
        if (db->anim_tick_id != 0)
        {
            gtk_widget_remove_tick_callback (db->area, db->anim_tick_id);
            db->anim_tick_id   = 0;
            db->anim_last_time = 0;
        }
        db->anim_hover = anim_target_hover (db);
        db->anim_press = anim_target_press (db);
        db->anim_shown = anim_target_shown (db);
    }
    else if (db->anim_tick_id == 0
             && (db->anim_hover != anim_target_hover (db)
                 || db->anim_press != anim_target_press (db)
                 || db->anim_shown != anim_target_shown (db)))
    {
        db->anim_last_time = 0;
        db->anim_tick_id = gtk_widget_add_tick_callback (db->area, anim_tick, db, NULL);
    }

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
db_plugin_invalidate_icon (DbPlugin *db)
{
    if (db->icon_surf != NULL)
    {
        cairo_surface_destroy (db->icon_surf);
        db->icon_surf = NULL;
    }
    g_free (db->cached_icon_name);
    db->cached_icon_name  = NULL;
    db->cached_icon_size  = 0;
    db->cached_icon_scale = 0;
}

/* ---------- menú contextual ---------- */

static void
menu_sync (DbPlugin *db)
{
    gint i;

    db->menu_updating = TRUE;
    for (i = 0; i <= PROFILE_CLASSIC; i++)
        if (db->menu_profiles[i] != NULL)
            gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (db->menu_profiles[i]),
                                            (gint) db->cfg.profile == i);
    db->menu_updating = FALSE;

    if (db->menu_shade != NULL)
        gtk_widget_set_visible (db->menu_shade, db_wm_can_shade (db));
}

void
db_plugin_update_size (DbPlugin *db)
{
    XfcePanelPluginMode mode = xfce_panel_plugin_get_mode (db->plugin);
    gboolean            small_deskbar;
    gint                t;

    /* Icono clásico: el ancho acompaña al tamaño de iconos del panel */
    if (db->cfg.profile == PROFILE_CLASSIC)
        db->cfg.thickness = db_plugin_icon_size (db) + 2 * CLASSIC_ICON_PADDING;

    t = db->cfg.thickness;

    /* en modo Deskbar, el perfil Icono clásico se comporta como botón
     * compacto; los modos de línea/barra ocupan todo el ancho de la barra */
    small_deskbar = (mode == XFCE_PANEL_PLUGIN_MODE_DESKBAR
                     && db->cfg.profile == PROFILE_CLASSIC);
    xfce_panel_plugin_set_small (db->plugin, small_deskbar);

    /* el eje delgado es siempre el que corre a lo largo del panel;
     * el otro eje (-1) lo llena el panel completo */
    if (small_deskbar)
        gtk_widget_set_size_request (GTK_WIDGET (db->plugin), t, t);
    else if (db_is_horizontal (db))
        gtk_widget_set_size_request (GTK_WIDGET (db->plugin), t, -1);
    else
        gtk_widget_set_size_request (GTK_WIDGET (db->plugin), -1, t);

    /* todo cambio de apariencia pasa por acá: mantener el menú al día */
    menu_sync (db);

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

/* cambia de perfil desde fuera del diálogo (menú contextual) */
void
db_plugin_set_profile (DbPlugin *db, DbProfile profile)
{
    if (profile == db->cfg.profile)
        return;

    if (profile == PROFILE_CUSTOM)
        db->cfg.profile = PROFILE_CUSTOM;   /* conserva los valores actuales */
    else
        db_config_apply_profile (&db->cfg, profile, db->light_panel);

    db_plugin_invalidate_icon (db);
    db_plugin_update_size (db);
    db_plugin_update_tooltip (db);
    db_dialog_sync (db);
    db_config_save (db);
}

/* la configuración se reemplazó entera (copiada de otro botón) */
void
db_plugin_config_replaced (DbPlugin *db)
{
    db->showing = db_wm_get_showing (db);
    db_plugin_invalidate_icon (db);
    db_plugin_update_tooltip (db);
    db_plugin_apply_tone (db);   /* ajusta el tono, el tamaño, el diálogo y el menú */
    db_config_save (db);
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

/* mezcla a→b en proporción t, con alfa premultiplicado: así pasar de un
 * color transparente a uno visible no muestra tintes intermedios raros */
static GdkRGBA
mix_rgba (const GdkRGBA *a, const GdkRGBA *b, gdouble t)
{
    GdkRGBA r;
    gdouble alpha;

    if (t <= 0.0) return *a;
    if (t >= 1.0) return *b;

    alpha   = a->alpha + (b->alpha - a->alpha) * t;
    r.alpha = alpha;
    if (alpha <= 0.0)
    {
        r.red = r.green = r.blue = 0.0;
        return r;
    }
    r.red   = (a->red   * a->alpha + (b->red   * b->alpha - a->red   * a->alpha) * t) / alpha;
    r.green = (a->green * a->alpha + (b->green * b->alpha - a->green * a->alpha) * t) / alpha;
    r.blue  = (a->blue  * a->alpha + (b->blue  * b->alpha - a->blue  * a->alpha) * t) / alpha;
    return r;
}

/* rectángulo con esquinas redondeadas; el radio se limita a la mitad
 * del lado más corto, así nunca se deforma en botones muy delgados */
static void
rounded_rect (cairo_t *cr, gdouble x, gdouble y, gdouble w, gdouble h, gdouble r)
{
    r = MIN (r, MIN (w, h) / 2.0);

    if (r <= 0.0)
    {
        cairo_rectangle (cr, x, y, w, h);
        return;
    }

    cairo_new_sub_path (cr);
    cairo_arc (cr, x + w - r, y + r,     r, -G_PI / 2, 0);
    cairo_arc (cr, x + w - r, y + h - r, r, 0,          G_PI / 2);
    cairo_arc (cr, x + r,     y + h - r, r, G_PI / 2,   G_PI);
    cairo_arc (cr, x + r,     y + r,     r, G_PI,       3 * G_PI / 2);
    cairo_close_path (cr);
}

static void
fill_shape_rgba (cairo_t *cr, const GdkRGBA *c,
                 gdouble x, gdouble y, gdouble w, gdouble h, gdouble r)
{
    if (c->alpha <= 0.0 || w <= 0.0 || h <= 0.0)
        return;
    gdk_cairo_set_source_rgba (cr, c);
    rounded_rect (cr, x, y, w, h, r);
    cairo_fill (cr);
}

/* zona del fondo: todo el botón menos el margen, sin quedar en cero */
static void
bg_area (DbPlugin *db, gint w, gint h, gdouble *x, gdouble *y, gdouble *bw, gdouble *bh)
{
    gint m  = db->cfg.bg_margin;
    gint mx = MIN (m, (w - 1) / 2);
    gint my = MIN (m, (h - 1) / 2);

    *x  = mx;
    *y  = my;
    *bw = w - 2 * mx;
    *bh = h - 2 * my;
}

static void
draw_glass (cairo_t *cr, gboolean horizontal,
            gdouble x, gdouble y, gdouble w, gdouble h, gdouble r)
{
    cairo_pattern_t *pat;

    /* la luz viene de arriba (o de la izquierda en paneles verticales),
     * como en la barra de Windows 7, esté donde esté el panel */
    if (horizontal)
        pat = cairo_pattern_create_linear (0, y, 0, y + h);
    else
        pat = cairo_pattern_create_linear (x, 0, x + w, 0);

    cairo_pattern_add_color_stop_rgba (pat, 0.0,  1, 1, 1, 0.30);
    cairo_pattern_add_color_stop_rgba (pat, 0.5,  1, 1, 1, 0.06);
    cairo_pattern_add_color_stop_rgba (pat, 0.51, 1, 1, 1, 0.00);
    cairo_pattern_add_color_stop_rgba (pat, 1.0,  1, 1, 1, 0.04);

    cairo_set_source (cr, pat);
    rounded_rect (cr, x, y, w, h, r);
    cairo_fill (cr);
    cairo_pattern_destroy (pat);
}

static void
draw_line (DbPlugin *db, cairo_t *cr, gboolean horizontal, gint w, gint h)
{
    DbConfig *cfg = &db->cfg;
    GdkRGBA   color;
    gint      along, across, thick, len, pos_along, pos_across;

    /* hover aclara, presionado atenúa (estilo Fluent) */
    color = mix_rgba (&cfg->line_color, &cfg->line_hover_color, db->anim_hover);
    color = mix_rgba (&color, &cfg->line_pressed_color, db->anim_press);

    /* "solo con hover": la línea aparece y desaparece con un fundido */
    if (cfg->line_visibility == LINE_HOVER)
        color.alpha *= MAX (db->anim_hover, db->anim_press);

    if (color.alpha <= 0.0)
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

    /* puntas redondeadas: radio de medio grosor, efecto "píldora" */
    {
        gdouble r = cfg->line_round ? thick / 2.0 : 0.0;

        if (horizontal)
            fill_shape_rgba (cr, &color, pos_along, pos_across, thick, len, r);
        else
            fill_shape_rgba (cr, &color, pos_across, pos_along, len, thick, r);
    }
}

static void
draw_icon (DbPlugin *db, GtkWidget *widget, cairo_t *cr, gint w, gint h)
{
    DbConfig    *cfg  = &db->cfg;
    const gchar *name = (cfg->icon_name && *cfg->icon_name) ? cfg->icon_name : "user-desktop";
    gint         scale = gtk_widget_get_scale_factor (widget);
    gint         size  = MIN (db_plugin_icon_size (db), MIN (w, h));

    if (size < 1)
        return;

    if (db->icon_surf == NULL
        || db->cached_icon_size != size
        || db->cached_icon_scale != scale
        || g_strcmp0 (db->cached_icon_name, name) != 0)
    {
        GtkIconTheme *theme = gtk_icon_theme_get_for_screen (gtk_widget_get_screen (widget));

        db_plugin_invalidate_icon (db);
        db->icon_surf = gtk_icon_theme_load_surface (theme, name, size, scale, NULL,
                                                     GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
        if (db->icon_surf == NULL)
            db->icon_surf = gtk_icon_theme_load_surface (theme, "user-desktop", size, scale, NULL,
                                                         GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
        db->cached_icon_name  = g_strdup (name);
        db->cached_icon_size  = size;
        db->cached_icon_scale = scale;
    }

    if (db->icon_surf == NULL)
        return;

    cairo_set_source_surface (cr, db->icon_surf, (w - size) / 2, (h - size) / 2);
    /* reposo 0.85 → hover 1.0 → presionado 0.65 */
    {
        gdouble a = 0.85 + 0.15 * db->anim_hover;
        a += (0.65 - a) * db->anim_press;
        cairo_paint_with_alpha (cr, a);
    }
}

static gboolean
on_draw (GtkWidget *widget, cairo_t *cr, DbPlugin *db)
{
    DbConfig      *cfg        = &db->cfg;
    gint           w          = gtk_widget_get_allocated_width (widget);
    gint           h          = gtk_widget_get_allocated_height (widget);
    gboolean       horizontal = db_is_horizontal (db);
    GdkRGBA        bg;
    gdouble        bx, by, bw, bh;

    /* fondo: normal → hover → presionado, mezclados según la animación.
     * Con el escritorio visible no queda resaltado, salvo que se pida. */
    bg = mix_rgba (&cfg->bg_color, &cfg->hover_bg_color, db->anim_hover);
    bg = mix_rgba (&bg, &cfg->active_bg_color, MAX (db->anim_press, db->anim_shown));

    bg_area (db, w, h, &bx, &by, &bw, &bh);
    fill_shape_rgba (cr, &bg, bx, by, bw, bh, cfg->bg_radius);

    if (cfg->glass)
        draw_glass (cr, horizontal, bx, by, bw, bh, cfg->bg_radius);

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
on_grab_broken (GtkWidget *widget, GdkEventGrabBroken *ev, DbPlugin *db)
{
    (void) widget; (void) ev;
    if (db->pressed)
    {
        db->pressed = FALSE;
        db_plugin_redraw (db);
    }
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
    case GDK_SCROLL_LEFT:
        db->scroll_acc = 0.0;
        dir = -1;
        break;
    case GDK_SCROLL_DOWN:
    case GDK_SCROLL_RIGHT:
        db->scroll_acc = 0.0;
        dir = 1;
        break;
    case GDK_SCROLL_SMOOTH:
        {
            gdouble dx = 0, dy = 0, delta;
            gdk_event_get_scroll_deltas ((GdkEvent *) ev, &dx, &dy);
            delta = fabs (dy) >= fabs (dx) ? dy : dx;
            /* si el gesto invierte su dirección, reiniciar acumulador */
            if (delta * db->scroll_acc < 0.0)
                db->scroll_acc = 0.0;
            db->scroll_acc += delta;
            if (db->scroll_acc <= -1.0)
            {
                dir = -1;
                db->scroll_acc += 1.0;
            }
            else if (db->scroll_acc >= 1.0)
            {
                dir = 1;
                db->scroll_acc -= 1.0;
            }
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
    gboolean accept;

    (void) x; (void) y;

    /* se acepta soltar solo si la opción está activa y son archivos */
    accept = db->cfg.drop_to_desktop
             && gtk_drag_dest_find_target (widget, ctx, NULL) != GDK_NONE;

    if (!db->cfg.drag_hover && !accept)
        return FALSE;

    if (!db->hovered)
    {
        db->hovered = TRUE;
        db_plugin_redraw (db);
    }

    if (db->cfg.drag_hover && db->drag_timeout_id == 0 && !db->showing)
        db->drag_timeout_id = g_timeout_add (DRAG_HOVER_DELAY_MS, drag_timeout_cb, db);

    /* siempre "copiar": el original nunca se mueve ni se borra */
    gdk_drag_status (ctx, accept ? GDK_ACTION_COPY : 0, time_);
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

/* el tema GTK cambió: puede haber cambiado el tema de iconos o el tono */
static void
on_style_updated (GtkWidget *widget, DbPlugin *db)
{
    gboolean light;

    (void) widget;
    db_plugin_invalidate_icon (db);

    if (db->cfg.panel_tone != TONE_AUTO)
        return;

    light = db_tone_detect_light (db);
    if (light != db->light_panel)
        db_plugin_apply_tone (db);
}

static void
on_scale_factor_notify (GtkWidget *widget, GParamSpec *pspec, DbPlugin *db)
{
    (void) widget; (void) pspec;
    db_plugin_invalidate_icon (db);
    db_plugin_redraw (db);
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
on_menu_minimize (GtkMenuItem *item, DbPlugin *db)
{
    (void) item;
    db_wm_minimize_all (db);
}

static void
on_menu_shade (GtkMenuItem *item, DbPlugin *db)
{
    (void) item;
    db_wm_shade_all (db);
}

static void
on_menu_profile (GtkCheckMenuItem *item, DbPlugin *db)
{
    if (db->menu_updating || !gtk_check_menu_item_get_active (item))
        return;

    db_plugin_set_profile (db, GPOINTER_TO_INT (g_object_get_data (G_OBJECT (item), "profile")));
}

static void
menu_build (DbPlugin *db)
{
    /* TRANSLATORS: same profile names as in the properties dialog */
    static const gchar *names[PROFILE_CLASSIC + 1] =
        { N_("Custom"), N_("Glass"), N_("Strip"), N_("Minimal"), N_("Classic icon") };
    GtkWidget *item, *submenu;
    GSList    *group = NULL;
    gint       i;

    item = gtk_menu_item_new_with_mnemonic (_("_Minimize all windows"));
    g_signal_connect (item, "activate", G_CALLBACK (on_menu_minimize), db);
    gtk_widget_show (item);
    xfce_panel_plugin_menu_insert_item (db->plugin, GTK_MENU_ITEM (item));

    /* solo existe en X11: se oculta en Wayland (ver menu_sync) */
    db->menu_shade = gtk_menu_item_new_with_mnemonic (_("S_hade / unshade windows"));
    g_signal_connect (db->menu_shade, "activate", G_CALLBACK (on_menu_shade), db);
    gtk_widget_set_no_show_all (db->menu_shade, TRUE);
    xfce_panel_plugin_menu_insert_item (db->plugin, GTK_MENU_ITEM (db->menu_shade));

    submenu = gtk_menu_new ();
    for (i = 0; i <= PROFILE_CLASSIC; i++)
    {
        db->menu_profiles[i] = gtk_radio_menu_item_new_with_label (group, _(names[i]));
        group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (db->menu_profiles[i]));
        g_object_set_data (G_OBJECT (db->menu_profiles[i]), "profile", GINT_TO_POINTER (i));
        g_signal_connect (db->menu_profiles[i], "toggled", G_CALLBACK (on_menu_profile), db);
        gtk_menu_shell_append (GTK_MENU_SHELL (submenu), db->menu_profiles[i]);
    }
    gtk_widget_show_all (submenu);

    item = gtk_menu_item_new_with_mnemonic (_("_Profile"));
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), submenu);
    gtk_widget_show (item);
    xfce_panel_plugin_menu_insert_item (db->plugin, GTK_MENU_ITEM (item));

    menu_sync (db);
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

    if (db->anim_tick_id != 0 && db->area != NULL)
        gtk_widget_remove_tick_callback (db->area, db->anim_tick_id);

    db_plugin_invalidate_icon (db);
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

    /* igual que los plugins oficiales: además de enlazar el dominio, lo deja
     * como dominio por omisión del proceso. Sin esto GLib no traduce los
     * textos del plugin si el proceso contenedor no tiene traducción propia. */
    xfce_textdomain (GETTEXT_PACKAGE, LOCALEDIR, "UTF-8");

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
    g_signal_connect (db->area, "button-press-event",   G_CALLBACK (on_button_press),        db);
    g_signal_connect (db->area, "button-release-event", G_CALLBACK (on_button_release),      db);
    g_signal_connect (db->area, "grab-broken-event",    G_CALLBACK (on_grab_broken),         db);
    g_signal_connect (db->area, "scroll-event",         G_CALLBACK (on_scroll),              db);
    g_signal_connect (db->area, "style-updated",        G_CALLBACK (on_style_updated),       db);
    g_signal_connect (db->area, "notify::scale-factor", G_CALLBACK (on_scale_factor_notify), db);

    /* mostrar escritorio al arrastrar un archivo encima (como el original) */
    gtk_drag_dest_set (db->area, 0, NULL, 0, 0);
    gtk_drag_dest_set_track_motion (db->area, TRUE);
    g_signal_connect (db->area, "drag-motion", G_CALLBACK (on_drag_motion), db);
    g_signal_connect (db->area, "drag-leave",  G_CALLBACK (on_drag_leave),  db);

    /* clic derecho sobre el área abre el menú del panel */
    xfce_panel_plugin_add_action_widget (plugin, db->area);

    xfce_panel_plugin_menu_show_configure (plugin);
    xfce_panel_plugin_menu_show_about (plugin);
    menu_build (db);

    /* soltar archivos encima: copiarlos al Escritorio (drop.c) */
    db_drop_init (db);

    g_signal_connect (plugin, "size-changed",            G_CALLBACK (on_size_changed),            db);
    g_signal_connect (plugin, "mode-changed",            G_CALLBACK (on_mode_changed),            db);
    g_signal_connect (plugin, "save",             G_CALLBACK (on_save),         db);
    g_signal_connect (plugin, "configure-plugin", G_CALLBACK (on_configure),    db);
    g_signal_connect (plugin, "about",            G_CALLBACK (on_about),        db);
    g_signal_connect (plugin, "remote-event",     G_CALLBACK (on_remote_event), db);
    g_signal_connect (plugin, "free-data",        G_CALLBACK (on_free),         db);

    db_plugin_update_tooltip (db);

    /* el estado inicial se dibuja tal cual, sin animar desde cero */
    db->anim_shown = (db->showing && db->cfg.highlight_shown) ? 1.0 : 0.0;

    gtk_widget_show_all (GTK_WIDGET (plugin));

    /* con el widget ya en el panel, el tema y xfconf dan el tono real */
    db_plugin_apply_tone (db);
}

XFCE_PANEL_PLUGIN_REGISTER (db_plugin_construct);
