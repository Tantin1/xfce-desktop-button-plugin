/*
 * dialog.c — ventana de propiedades
 *
 * Todos los cambios se aplican en vivo sobre el panel.
 * Tocar cualquier ajuste de apariencia pasa el perfil a "Personalizado".
 */

#include <string.h>
#include <glib/gi18n-lib.h>

#include "plugin.h"

typedef struct
{
    DbPlugin  *db;
    gboolean   updating;   /* evita bucles mientras se rellenan los controles */

    GtkWidget *profile;
    GtkWidget *profile_hint;
    GtkWidget *panel_tone;
    GtkWidget *tone_hint;
    GtkWidget *thickness;
    GtkWidget *style;

    GtkWidget *line_frame;
    GtkWidget *line_width;
    GtkWidget *line_length;
    GtkWidget *line_unit;
    GtkWidget *line_align;
    GtkWidget *line_visibility;
    GtkWidget *line_color;
    GtkWidget *line_hover_color;
    GtkWidget *line_pressed_color;
    GtkWidget *line_round;

    GtkWidget *icon_frame;
    GtkWidget *icon_name;
    GtkWidget *icon_size;
    GtkWidget *icon_follow_panel;

    GtkWidget *bg_color;
    GtkWidget *hover_bg_color;
    GtkWidget *active_bg_color;
    GtkWidget *glass;
    GtkWidget *highlight_shown;
    GtkWidget *bg_radius;
    GtkWidget *bg_margin;
    GtkWidget *animate;
    GtkWidget *anim_speed;
    GtkWidget *anim_hint;

    GtkWidget *middle_action;
    GtkWidget *shade_hint;
    GtkWidget *scroll_action;
    GtkWidget *scroll_hint;
    GtkWidget *drag_hover;
    GtkWidget *drop_to_desktop;
    GtkWidget *show_tooltip;
    GtkWidget *peek_on_hover;
    GtkWidget *peek_delay;

    GtkWidget *copy_combo;
    GtkWidget *copy_button;
} DbDialog;

/* descripción de cada perfil, en el orden de DbProfile */
static const gchar *
profile_description (DbProfile p)
{
    switch (p)
    {
    case PROFILE_GLASS:   return _("Inspired by Windows 7");
    case PROFILE_STRIP:   return _("Inspired by Windows 10");
    case PROFILE_MINIMAL: return _("Inspired by Windows 11");
    case PROFILE_CLASSIC: return _("Like the original Xfce button");
    case PROFILE_CUSTOM:
    default:              return _("Your own settings");
    }
}

/* ---------- helpers de construcción ---------- */

static GtkWidget *
make_grid (void)
{
    GtkWidget *grid = gtk_grid_new ();
    gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
    gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
    gtk_container_set_border_width (GTK_CONTAINER (grid), 6);
    return grid;
}

static GtkWidget *
make_frame (const gchar *title, GtkWidget *child)
{
    GtkWidget *frame = gtk_frame_new (NULL);
    GtkWidget *label = gtk_label_new (NULL);
    gchar     *markup = g_markup_printf_escaped ("<b>%s</b>", title);

    gtk_label_set_markup (GTK_LABEL (label), markup);
    g_free (markup);
    gtk_frame_set_label_widget (GTK_FRAME (frame), label);
    gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_NONE);
    gtk_widget_set_margin_start (child, 12);
    gtk_container_add (GTK_CONTAINER (frame), child);
    return frame;
}

/* alinea la columna de etiquetas de todos los grupos de una misma pestaña */
static GtkSizeGroup *s_label_group = NULL;

static GtkWidget *
new_page (GtkWidget *notebook, const gchar *title)
{
    GtkWidget *page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);

    gtk_container_set_border_width (GTK_CONTAINER (page), 12);
    gtk_notebook_append_page (GTK_NOTEBOOK (notebook), page, gtk_label_new (title));

    if (s_label_group != NULL)
        g_object_unref (s_label_group);
    s_label_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

    return page;
}

static void
add_row (GtkWidget *grid, gint row, const gchar *text, GtkWidget *widget)
{
    GtkWidget *label = gtk_label_new_with_mnemonic (text);

    if (s_label_group != NULL)
        gtk_size_group_add_widget (s_label_group, label);

    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_label_set_mnemonic_widget (GTK_LABEL (label), widget);
    gtk_widget_set_hexpand (widget, TRUE);
    gtk_grid_attach (GTK_GRID (grid), label,  0, row, 1, 1);
    gtk_grid_attach (GTK_GRID (grid), widget, 1, row, 1, 1);
}

