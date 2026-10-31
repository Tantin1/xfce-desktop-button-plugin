/*
 * xfce-desktop-button-plugin
 * Reemplazo del plugin "Mostrar escritorio" con botón delgado,
 * modo línea, colores, perfiles predefinidos y peek al pasar el mouse.
 */

#ifndef DESKTOP_BUTTON_PLUGIN_H
#define DESKTOP_BUTTON_PLUGIN_H

#include <gtk/gtk.h>
#include <libxfce4panel/libxfce4panel.h>

G_BEGIN_DECLS

/* ---------- enumeraciones de configuración ---------- */

typedef enum
{
    PROFILE_CUSTOM = 0,
    PROFILE_GLASS,       /* "Vidrio"        — inspirado en Windows 7  */
    PROFILE_STRIP,       /* "Franja"        — inspirado en Windows 10 */
    PROFILE_MINIMAL,     /* "Mínimo"        — inspirado en Windows 11 */
    PROFILE_CLASSIC      /* "Icono clásico" — como el botón original de Xfce */
} DbProfile;

typedef enum
{
    STYLE_LINE = 0,
    STYLE_ICON,
    STYLE_NONE           /* solo fondos, sin línea ni icono */
} DbStyle;

typedef enum
{
    ALIGN_START = 0,     /* izquierda en panel horizontal, arriba en vertical */
    ALIGN_CENTER,
    ALIGN_END
} DbAlign;

typedef enum
{
    LINE_ALWAYS = 0,
    LINE_HOVER           /* la línea aparece solo al pasar el mouse */
} DbLineVisibility;

typedef enum
{
    MIDDLE_NONE = 0,
    MIDDLE_TOGGLE,
    MIDDLE_MINIMIZE_ALL,
    MIDDLE_SHADE         /* enrollar / desenrollar ventanas (solo X11) */
} DbMiddleAction;

typedef enum
{
    SCROLL_NONE = 0,
    SCROLL_WORKSPACES,
    SCROLL_SHOW_HIDE     /* rueda arriba: mostrar escritorio, abajo: restaurar */
} DbScrollAction;

typedef enum
{
    ANIM_FAST = 0,
    ANIM_NORMAL,
    ANIM_SLOW
} DbAnimSpeed;

typedef enum
{
    TONE_AUTO = 0,       /* detectar si el panel es claro u oscuro */
    TONE_DARK,
    TONE_LIGHT
} DbPanelTone;

/* ---------- configuración ---------- */

typedef struct
{
    DbProfile        profile;

    /* tamaño del botón a lo largo del panel (px) */
    gint             thickness;

    DbStyle          style;

    /* modo línea */
    gint             line_width;      /* grosor (px) */
    gint             line_length;     /* largo: % o px según line_length_pct */
    gboolean         line_length_pct;
    DbAlign          line_align;
    DbLineVisibility line_visibility;
    GdkRGBA          line_color;
    GdkRGBA          line_hover_color;
    GdkRGBA          line_pressed_color;
    gboolean         line_round;      /* puntas redondeadas en la línea */

    /* modo icono */
    gchar           *icon_name;
    gint             icon_size;
    gboolean         icon_follow_panel; /* usar el "tamaño de iconos" del panel */

    /* fondos (alpha 0 = transparente) */
    GdkRGBA          bg_color;
    GdkRGBA          hover_bg_color;
    GdkRGBA          active_bg_color; /* mientras se mantiene el clic */
    gboolean         glass;           /* brillo degradado estilo Aero */
    gboolean         highlight_shown; /* mantener el fondo "presionado" con el escritorio visible */
    gint             bg_radius;       /* radio de esquinas del fondo (px) */
    gint             bg_margin;       /* margen interior del fondo (px) */

    /* transiciones suaves entre estados (hover, presionado) */
    gboolean         animate;
    DbAnimSpeed      anim_speed;

    /* tono del panel: decide la variante clara u oscura de los perfiles */
    DbPanelTone      panel_tone;

    /* comportamiento */
    DbMiddleAction   middle_action;
    DbScrollAction   scroll_action;
    gboolean         drag_hover;      /* mostrar escritorio al arrastrar encima */
    gboolean         drop_to_desktop; /* copiar al Escritorio los archivos soltados encima */
    gboolean         show_tooltip;

    /* peek: mostrar el escritorio mientras el mouse está encima */
    gboolean         peek_on_hover;
    gint             peek_delay;      /* ms */
} DbConfig;

