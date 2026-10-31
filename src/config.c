/*
 * config.c — valores por defecto, perfiles y guardado (XfceRc)
 */

#include <string.h>

#include "plugin.h"

#include <libxfce4util/libxfce4util.h>

/* ---------- utilidades de color ---------- */

static void
rgba_set (GdkRGBA *c, gdouble r, gdouble g, gdouble b, gdouble a)
{
    c->red = r; c->green = g; c->blue = b; c->alpha = a;
}

static void
rc_read_color (XfceRc *rc, const gchar *key, GdkRGBA *c)
{
    const gchar *s = xfce_rc_read_entry (rc, key, NULL);
    GdkRGBA tmp;

    if (s != NULL && gdk_rgba_parse (&tmp, s))
        *c = tmp;
}

static void
rc_write_color (XfceRc *rc, const gchar *key, const GdkRGBA *c)
{
    gchar *s = gdk_rgba_to_string (c);
    xfce_rc_write_entry (rc, key, s);
    g_free (s);
}

/* ---------- perfiles ---------- */

/*
 * Los perfiles solo tocan la apariencia. El comportamiento
 * (clic medio, rueda, arrastre, tooltip, peek) se respeta.
 *
 * Nombres propios para no usar marcas registradas como nombre:
 *   Vidrio  ≈ Windows 7,  Franja ≈ Windows 10,  Mínimo ≈ Windows 11,
 *   Icono clásico ≈ el botón original de Xfce.
 *
 * "light" elige la variante para panel claro: los mismos valores,
 * pero con negro translúcido en lugar de blanco. El brillo de vidrio
 * sigue siendo blanco.
 *
 * Al presionar, la línea se atenúa respecto del hover.
 */
#define TINT(c, a) rgba_set ((c), ink, ink, ink, (a))

void
db_config_apply_profile (DbConfig *cfg, DbProfile profile, gboolean light)
{
    const gdouble ink = light ? 0.0 : 1.0;

    cfg->profile = profile;

    switch (profile)
    {
    case PROFILE_GLASS:
        /* rectángulo translúcido con brillo y borde */
        cfg->thickness       = 15;
        cfg->style           = STYLE_LINE;
        cfg->line_width      = 1;
        cfg->line_length     = 100;
        cfg->line_length_pct = TRUE;
        cfg->line_align      = ALIGN_START;
        cfg->line_visibility = LINE_ALWAYS;
        TINT (&cfg->line_color,         0.40);
        TINT (&cfg->line_hover_color,   0.70);
        TINT (&cfg->line_pressed_color, 0.55);
        TINT (&cfg->bg_color,           0.08);
        TINT (&cfg->hover_bg_color,     0.30);
        TINT (&cfg->active_bg_color,    0.40);
        cfg->glass           = TRUE;
        cfg->line_round      = FALSE;
        cfg->bg_radius       = 0;
        cfg->bg_margin       = 0;
        break;

    case PROFILE_STRIP:
        /* franja fina con separador del alto del panel y resaltado al pasar */
        cfg->thickness       = 6;
        cfg->style           = STYLE_LINE;
        cfg->line_width      = 1;
        cfg->line_length     = 100;
        cfg->line_length_pct = TRUE;
        cfg->line_align      = ALIGN_START;
        cfg->line_visibility = LINE_ALWAYS;
        TINT (&cfg->line_color,         0.35);
        TINT (&cfg->line_hover_color,   0.60);
        TINT (&cfg->line_pressed_color, 0.45);
        TINT (&cfg->bg_color,           0.00);
        TINT (&cfg->hover_bg_color,     0.12);
        TINT (&cfg->active_bg_color,    0.18);
        cfg->glass           = FALSE;
        cfg->line_round      = FALSE;
        cfg->bg_radius       = 0;
        cfg->bg_margin       = 0;
        break;

    case PROFILE_MINIMAL:
        /* invisible en reposo; una línea fina aparece al pasar el mouse
         * y se atenúa al hacer clic. Sin fondos. */
        cfg->thickness       = 6;
        cfg->style           = STYLE_LINE;
        cfg->line_width      = 1;
        cfg->line_length     = 50;
        cfg->line_length_pct = TRUE;
        cfg->line_align      = ALIGN_START;
        cfg->line_visibility = LINE_HOVER;
        TINT (&cfg->line_color,         0.55);
        TINT (&cfg->line_hover_color,   0.55);
        TINT (&cfg->line_pressed_color, 0.33);
        TINT (&cfg->bg_color,           0.00);
        TINT (&cfg->hover_bg_color,     0.00);
        TINT (&cfg->active_bg_color,    0.00);
        cfg->glass           = FALSE;
        cfg->line_round      = TRUE;   /* línea tipo "píldora" */
        cfg->bg_radius       = 0;
        cfg->bg_margin       = 0;
        break;

    case PROFILE_CLASSIC:
        /* icono del plugin original con el tamaño de iconos del panel;
         * el ancho real (icono + margen) lo calcula db_plugin_update_size */
        cfg->thickness       = 24;
        cfg->style           = STYLE_ICON;
        g_free (cfg->icon_name);
        cfg->icon_name       = g_strdup ("org.xfce.panel.showdesktop");
        cfg->icon_follow_panel = TRUE;
        cfg->line_width      = 1;
        cfg->line_length     = 100;
        cfg->line_length_pct = TRUE;
        cfg->line_align      = ALIGN_START;
        cfg->line_visibility = LINE_ALWAYS;
        TINT (&cfg->line_color,         0.35);
        TINT (&cfg->line_hover_color,   0.60);
        TINT (&cfg->line_pressed_color, 0.45);
        TINT (&cfg->bg_color,           0.00);
        TINT (&cfg->hover_bg_color,     0.10);
        TINT (&cfg->active_bg_color,    0.16);
        cfg->glass           = FALSE;
        cfg->line_round      = FALSE;
        cfg->bg_radius       = 4;      /* resaltado redondeado alrededor del icono */
        cfg->bg_margin       = 2;
        break;

    case PROFILE_CUSTOM:
    default:
        break;
    }
}

