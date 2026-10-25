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
    if (G_VALUE_HOLDS_INT (v))  return g_value_get_int (v);
    if (G_VALUE_HOLDS_UINT (v)) return (gint) g_value_get_uint (v);
    return -1;
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

    panel = find_panel_id (ch, xfce_panel_plugin_get_unique_id (db->plugin));
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
            c.red   = g_value_get_double (g_ptr_array_index (rgba, 0));
            c.green = g_value_get_double (g_ptr_array_index (rgba, 1));
            c.blue  = g_value_get_double (g_ptr_array_index (rgba, 2));
            c.alpha = g_value_get_double (g_ptr_array_index (rgba, 3));

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

static void
on_panel_property_changed (XfconfChannel *ch, const gchar *property,
                           const GValue *value, DbPlugin *db)
{
    (void) ch; (void) value;

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