static GtkWidget *
make_combo (const gchar *first, ...)
{
    GtkWidget   *combo = gtk_combo_box_text_new ();
    const gchar *item;
    va_list      args;

    va_start (args, first);
    for (item = first; item != NULL; item = va_arg (args, const gchar *))
        gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), item);
    va_end (args);

    return combo;
}

static GtkWidget *
make_color (void)
{
    GtkWidget *btn = gtk_color_button_new ();
    gtk_color_chooser_set_use_alpha (GTK_COLOR_CHOOSER (btn), TRUE);
    return btn;
}

/* ---------- sincronización controles <-> configuración ---------- */

static void
update_length_range (DbDialog *d, gboolean pct)
{
    gtk_spin_button_set_range (GTK_SPIN_BUTTON (d->line_length), 1, pct ? 100 : 256);
}

static void
update_sensitivity (DbDialog *d)
{
    DbConfig *cfg = &d->db->cfg;

    gtk_widget_set_sensitive (d->line_frame, cfg->style == STYLE_LINE);
    gtk_widget_set_sensitive (d->icon_frame, cfg->style == STYLE_ICON);
    gtk_widget_set_sensitive (d->icon_size,  !cfg->icon_follow_panel);
    gtk_widget_set_sensitive (d->peek_delay, cfg->peek_on_hover);
    gtk_widget_set_sensitive (d->anim_speed, cfg->animate);

    if (d->shade_hint != NULL)
        gtk_widget_set_visible (d->shade_hint,
                                cfg->middle_action == MIDDLE_SHADE && !db_wm_can_shade (d->db));

    if (d->scroll_hint != NULL)
        gtk_widget_set_visible (d->scroll_hint,
                                cfg->scroll_action == SCROLL_WORKSPACES
                                && !db_wm_can_switch_workspace (d->db));
}

/* avisa si el sistema tiene las animaciones desactivadas */
static void
update_anim_hint (DbDialog *d)
{
    gboolean system = TRUE;

    g_object_get (gtk_widget_get_settings (d->anim_hint), "gtk-enable-animations", &system, NULL);
    gtk_widget_set_visible (d->anim_hint, !system);
}

/* cfg -> controles */
static void
dialog_fill (DbDialog *d)
{
    DbConfig *cfg = &d->db->cfg;

    d->updating = TRUE;

    gtk_combo_box_set_active (GTK_COMBO_BOX (d->profile), cfg->profile);
    gtk_label_set_text (GTK_LABEL (d->profile_hint), profile_description (cfg->profile));
    gtk_combo_box_set_active (GTK_COMBO_BOX (d->panel_tone), cfg->panel_tone);
    {
        gboolean detected = db_tone_detect_light (d->db);
        gtk_label_set_text (GTK_LABEL (d->tone_hint),
                            detected ? _("Detected: light panel") : _("Detected: dark panel"));
    }
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (d->thickness), cfg->thickness);
    gtk_combo_box_set_active (GTK_COMBO_BOX (d->style), cfg->style);

    gtk_spin_button_set_value (GTK_SPIN_BUTTON (d->line_width), cfg->line_width);
    gtk_combo_box_set_active (GTK_COMBO_BOX (d->line_unit), cfg->line_length_pct ? 0 : 1);
    update_length_range (d, cfg->line_length_pct);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (d->line_length), cfg->line_length);
    gtk_combo_box_set_active (GTK_COMBO_BOX (d->line_align), cfg->line_align);
    gtk_combo_box_set_active (GTK_COMBO_BOX (d->line_visibility), cfg->line_visibility);
    gtk_color_chooser_set_rgba (GTK_COLOR_CHOOSER (d->line_color), &cfg->line_color);
    gtk_color_chooser_set_rgba (GTK_COLOR_CHOOSER (d->line_hover_color), &cfg->line_hover_color);
    gtk_color_chooser_set_rgba (GTK_COLOR_CHOOSER (d->line_pressed_color), &cfg->line_pressed_color);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (d->line_round), cfg->line_round);

    gtk_entry_set_text (GTK_ENTRY (d->icon_name), cfg->icon_name ? cfg->icon_name : "");
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (d->icon_size), db_plugin_icon_size (d->db));
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (d->icon_follow_panel), cfg->icon_follow_panel);

    gtk_color_chooser_set_rgba (GTK_COLOR_CHOOSER (d->bg_color), &cfg->bg_color);
    gtk_color_chooser_set_rgba (GTK_COLOR_CHOOSER (d->hover_bg_color), &cfg->hover_bg_color);
    gtk_color_chooser_set_rgba (GTK_COLOR_CHOOSER (d->active_bg_color), &cfg->active_bg_color);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (d->glass), cfg->glass);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (d->highlight_shown), cfg->highlight_shown);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (d->bg_radius), cfg->bg_radius);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (d->bg_margin), cfg->bg_margin);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (d->animate), cfg->animate);
    gtk_combo_box_set_active (GTK_COMBO_BOX (d->anim_speed), cfg->anim_speed);

    gtk_combo_box_set_active (GTK_COMBO_BOX (d->middle_action), cfg->middle_action);
    gtk_combo_box_set_active (GTK_COMBO_BOX (d->scroll_action), cfg->scroll_action);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (d->drag_hover), cfg->drag_hover);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (d->drop_to_desktop), cfg->drop_to_desktop);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (d->show_tooltip), cfg->show_tooltip);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (d->peek_on_hover), cfg->peek_on_hover);
    gtk_spin_button_set_value (GTK_SPIN_BUTTON (d->peek_delay), cfg->peek_delay);

    d->updating = FALSE;

    update_sensitivity (d);
}

