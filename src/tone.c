/*
 * tone.c — ¿el panel es claro u oscuro?
 *
 * Orden de detección:
 *   1. Si el panel usa un color de fondo propio (Preferencias del panel →
 *      Apariencia → Color sólido) y es bastante opaco, se mide ese color.
 *   2. Si no, se mira el color del texto que el tema usa en el panel:
 *      texto claro = panel oscuro, y viceversa. Esto también cubre el
 *      "modo oscuro" del panel, que el panel aplica al tema del plugin.
 *
 * Se vuelve a detectar solo cuando cambia la configuración del panel
 * (xfconf) o el tema GTK.
 */

#include <string.h>

#include "plugin.h"

#include <xfconf/xfconf.h>

#define PANEL_CHANNEL "xfce4-panel"

static gdouble
luminance (const GdkRGBA *c)
{
    return 0.2126 * c->red + 0.7152 * c->green + 0.0722 * c->blue;
}

static gint
value_as_int (const GValue *v)
{
    if (v == NULL)              return -1;
    if (G_VALUE_HOLDS_INT (v))  return g_value_get_int (v);
    if (G_VALUE_HOLDS_UINT (v)) return (gint) g_value_get_uint (v);
    return -1;
}

static gdouble
value_as_double (const GValue *v, gdouble fallback)
{
    if (v == NULL)                 return fallback;
    if (G_VALUE_HOLDS_DOUBLE (v))  return g_value_get_double (v);
    if (G_VALUE_HOLDS_FLOAT (v))   return (gdouble) g_value_get_float (v);
    return fallback;
}

/* busca en qué panel está este plugin (los ids de plugin son únicos) */
static gint
find_panel_id (XfconfChannel *ch, gint plugin_id)
{
    GPtrArray *panels = xfconf_channel_get_arrayv (ch, "/panels");
    gint       found  = -1;
    guint      i, j;

    if (panels == NULL)
        return -1;

    for (i = 0; i < panels->len && found < 0; i++)
    {
        gint       pid  = value_as_int (g_ptr_array_index (panels, i));
        gchar     *prop = g_strdup_printf ("/panels/panel-%d/plugin-ids", pid);
        GPtrArray *ids  = xfconf_channel_get_arrayv (ch, prop);

        g_free (prop);
        if (ids == NULL)
            continue;

        for (j = 0; j < ids->len; j++)
            if (value_as_int (g_ptr_array_index (ids, j)) == plugin_id)
            {
                found = pid;
                break;
            }

        xfconf_array_free (ids);
    }

    xfconf_array_free (panels);
    return found;
}

/* 1 = claro, 0 = oscuro, -1 = no se pudo saber por xfconf */
static gint
detect_from_xfconf (DbPlugin *db)
{
    XfconfChannel *ch = db->tone_channel;
    gint           panel, result = -1;
    gchar         *prop;
    guint          style;

    if (ch == NULL)
        return -1;

    if (db->cached_panel_id < 0)
        db->cached_panel_id = find_panel_id (ch, xfce_panel_plugin_get_unique_id (db->plugin));
    panel = db->cached_panel_id;
    if (panel < 0)
        return -1;

    /* background-style: 0 = tema, 1 = color sólido, 2 = imagen */
    prop  = g_strdup_printf ("/panels/panel-%d/background-style", panel);
    style = xfconf_channel_get_uint (ch, prop, 0);
    g_free (prop);

    if (style == 1)
    {
        GPtrArray *rgba;

        prop = g_strdup_printf ("/panels/panel-%d/background-rgba", panel);
        rgba = xfconf_channel_get_arrayv (ch, prop);
        g_free (prop);

        if (rgba != NULL && rgba->len == 4)
        {
            GdkRGBA c;
            c.red   = value_as_double (g_ptr_array_index (rgba, 0), 0.0);
            c.green = value_as_double (g_ptr_array_index (rgba, 1), 0.0);
            c.blue  = value_as_double (g_ptr_array_index (rgba, 2), 0.0);
            c.alpha = value_as_double (g_ptr_array_index (rgba, 3), 1.0);

            /* un panel muy transparente muestra el fondo de pantalla:
             * en ese caso el color configurado no sirve para decidir */
            if (c.alpha >= 0.5)
                result = luminance (&c) > 0.5 ? 1 : 0;
        }
        if (rgba != NULL)
            xfconf_array_free (rgba);
    }

    /* el "modo oscuro" del panel no se lee acá: el panel se lo transmite
     * al proceso del plugin y ya se refleja en el color del texto del tema
     * (y si el tema no tiene variante oscura, el panel tampoco cambia) */
    return result;
}