#undef TINT

void
db_config_init_defaults (DbConfig *cfg)
{
    memset (cfg, 0, sizeof (*cfg));

    cfg->icon_name     = g_strdup ("user-desktop");
    cfg->icon_size     = 16;
    cfg->icon_follow_panel = FALSE;

    cfg->middle_action = MIDDLE_MINIMIZE_ALL;
    cfg->scroll_action = SCROLL_NONE;
    cfg->drag_hover    = TRUE;
    cfg->show_tooltip  = TRUE;

    cfg->highlight_shown = FALSE;
    cfg->panel_tone      = TONE_AUTO;

    cfg->peek_on_hover   = FALSE;
    cfg->peek_delay      = 500;

    cfg->animate         = TRUE;
    cfg->anim_speed      = ANIM_NORMAL;

    /* "Franja" por defecto: delgado y visible desde el primer momento.
     * La variante clara/oscura se ajusta después, al detectar el panel. */
    db_config_apply_profile (cfg, PROFILE_STRIP, FALSE);
}

void
db_config_clear (DbConfig *cfg)
{
    g_free (cfg->icon_name);
    cfg->icon_name = NULL;
}

/* ---------- lectura / escritura ---------- */

void
db_config_load (DbPlugin *db)
{
    gchar *file = xfce_panel_plugin_lookup_rc_file (db->plugin);

    db_config_load_file (&db->cfg, file);
    g_free (file);
}

/* lee un archivo de configuración; también sirve para copiar los
 * ajustes de otro botón. Sin archivo, quedan los valores por defecto. */