static void
apply_to_panel (DbDialog *d)
{
    db_plugin_update_size (d->db);
    db_plugin_update_tooltip (d->db);
    db_plugin_redraw (d->db);
}

/* controles de apariencia -> cfg; el perfil pasa a Personalizado */
static void
on_appearance_changed (GtkWidget *widget, DbDialog *d)
{
    DbConfig *cfg = &d->db->cfg;
    gboolean  pct;

    (void) widget;
    if (d->updating)
        return;

    cfg->thickness = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (d->thickness));
    cfg->style     = gtk_combo_box_get_active (GTK_COMBO_BOX (d->style));

    pct = gtk_combo_box_get_active (GTK_COMBO_BOX (d->line_unit)) == 0;
    if (pct != cfg->line_length_pct)
    {
        d->updating = TRUE;
        update_length_range (d, pct);
        d->updating = FALSE;
    }
    cfg->line_length_pct = pct;
    cfg->line_width      = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (d->line_width));
    cfg->line_length     = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (d->line_length));
    cfg->line_align      = gtk_combo_box_get_active (GTK_COMBO_BOX (d->line_align));
    cfg->line_visibility = gtk_combo_box_get_active (GTK_COMBO_BOX (d->line_visibility));
    gtk_color_chooser_get_rgba (GTK_COLOR_CHOOSER (d->line_color), &cfg->line_color);
    gtk_color_chooser_get_rgba (GTK_COLOR_CHOOSER (d->line_hover_color), &cfg->line_hover_color);
    gtk_color_chooser_get_rgba (GTK_COLOR_CHOOSER (d->line_pressed_color), &cfg->line_pressed_color);
    cfg->line_round = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (d->line_round));

    g_free (cfg->icon_name);
    cfg->icon_name = g_strdup (gtk_entry_get_text (GTK_ENTRY (d->icon_name)));
    cfg->icon_follow_panel = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (d->icon_follow_panel));
    if (!cfg->icon_follow_panel)
        cfg->icon_size = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (d->icon_size));

    gtk_color_chooser_get_rgba (GTK_COLOR_CHOOSER (d->bg_color), &cfg->bg_color);
    gtk_color_chooser_get_rgba (GTK_COLOR_CHOOSER (d->hover_bg_color), &cfg->hover_bg_color);
    gtk_color_chooser_get_rgba (GTK_COLOR_CHOOSER (d->active_bg_color), &cfg->active_bg_color);
    cfg->glass = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (d->glass));
    cfg->bg_radius = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (d->bg_radius));
    cfg->bg_margin = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (d->bg_margin));

    if (cfg->profile != PROFILE_CUSTOM)
    {
        cfg->profile = PROFILE_CUSTOM;
        d->updating = TRUE;
        gtk_combo_box_set_active (GTK_COMBO_BOX (d->profile), PROFILE_CUSTOM);
        gtk_label_set_text (GTK_LABEL (d->profile_hint), profile_description (PROFILE_CUSTOM));
        d->updating = FALSE;
    }

    update_sensitivity (d);
    apply_to_panel (d);
}