/* ---------- estado del plugin ---------- */

typedef struct
{
    XfcePanelPlugin *plugin;
    GtkWidget       *area;            /* GtkDrawingArea: todo se dibuja a mano */
    GtkWidget       *dialog;

    gpointer         wm;              /* backend de ventanas (wm.c): xfw o wnck */

    gpointer         tone_channel;    /* XfconfChannel del panel (tone.c) */
    gulong           tone_handler;
    gint             cached_panel_id; /* evita recorrer /panels en cada consulta */
    gboolean         light_panel;     /* resultado del tono en uso */

    gboolean         showing;         /* escritorio mostrado */
    gboolean         hovered;
    gboolean         pressed;

    /* animación: cada valor va de 0 a 1 y persigue su objetivo
     * (hovered, pressed, showing) cuadro a cuadro */
    gdouble          anim_hover;
    gdouble          anim_press;
    gdouble          anim_shown;
    guint            anim_tick_id;
    gint64           anim_last_time;

    guint            drag_timeout_id;
    guint            peek_timeout_id;
    gboolean         shown_by_peek;   /* el escritorio visible lo puso el peek */

    /* caché de icono y acumulador de scroll suave */
    cairo_surface_t *icon_surf;
    gchar           *cached_icon_name;
    gint             cached_icon_size;
    gint             cached_icon_scale;
    gdouble          scroll_acc;

    /* menú contextual: submenú de perfiles */
    GtkWidget       *menu_profiles[PROFILE_CLASSIC + 1];
    GtkWidget       *menu_shade;
    gboolean         menu_updating;

    DbConfig         cfg;
} DbPlugin;

/* config.c */
void     db_config_init_defaults (DbConfig *cfg);
void     db_config_apply_profile (DbConfig *cfg, DbProfile profile, gboolean light);
void     db_config_load          (DbPlugin *db);
void     db_config_load_file     (DbConfig *cfg, const gchar *file);
void     db_config_save          (DbPlugin *db);
void     db_config_clear         (DbConfig *cfg);

/* plugin.c */
void     db_plugin_update_size     (DbPlugin *db);
void     db_plugin_update_tooltip  (DbPlugin *db);
void     db_plugin_redraw          (DbPlugin *db);
void     db_plugin_showing_changed (DbPlugin *db);
void     db_plugin_apply_tone      (DbPlugin *db);
gint     db_plugin_icon_size       (DbPlugin *db);
void     db_plugin_invalidate_icon (DbPlugin *db);
void     db_plugin_set_profile     (DbPlugin *db, DbProfile profile);
void     db_plugin_config_replaced (DbPlugin *db);

/* wm.c — mostrar escritorio y ventanas (libxfce4windowing o libwnck) */
gboolean db_wm_init              (DbPlugin *db);
void     db_wm_free              (DbPlugin *db);
gboolean db_wm_available         (DbPlugin *db);
gboolean db_wm_get_showing       (DbPlugin *db);
void     db_wm_set_showing       (DbPlugin *db, gboolean show);
void     db_wm_minimize_all      (DbPlugin *db);
void     db_wm_shade_all         (DbPlugin *db);
void     db_wm_switch_workspace  (DbPlugin *db, gint delta, guint32 timestamp);
gboolean db_wm_can_shade            (DbPlugin *db);
gboolean db_wm_can_switch_workspace (DbPlugin *db);

/* tone.c — detección de panel claro / oscuro */
void     db_tone_init            (DbPlugin *db);
void     db_tone_free            (DbPlugin *db);
gboolean db_tone_detect_light    (DbPlugin *db);
gint     db_tone_panel_of_plugin (DbPlugin *db, gint plugin_id);
GList   *db_tone_list_siblings   (DbPlugin *db);   /* ids de otros botones (GINT_TO_POINTER) */

/* drop.c — copiar al Escritorio los archivos soltados sobre el botón */
void     db_drop_init            (DbPlugin *db);
gchar   *db_drop_unique_name     (const gchar *basename, gboolean is_dir, guint n);
void     db_drop_copy_file       (GFile *src, GFile *desktop);   /* síncrona; la usa check.sh */

/* dialog.c */
void     db_dialog_show (DbPlugin *db);
void     db_dialog_sync (DbPlugin *db);

G_END_DECLS

#endif /* DESKTOP_BUTTON_PLUGIN_H */