void
db_config_load_file (DbConfig *cfg, const gchar *file)
{
    XfceRc *rc;

    db_config_clear (cfg);
    db_config_init_defaults (cfg);

    if (file == NULL || !g_file_test (file, G_FILE_TEST_IS_REGULAR))
        return;

    rc = xfce_rc_simple_open (file, TRUE);
    if (rc == NULL)
        return;

    cfg->profile = CLAMP (xfce_rc_read_int_entry (rc, "profile", cfg->profile),
                          PROFILE_CUSTOM, PROFILE_CLASSIC);

    if (cfg->profile == PROFILE_CUSTOM)
    {
        cfg->thickness       = xfce_rc_read_int_entry  (rc, "thickness",       cfg->thickness);
        cfg->style           = xfce_rc_read_int_entry  (rc, "style",           cfg->style);
        cfg->line_width      = xfce_rc_read_int_entry  (rc, "line-width",      cfg->line_width);
        cfg->line_length     = xfce_rc_read_int_entry  (rc, "line-length",     cfg->line_length);
        cfg->line_length_pct = xfce_rc_read_bool_entry (rc, "line-length-pct", cfg->line_length_pct);
        cfg->line_align      = xfce_rc_read_int_entry  (rc, "line-align",      cfg->line_align);
        cfg->line_visibility = xfce_rc_read_int_entry  (rc, "line-visibility", cfg->line_visibility);
        cfg->line_round      = xfce_rc_read_bool_entry (rc, "line-round",      cfg->line_round);
        cfg->glass           = xfce_rc_read_bool_entry (rc, "glass",           cfg->glass);
        cfg->bg_radius       = xfce_rc_read_int_entry  (rc, "bg-radius",       cfg->bg_radius);
        cfg->bg_margin       = xfce_rc_read_int_entry  (rc, "bg-margin",       cfg->bg_margin);
        cfg->icon_follow_panel = xfce_rc_read_bool_entry (rc, "icon-follow-panel", cfg->icon_follow_panel);

        rc_read_color (rc, "line-color",       &cfg->line_color);
        rc_read_color (rc, "line-hover-color", &cfg->line_hover_color);
        /* archivos anteriores no tienen este color: usar el de hover */
        cfg->line_pressed_color = cfg->line_hover_color;
        rc_read_color (rc, "line-pressed-color", &cfg->line_pressed_color);
        rc_read_color (rc, "bg-color",         &cfg->bg_color);
        rc_read_color (rc, "hover-bg-color",   &cfg->hover_bg_color);
        rc_read_color (rc, "active-bg-color",  &cfg->active_bg_color);
    }
    else
    {
        /* perfil predefinido: los valores salen del código, así las mejoras
         * futuras de un perfil llegan sin tener que volver a elegirlo */
        db_config_apply_profile (cfg, cfg->profile, FALSE);
    }

    cfg->icon_size       = xfce_rc_read_int_entry  (rc, "icon-size",       cfg->icon_size);
    cfg->middle_action   = xfce_rc_read_int_entry  (rc, "middle-action",   cfg->middle_action);
    cfg->scroll_action   = xfce_rc_read_int_entry  (rc, "scroll-action",   cfg->scroll_action);
    cfg->drag_hover      = xfce_rc_read_bool_entry (rc, "drag-hover",      cfg->drag_hover);
    cfg->drop_to_desktop = xfce_rc_read_bool_entry (rc, "drop-to-desktop", cfg->drop_to_desktop);
    cfg->show_tooltip    = xfce_rc_read_bool_entry (rc, "show-tooltip",    cfg->show_tooltip);
    cfg->highlight_shown = xfce_rc_read_bool_entry (rc, "highlight-shown", cfg->highlight_shown);
    cfg->panel_tone      = xfce_rc_read_int_entry  (rc, "panel-tone",      cfg->panel_tone);
    cfg->peek_on_hover   = xfce_rc_read_bool_entry (rc, "peek-on-hover",   cfg->peek_on_hover);
    cfg->peek_delay      = xfce_rc_read_int_entry  (rc, "peek-delay",      cfg->peek_delay);
    cfg->animate         = xfce_rc_read_bool_entry (rc, "animate",         cfg->animate);
    cfg->anim_speed      = xfce_rc_read_int_entry  (rc, "anim-speed",      cfg->anim_speed);

    /* el perfil Icono clásico fija su propio icono */
    if (cfg->profile != PROFILE_CLASSIC)
    {
        const gchar *icon = xfce_rc_read_entry (rc, "icon-name", NULL);
        if (icon != NULL && *icon != '\0')
        {
            g_free (cfg->icon_name);
            cfg->icon_name = g_strdup (icon);
        }
    }

    xfce_rc_close (rc);

    cfg->style           = CLAMP (cfg->style,           STYLE_LINE,  STYLE_NONE);
    cfg->line_align      = CLAMP (cfg->line_align,      ALIGN_START, ALIGN_END);
    cfg->line_visibility = CLAMP (cfg->line_visibility, LINE_ALWAYS, LINE_HOVER);
    cfg->middle_action   = CLAMP (cfg->middle_action,   MIDDLE_NONE, MIDDLE_SHADE);
    cfg->scroll_action   = CLAMP (cfg->scroll_action,   SCROLL_NONE, SCROLL_SHOW_HIDE);
    cfg->panel_tone      = CLAMP (cfg->panel_tone,      TONE_AUTO,   TONE_LIGHT);
    cfg->anim_speed      = CLAMP (cfg->anim_speed,      ANIM_FAST,   ANIM_SLOW);

    /* límites por si el archivo fue editado a mano */
    cfg->thickness   = CLAMP (cfg->thickness,   1, 128);
    cfg->line_width  = CLAMP (cfg->line_width,  1, 64);
    cfg->line_length = CLAMP (cfg->line_length, 1, cfg->line_length_pct ? 100 : 256);
    cfg->icon_size   = CLAMP (cfg->icon_size,   8, 128);
    cfg->peek_delay  = CLAMP (cfg->peek_delay,  100, 3000);
    cfg->bg_radius   = CLAMP (cfg->bg_radius,   0, 16);
    cfg->bg_margin   = CLAMP (cfg->bg_margin,   0, 8);
}