static void
on_behavior_changed (GtkWidget *widget, DbDialog *d)
{
    DbConfig *cfg = &d->db->cfg;

    (void) widget;
    if (d->updating)
        return;

    cfg->middle_action = gtk_combo_box_get_active (GTK_COMBO_BOX (d->middle_action));
    cfg->scroll_action = gtk_combo_box_get_active (GTK_COMBO_BOX (d->scroll_action));
    cfg->drag_hover    = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (d->drag_hover));
    cfg->drop_to_desktop = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (d->drop_to_desktop));
    cfg->show_tooltip  = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (d->show_tooltip));
    cfg->highlight_shown = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (d->highlight_shown));
    cfg->peek_on_hover   = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (d->peek_on_hover));
    cfg->peek_delay      = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (d->peek_delay));
    cfg->animate         = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (d->animate));
    cfg->anim_speed      = gtk_combo_box_get_active (GTK_COMBO_BOX (d->anim_speed));

    update_sensitivity (d);
    apply_to_panel (d);
}

static void
on_profile_changed (GtkComboBox *combo, DbDialog *d)
{
    DbProfile p;

    if (d->updating)
        return;

    p = gtk_combo_box_get_active (combo);
    if (p == PROFILE_CUSTOM)
    {
        d->db->cfg.profile = PROFILE_CUSTOM;
        gtk_label_set_text (GTK_LABEL (d->profile_hint), profile_description (PROFILE_CUSTOM));
        return;
    }

    db_config_apply_profile (&d->db->cfg, p, d->db->light_panel);
    /* primero el tamaño: Icono clásico calcula su ancho al aplicarse */
    apply_to_panel (d);
    dialog_fill (d);
}

static void
on_tone_changed (GtkComboBox *combo, DbDialog *d)
{
    if (d->updating)
        return;

    d->db->cfg.panel_tone = gtk_combo_box_get_active (combo);
    /* re-aplica el perfil con la variante correcta y refresca los controles */
    db_plugin_apply_tone (d->db);
}

/* lo llama el plugin cuando algo cambia desde afuera (tono detectado) */
void
db_dialog_sync (DbPlugin *db)
{
    DbDialog *d;

    if (db->dialog == NULL)
        return;

    d = g_object_get_data (G_OBJECT (db->dialog), "db-dialog");
    if (d != NULL)
        dialog_fill (d);
}

/* copia toda la configuración de otro Desktop Button */
static void
on_copy_clicked (GtkButton *button, DbDialog *d)
{
    DbPlugin    *db = d->db;
    const gchar *id_str;
    gchar       *own, *dir, *file;
    DbConfig     tmp;

    (void) button;

    id_str = gtk_combo_box_get_active_id (GTK_COMBO_BOX (d->copy_combo));
    if (id_str == NULL)
        return;

    /* el archivo del otro botón está junto al nuestro: <módulo>-<id>.rc */
    own  = xfce_panel_plugin_save_location (db->plugin, FALSE);
    dir  = own != NULL ? g_path_get_dirname (own) : NULL;
    file = dir != NULL ? g_strdup_printf ("%s" G_DIR_SEPARATOR_S "%s-%s.rc", dir,
                                          xfce_panel_plugin_get_name (db->plugin), id_str)
                       : NULL;

    memset (&tmp, 0, sizeof (tmp));
    db_config_load_file (&tmp, file);

    db_config_clear (&db->cfg);
    db->cfg = tmp;              /* la estructura pasa a ser nuestra (incluido icon_name) */

    db_plugin_config_replaced (db);

    g_free (file);
    g_free (dir);
    g_free (own);
}

/* llena la lista con los otros botones que hay en los paneles */
static void
fill_copy_combo (DbDialog *d)
{
    GList *ids = db_tone_list_siblings (d->db), *l;

    for (l = ids; l != NULL; l = l->next)
    {
        gint   id    = GPOINTER_TO_INT (l->data);
        gint   panel = db_tone_panel_of_plugin (d->db, id);
        gchar *key   = g_strdup_printf ("%d", id);
        /* TRANSLATORS: entry in "Copy settings from another button";
         * the first number is the panel, the second the plugin id */
        gchar *label = g_strdup_printf (_("Panel %d — button #%d"), panel, id);

        gtk_combo_box_text_append (GTK_COMBO_BOX_TEXT (d->copy_combo), key, label);
        g_free (label);
        g_free (key);
    }

    if (ids != NULL)
        gtk_combo_box_set_active (GTK_COMBO_BOX (d->copy_combo), 0);

    gtk_widget_set_sensitive (d->copy_combo,  ids != NULL);
    gtk_widget_set_sensitive (d->copy_button, ids != NULL);
    g_list_free (ids);
}