gboolean
db_tone_detect_light (DbPlugin *db)
{
    GtkStyleContext *ctx;
    GdkRGBA          fg;
    gint             r = detect_from_xfconf (db);

    if (r >= 0)
        return r == 1;

    /* color del texto del tema en el panel */
    ctx = gtk_widget_get_style_context (GTK_WIDGET (db->plugin));
    gtk_style_context_get_color (ctx, gtk_style_context_get_state (ctx), &fg);
    return luminance (&fg) < 0.5;
}

/* número de panel en el que está un plugin (-1 si no se encuentra) */
gint
db_tone_panel_of_plugin (DbPlugin *db, gint plugin_id)
{
    if (db->tone_channel == NULL)
        return -1;
    return find_panel_id (db->tone_channel, plugin_id);
}

static gint
compare_ids (gconstpointer a, gconstpointer b)
{
    return GPOINTER_TO_INT (a) - GPOINTER_TO_INT (b);
}

/*
 * Otros "Desktop Button" que están en algún panel ahora mismo.
 * Se mira la lista de plugins de cada panel (y no los archivos .rc),
 * porque un botón quitado del panel puede dejar su archivo viejo.
 */
GList *
db_tone_list_siblings (DbPlugin *db)
{
    XfconfChannel *ch = db->tone_channel;
    GHashTable    *props;
    GHashTableIter iter;
    gpointer       key, value;
    GList         *ids = NULL;
    gint           self = xfce_panel_plugin_get_unique_id (db->plugin);
    const gchar   *module = xfce_panel_plugin_get_name (db->plugin);

    if (ch == NULL)
        return NULL;

    props = xfconf_channel_get_properties (ch, "/plugins");
    if (props == NULL)
        return NULL;

    g_hash_table_iter_init (&iter, props);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        const gchar *prop = key;
        gint         id;
        gchar       *end = NULL;

        /* solo "/plugins/plugin-N" (no sus subpropiedades) con nuestro módulo */
        if (!g_str_has_prefix (prop, "/plugins/plugin-")
            || !G_VALUE_HOLDS_STRING ((GValue *) value)
            || g_strcmp0 (g_value_get_string (value), module) != 0)
            continue;

        id = (gint) g_ascii_strtoll (prop + strlen ("/plugins/plugin-"), &end, 10);
        if (end == NULL || *end != '\0' || id == self)
            continue;

        if (find_panel_id (ch, id) >= 0)
            ids = g_list_insert_sorted (ids, GINT_TO_POINTER (id), compare_ids);
    }

    g_hash_table_destroy (props);
    return ids;
}

static void
on_panel_property_changed (XfconfChannel *ch, const gchar *property,
                           const GValue *value, DbPlugin *db)
{
    (void) ch; (void) value;

    if (g_str_has_suffix (property, "plugin-ids"))
        db->cached_panel_id = -1;

    if (db->cfg.panel_tone != TONE_AUTO)
        return;

    if (g_str_has_prefix (property, "/panels")
        && (g_str_has_suffix (property, "background-style")
            || g_str_has_suffix (property, "background-rgba")
            || g_str_has_suffix (property, "dark-mode")
            || g_str_has_suffix (property, "plugin-ids")))
        db_plugin_apply_tone (db);
}

void
db_tone_init (DbPlugin *db)
{
    GError *err = NULL;

    db->cached_panel_id = -1;

    if (!xfconf_init (&err))
    {
        g_warning ("desktop-button: xfconf no disponible: %s", err ? err->message : "?");
        g_clear_error (&err);
        return;
    }

    db->tone_channel = xfconf_channel_get (PANEL_CHANNEL);
    db->tone_handler = g_signal_connect (db->tone_channel, "property-changed",
                                         G_CALLBACK (on_panel_property_changed), db);
}

void
db_tone_free (DbPlugin *db)
{
    if (db->tone_channel == NULL)
        return;

    if (db->tone_handler != 0)
        g_signal_handler_disconnect (db->tone_channel, db->tone_handler);

    /* el canal pertenece a xfconf: no se libera, solo se cierra la conexión */
    db->tone_channel = NULL;
    xfconf_shutdown ();
}