void
db_config_save (DbPlugin *db)
{
    DbConfig *cfg = &db->cfg;
    gchar    *file;
    XfceRc   *rc;

    file = xfce_panel_plugin_save_location (db->plugin, TRUE);
    if (file == NULL)
        return;

    rc = xfce_rc_simple_open (file, FALSE);
    g_free (file);
    if (rc == NULL)
        return;

    xfce_rc_write_int_entry  (rc, "profile",         cfg->profile);
    xfce_rc_write_int_entry  (rc, "thickness",       cfg->thickness);
    xfce_rc_write_int_entry  (rc, "style",           cfg->style);
    xfce_rc_write_int_entry  (rc, "line-width",      cfg->line_width);
    xfce_rc_write_int_entry  (rc, "line-length",     cfg->line_length);
    xfce_rc_write_bool_entry (rc, "line-length-pct", cfg->line_length_pct);
    xfce_rc_write_int_entry  (rc, "line-align",      cfg->line_align);
    xfce_rc_write_int_entry  (rc, "line-visibility", cfg->line_visibility);
    xfce_rc_write_entry      (rc, "icon-name",       cfg->icon_name ? cfg->icon_name : "");
    xfce_rc_write_int_entry  (rc, "icon-size",       cfg->icon_size);
    xfce_rc_write_bool_entry (rc, "glass",           cfg->glass);
    xfce_rc_write_bool_entry (rc, "icon-follow-panel", cfg->icon_follow_panel);
    xfce_rc_write_int_entry  (rc, "middle-action",   cfg->middle_action);
    xfce_rc_write_int_entry  (rc, "scroll-action",   cfg->scroll_action);
    xfce_rc_write_bool_entry (rc, "drag-hover",      cfg->drag_hover);
    xfce_rc_write_bool_entry (rc, "drop-to-desktop", cfg->drop_to_desktop);
    xfce_rc_write_bool_entry (rc, "line-round",      cfg->line_round);
    xfce_rc_write_int_entry  (rc, "bg-radius",       cfg->bg_radius);
    xfce_rc_write_int_entry  (rc, "bg-margin",       cfg->bg_margin);
    xfce_rc_write_bool_entry (rc, "show-tooltip",    cfg->show_tooltip);
    xfce_rc_write_bool_entry (rc, "highlight-shown", cfg->highlight_shown);
    xfce_rc_write_int_entry  (rc, "panel-tone",      cfg->panel_tone);
    xfce_rc_write_bool_entry (rc, "peek-on-hover",   cfg->peek_on_hover);
    xfce_rc_write_int_entry  (rc, "peek-delay",      cfg->peek_delay);
    xfce_rc_write_bool_entry (rc, "animate",         cfg->animate);
    xfce_rc_write_int_entry  (rc, "anim-speed",      cfg->anim_speed);

    rc_write_color (rc, "line-color",       &cfg->line_color);
    rc_write_color (rc, "line-hover-color", &cfg->line_hover_color);
    rc_write_color (rc, "line-pressed-color", &cfg->line_pressed_color);
    rc_write_color (rc, "bg-color",         &cfg->bg_color);
    rc_write_color (rc, "hover-bg-color",   &cfg->hover_bg_color);
    rc_write_color (rc, "active-bg-color",  &cfg->active_bg_color);

    xfce_rc_close (rc);
}