static void
on_response (GtkDialog *dialog, gint response, DbDialog *d)
{
    DbPlugin *db = d->db;

    (void) response;

    db_config_save (db);
    xfce_panel_plugin_unblock_menu (db->plugin);
    db->dialog = NULL;
    gtk_widget_destroy (GTK_WIDGET (dialog));
}

/* ---------- construcción del diálogo ---------- */

static void
connect_appearance (GtkWidget *w, const gchar *signal, DbDialog *d)
{
    g_signal_connect (w, signal, G_CALLBACK (on_appearance_changed), d);
}

static void
connect_behavior (GtkWidget *w, const gchar *signal, DbDialog *d)
{
    g_signal_connect (w, signal, G_CALLBACK (on_behavior_changed), d);
}

void
db_dialog_show (DbPlugin *db)
{
    DbDialog  *d;
    GtkWidget *dialog, *content, *notebook, *page, *grid;
    gint       row;

    if (db->dialog != NULL)
    {
        gtk_window_present (GTK_WINDOW (db->dialog));
        return;
    }

    d = g_new0 (DbDialog, 1);
    d->db = db;

    xfce_panel_plugin_block_menu (db->plugin);

    dialog = gtk_dialog_new_with_buttons (_("Desktop Button"),
                                          NULL, 0,
                                          _("_Close"), GTK_RESPONSE_CLOSE,
                                          NULL);
    db->dialog = dialog;
    g_object_set_data_full (G_OBJECT (dialog), "db-dialog", d, g_free);

    gtk_window_set_icon_name (GTK_WINDOW (dialog), "user-desktop");
    gtk_window_set_screen (GTK_WINDOW (dialog), gtk_widget_get_screen (GTK_WIDGET (db->plugin)));
    gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER);
    gtk_window_set_default_size (GTK_WINDOW (dialog), 420, -1);

    content  = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
    notebook = gtk_notebook_new ();
    gtk_container_set_border_width (GTK_CONTAINER (notebook), 6);
    gtk_box_pack_start (GTK_BOX (content), notebook, TRUE, TRUE, 0);

    /* ===== pestaña Botón ===== */
    page = new_page (notebook, _("Button"));

    /* perfil + botón */
    grid = make_grid ();
    row  = 0;
    /* TRANSLATORS: profile names. "Glass", "Strip" and "Minimal" are styles
     * inspired by Windows 7, 10 and 11; translate them as short style names */
    d->profile = make_combo (_("Custom"), _("Glass"), _("Strip"), _("Minimal"),
                             _("Classic icon"), NULL);
    add_row (grid, row++, _("_Profile:"), d->profile);

    d->profile_hint = gtk_label_new (NULL);
    gtk_label_set_xalign (GTK_LABEL (d->profile_hint), 0.0);
    gtk_style_context_add_class (gtk_widget_get_style_context (d->profile_hint), "dim-label");
    gtk_grid_attach (GTK_GRID (grid), d->profile_hint, 1, row++, 1, 1);

    d->panel_tone = make_combo (_("Automatic"), _("Dark"), _("Light"), NULL);
    gtk_widget_set_tooltip_text (d->panel_tone,
        _("Built-in profiles use white on dark panels and black on light panels"));
    add_row (grid, row++, _("Panel t_one:"), d->panel_tone);

    d->tone_hint = gtk_label_new (NULL);
    gtk_label_set_xalign (GTK_LABEL (d->tone_hint), 0.0);
    gtk_style_context_add_class (gtk_widget_get_style_context (d->tone_hint), "dim-label");
    gtk_grid_attach (GTK_GRID (grid), d->tone_hint, 1, row++, 1, 1);

    d->thickness = gtk_spin_button_new_with_range (1, 128, 1);
    add_row (grid, row++, _("Button _thickness (px):"), d->thickness);

    d->style = make_combo (_("Line"), _("Icon"), _("None (background only)"), NULL);
    add_row (grid, row++, _("_Style:"), d->style);
    gtk_box_pack_start (GTK_BOX (page), make_frame (_("General"), grid), FALSE, FALSE, 0);

    /* icono */
    grid = make_grid ();
    row  = 0;
    d->icon_name = gtk_entry_new ();
    gtk_entry_set_placeholder_text (GTK_ENTRY (d->icon_name), "user-desktop");
    add_row (grid, row++, _("Icon _name:"), d->icon_name);

    d->icon_size = gtk_spin_button_new_with_range (8, 128, 1);
    add_row (grid, row++, _("Icon si_ze (px):"), d->icon_size);

    d->icon_follow_panel = gtk_check_button_new_with_mnemonic (_("_Follow the panel icon size"));
    gtk_grid_attach (GTK_GRID (grid), d->icon_follow_panel, 0, row++, 2, 1);

    d->icon_frame = make_frame (_("Icon"), grid);
    gtk_box_pack_start (GTK_BOX (page), d->icon_frame, FALSE, FALSE, 0);

    /* copiar de otro botón (útil con un panel por monitor) */
    grid = make_grid ();
    row  = 0;
    {
        GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);

        d->copy_combo  = gtk_combo_box_text_new ();
        d->copy_button = gtk_button_new_with_mnemonic (_("C_opy"));
        gtk_widget_set_tooltip_text (d->copy_button,
            _("Replace all settings of this button with those of the selected one"));
        gtk_widget_set_hexpand (d->copy_combo, TRUE);
        gtk_box_pack_start (GTK_BOX (box), d->copy_combo, TRUE, TRUE, 0);
        gtk_box_pack_start (GTK_BOX (box), d->copy_button, FALSE, FALSE, 0);
        add_row (grid, row++, _("_From:"), box);
        gtk_label_set_mnemonic_widget (
            GTK_LABEL (gtk_grid_get_child_at (GTK_GRID (grid), 0, row - 1)), d->copy_combo);
    }
    gtk_box_pack_start (GTK_BOX (page), make_frame (_("Copy settings from another button"), grid),
                        FALSE, FALSE, 0);

    /* ===== pestaña Línea ===== */
    page = new_page (notebook, _("Line"));

    /* línea */
    grid = make_grid ();
    row  = 0;
    d->line_width = gtk_spin_button_new_with_range (1, 64, 1);
    add_row (grid, row++, _("Line _width (px):"), d->line_width);

    {
        GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
        d->line_length = gtk_spin_button_new_with_range (1, 100, 1);
        /* TRANSLATORS: unit of the line length: percent of the panel height, or pixels */
        /* xgettext:no-c-format */
        d->line_unit   = make_combo (_("% of panel"), _("px"), NULL);
        gtk_widget_set_hexpand (d->line_length, TRUE);
        gtk_box_pack_start (GTK_BOX (box), d->line_length, TRUE, TRUE, 0);
        gtk_box_pack_start (GTK_BOX (box), d->line_unit, FALSE, FALSE, 0);
        add_row (grid, row++, _("Line _length:"), box);
        gtk_label_set_mnemonic_widget (
            GTK_LABEL (gtk_grid_get_child_at (GTK_GRID (grid), 0, row - 1)), d->line_length);
    }

    d->line_align = make_combo (_("Start (left / top)"), _("Center"), _("End (right / bottom)"), NULL);
    add_row (grid, row++, _("_Position:"), d->line_align);

    d->line_visibility = make_combo (_("Always"), _("Only on hover"), NULL);
    add_row (grid, row++, _("_Visible:"), d->line_visibility);

    d->line_color = make_color ();
    add_row (grid, row++, _("_Color:"), d->line_color);

    d->line_hover_color = make_color ();
    add_row (grid, row++, _("Color on _hover:"), d->line_hover_color);

    d->line_pressed_color = make_color ();
    add_row (grid, row++, _("Color when p_ressed:"), d->line_pressed_color);

    d->line_round = gtk_check_button_new_with_mnemonic (_("R_ounded ends"));
    gtk_grid_attach (GTK_GRID (grid), d->line_round, 0, row++, 2, 1);

    d->line_frame = make_frame (_("Line"), grid);
    gtk_box_pack_start (GTK_BOX (page), d->line_frame, FALSE, FALSE, 0);

    /* ===== pestaña Fondo ===== */
    page = new_page (notebook, _("Background"));

    /* fondos */
    grid = make_grid ();
    row  = 0;
    d->bg_color = make_color ();
    add_row (grid, row++, _("_Normal:"), d->bg_color);

    d->hover_bg_color = make_color ();
    add_row (grid, row++, _("On h_over:"), d->hover_bg_color);

    d->active_bg_color = make_color ();
    add_row (grid, row++, _("_Pressed:"), d->active_bg_color);

    d->bg_radius = gtk_spin_button_new_with_range (0, 16, 1);
    add_row (grid, row++, _("Corner _radius (px):"), d->bg_radius);

    d->bg_margin = gtk_spin_button_new_with_range (0, 8, 1);
    gtk_widget_set_tooltip_text (d->bg_margin,
        _("Empty space between the edge of the button and the colored background"));
    add_row (grid, row++, _("_Margin (px):"), d->bg_margin);

    d->glass = gtk_check_button_new_with_mnemonic (_("_Glass shine"));
    gtk_grid_attach (GTK_GRID (grid), d->glass, 0, row++, 2, 1);

    d->highlight_shown = gtk_check_button_new_with_mnemonic (
        _("_Keep the pressed color while the desktop is shown"));
    gtk_grid_attach (GTK_GRID (grid), d->highlight_shown, 0, row++, 2, 1);

    gtk_box_pack_start (GTK_BOX (page), make_frame (_("Background colors"), grid), FALSE, FALSE, 0);

    /* animación */
    grid = make_grid ();
    row  = 0;
    d->animate = gtk_check_button_new_with_mnemonic (_("Smooth _transitions"));
    gtk_widget_set_tooltip_text (d->animate,
        _("Fade colors when hovering and clicking instead of changing them instantly"));
    gtk_grid_attach (GTK_GRID (grid), d->animate, 0, row++, 2, 1);

    d->anim_speed = make_combo (_("Fast"), _("Normal"), _("Slow"), NULL);
    add_row (grid, row++, _("Spee_d:"), d->anim_speed);

    d->anim_hint = gtk_label_new (_("Animations are disabled in the system settings."));
    gtk_label_set_xalign (GTK_LABEL (d->anim_hint), 0.0);
    gtk_label_set_line_wrap (GTK_LABEL (d->anim_hint), TRUE);
    gtk_style_context_add_class (gtk_widget_get_style_context (d->anim_hint), "dim-label");
    gtk_widget_set_no_show_all (d->anim_hint, TRUE);
    gtk_grid_attach (GTK_GRID (grid), d->anim_hint, 0, row++, 2, 1);

    gtk_box_pack_start (GTK_BOX (page), make_frame (_("Animation"), grid), FALSE, FALSE, 0);

    /* ===== pestaña Comportamiento ===== */
    page = new_page (notebook, _("Behavior"));

    grid = make_grid ();
    row  = 0;
    d->middle_action = make_combo (_("Do nothing"), _("Show / restore desktop"),
                                   _("Minimize all windows"),
                                   _("Shade / unshade windows (X11 only)"), NULL);
    add_row (grid, row++, _("_Middle click:"), d->middle_action);

    d->shade_hint = gtk_label_new (_("Window shading is not supported on Wayland."));
    gtk_label_set_xalign (GTK_LABEL (d->shade_hint), 0.0);
    gtk_label_set_line_wrap (GTK_LABEL (d->shade_hint), TRUE);
    gtk_style_context_add_class (gtk_widget_get_style_context (d->shade_hint), "dim-label");
    gtk_widget_set_no_show_all (d->shade_hint, TRUE);
    gtk_grid_attach (GTK_GRID (grid), d->shade_hint, 1, row++, 1, 1);

    d->scroll_action = make_combo (_("Do nothing"), _("Switch workspace"),
                                   _("Up shows desktop, down restores"), NULL);
    add_row (grid, row++, _("Mouse _wheel:"), d->scroll_action);

    d->scroll_hint = gtk_label_new (_("Workspace switching is not available in this session."));
    gtk_label_set_xalign (GTK_LABEL (d->scroll_hint), 0.0);
    gtk_label_set_line_wrap (GTK_LABEL (d->scroll_hint), TRUE);
    gtk_style_context_add_class (gtk_widget_get_style_context (d->scroll_hint), "dim-label");
    gtk_widget_set_no_show_all (d->scroll_hint, TRUE);
    gtk_grid_attach (GTK_GRID (grid), d->scroll_hint, 1, row++, 1, 1);

    d->drag_hover = gtk_check_button_new_with_mnemonic (_("Show desktop when _dragging a file over the button"));
    gtk_grid_attach (GTK_GRID (grid), d->drag_hover, 0, row++, 2, 1);

    d->drop_to_desktop = gtk_check_button_new_with_mnemonic (
        _("Cop_y files dropped on the button to the Desktop"));
    gtk_widget_set_tooltip_text (d->drop_to_desktop,
        _("The original files are never moved or deleted, and existing files on the "
          "Desktop are never overwritten: a copy gets a new name such as \"photo (2).jpg\"."));
    gtk_grid_attach (GTK_GRID (grid), d->drop_to_desktop, 0, row++, 2, 1);

    d->show_tooltip = gtk_check_button_new_with_mnemonic (_("Show _tooltip"));
    gtk_grid_attach (GTK_GRID (grid), d->show_tooltip, 0, row++, 2, 1);

    gtk_box_pack_start (GTK_BOX (page), make_frame (_("Actions"), grid), FALSE, FALSE, 0);

    /* peek */
    grid = make_grid ();
    row  = 0;
    /* TRANSLATORS: "peek" = briefly showing the desktop while hovering, like Aero Peek */
    d->peek_on_hover = gtk_check_button_new_with_mnemonic (
        _("Show the desktop while the mouse is _over the button"));
    gtk_widget_set_tooltip_text (d->peek_on_hover,
        _("Moving the mouse away restores the windows. "
          "Click during the peek to keep the desktop visible."));
    gtk_grid_attach (GTK_GRID (grid), d->peek_on_hover, 0, row++, 2, 1);

    d->peek_delay = gtk_spin_button_new_with_range (100, 3000, 50);
    add_row (grid, row++, _("D_elay (ms):"), d->peek_delay);

    gtk_box_pack_start (GTK_BOX (page), make_frame (_("Peek"), grid), FALSE, FALSE, 0);

    {
        /* el comando queda fuera del texto traducible: debe escribirse tal cual */
        gchar *text = g_strdup_printf ("%s\n\n%s\n%s",
            _("Tip: place the button at the end of the panel and set the panel margin "
              "to 0 so you can click it by pushing the mouse into the screen corner."),
            _("Keyboard shortcut command:"),
            "xfce4-panel --plugin-event=xfce-desktop-button-plugin:show");
        GtkWidget *hint = gtk_label_new (text);
        g_free (text);
        gtk_label_set_selectable (GTK_LABEL (hint), TRUE);
        gtk_label_set_line_wrap (GTK_LABEL (hint), TRUE);
        gtk_label_set_max_width_chars (GTK_LABEL (hint), 50);
        gtk_label_set_xalign (GTK_LABEL (hint), 0.0);
        gtk_style_context_add_class (gtk_widget_get_style_context (hint), "dim-label");
        gtk_box_pack_start (GTK_BOX (page), hint, FALSE, FALSE, 0);
    }

    /* valores iniciales y señales */
    dialog_fill (d);

    g_signal_connect (d->profile, "changed", G_CALLBACK (on_profile_changed), d);
    g_signal_connect (d->panel_tone, "changed", G_CALLBACK (on_tone_changed), d);

    connect_appearance (d->thickness,        "value-changed", d);
    connect_appearance (d->style,            "changed",       d);
    connect_appearance (d->line_width,       "value-changed", d);
    connect_appearance (d->line_length,      "value-changed", d);
    connect_appearance (d->line_unit,        "changed",       d);
    connect_appearance (d->line_align,       "changed",       d);
    connect_appearance (d->line_visibility,  "changed",       d);
    connect_appearance (d->line_color,       "color-set",     d);
    connect_appearance (d->line_hover_color, "color-set",     d);
    connect_appearance (d->line_pressed_color, "color-set",   d);
    connect_appearance (d->line_round,       "toggled",       d);
    connect_appearance (d->bg_radius,        "value-changed", d);
    connect_appearance (d->bg_margin,        "value-changed", d);
    connect_appearance (d->icon_name,        "changed",       d);
    connect_appearance (d->icon_size,        "value-changed", d);
    connect_appearance (d->icon_follow_panel, "toggled",      d);
    connect_appearance (d->bg_color,         "color-set",     d);
    connect_appearance (d->hover_bg_color,   "color-set",     d);
    connect_appearance (d->active_bg_color,  "color-set",     d);
    connect_appearance (d->glass,            "toggled",       d);

    connect_behavior (d->middle_action, "changed", d);
    connect_behavior (d->scroll_action, "changed", d);
    connect_behavior (d->drag_hover,    "toggled", d);
    connect_behavior (d->drop_to_desktop, "toggled", d);
    connect_behavior (d->show_tooltip,  "toggled", d);
    connect_behavior (d->highlight_shown, "toggled", d);
    connect_behavior (d->peek_on_hover, "toggled",       d);
    connect_behavior (d->peek_delay,    "value-changed", d);
    connect_behavior (d->animate,       "toggled",       d);
    connect_behavior (d->anim_speed,    "changed",       d);

    g_signal_connect (dialog, "response", G_CALLBACK (on_response), d);

    fill_copy_combo (d);
    g_signal_connect (d->copy_button, "clicked", G_CALLBACK (on_copy_clicked), d);

    if (s_label_group != NULL)
    {
        g_object_unref (s_label_group);
        s_label_group = NULL;
    }

    gtk_widget_show_all (dialog);
    update_anim_hint (d);
}
