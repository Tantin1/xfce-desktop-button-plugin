/*
 * drop.c — copiar al Escritorio los archivos soltados sobre el botón
 *
 * Reglas de seguridad:
 *   - Siempre se COPIA: el original nunca se mueve ni se borra.
 *   - Nunca se sobrescribe: si ya existe "foto.jpg" en el Escritorio,
 *     la copia se llama "foto (2).jpg", "foto (3).jpg", etc. La copia se
 *     hace sin el permiso de sobrescribir, así que aunque otro programa
 *     cree el archivo al mismo tiempo, el sistema rechaza pisarlo.
 *   - Lo que ya está en el Escritorio se ignora (no se duplica).
 *   - Copiar el propio Escritorio (o una carpeta que lo contiene) se
 *     ignora, porque sería una copia infinita dentro de sí misma.
 *   - La copia corre en un hilo aparte: archivos grandes no congelan el botón.
 */

#include <string.h>
#include <glib/gi18n-lib.h>
#include <gio/gio.h>

#include "plugin.h"

#define MAX_NAME_ATTEMPTS 1000

/* ---------- nombre libre: "foto.jpg" → "foto (2).jpg" ---------- */

gchar *
db_drop_unique_name (const gchar *basename, gboolean is_dir, guint n)
{
    const gchar *ext;
    gsize        stem;

    if (n <= 1)
        return g_strdup (basename);

    /* carpetas, archivos sin extensión, ocultos (".bashrc") o con punto final */
    ext = is_dir ? NULL : strrchr (basename, '.');
    if (ext == NULL || ext == basename || ext[1] == '\0')
        return g_strdup_printf ("%s (%u)", basename, n);

    /* extensiones dobles: "copia.tar.gz" → "copia (2).tar.gz" */
    stem = ext - basename;
    if (stem > 4 && g_ascii_strncasecmp (ext - 4, ".tar", 4) == 0 && ext - 4 != basename)
    {
        ext  -= 4;
        stem -= 4;
    }

    return g_strdup_printf ("%.*s (%u)%s", (int) stem, basename, n, ext);
}

/* ---------- copia (en el hilo de trabajo) ---------- */

static gboolean
copy_recursive (GFile *src, GFile *dest, GCancellable *cancel, GError **error)
{
    GFileType        type;
    GFileEnumerator *en;
    GFileInfo       *info;
    gboolean         ok = TRUE;

    type = g_file_query_file_type (src, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancel);

    if (type != G_FILE_TYPE_DIRECTORY)
        return g_file_copy (src, dest, G_FILE_COPY_NOFOLLOW_SYMLINKS,
                            cancel, NULL, NULL, error);

    /* falla con G_IO_ERROR_EXISTS si ya existe: nunca se mezcla con otra carpeta */
    if (!g_file_make_directory (dest, cancel, error))
        return FALSE;

    en = g_file_enumerate_children (src, G_FILE_ATTRIBUTE_STANDARD_NAME,
                                    G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, cancel, error);
    if (en == NULL)
        return FALSE;

    while (ok && (info = g_file_enumerator_next_file (en, cancel, error)) != NULL)
    {
        const gchar *name  = g_file_info_get_name (info);
        GFile       *child = g_file_get_child (src, name);
        GFile       *to    = g_file_get_child (dest, name);

        ok = copy_recursive (child, to, cancel, error);

        g_object_unref (to);
        g_object_unref (child);
        g_object_unref (info);
    }

    /* next_file devuelve NULL tanto al terminar como ante un error */
    if (ok && error != NULL && *error != NULL)
        ok = FALSE;

    g_file_enumerator_close (en, NULL, NULL);
    g_object_unref (en);
    return ok;
}

void
db_drop_copy_file (GFile *src, GFile *desktop)
{
    GFile    *parent;
    gchar    *base;
    gboolean  is_dir;
    guint     n;

    /* ya está en el Escritorio */
    parent = g_file_get_parent (src);
    if (parent != NULL && g_file_equal (parent, desktop))
    {
        g_object_unref (parent);
        return;
    }
    g_clear_object (&parent);

    /* el propio Escritorio o una carpeta que lo contiene */
    if (g_file_equal (src, desktop) || g_file_has_prefix (desktop, src))
    {
        g_message ("desktop-button: no se copia una carpeta dentro de sí misma");
        return;
    }

    base = g_file_get_basename (src);
    if (base == NULL)
        return;

    is_dir = g_file_query_file_type (src, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL)
             == G_FILE_TYPE_DIRECTORY;

    for (n = 1; n <= MAX_NAME_ATTEMPTS; n++)
    {
        gchar  *name  = db_drop_unique_name (base, is_dir, n);
        GFile  *dest  = g_file_get_child (desktop, name);
        GError *error = NULL;
        gboolean ok   = copy_recursive (src, dest, NULL, &error);

        g_object_unref (dest);
        g_free (name);

        if (ok)
            break;

        if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS))
        {
            g_error_free (error);
            continue;       /* ocupado: probar el siguiente nombre */
        }

        g_warning ("desktop-button: no se pudo copiar \"%s\": %s",
                   base, error != NULL ? error->message : "?");
        g_clear_error (&error);
        break;
    }

    g_free (base);
}

typedef struct
{
    GPtrArray *files;     /* GFile* */
    GFile     *desktop;
} CopyJob;

static void
copy_job_free (gpointer data)
{
    CopyJob *job = data;

    g_ptr_array_unref (job->files);
    g_object_unref (job->desktop);
    g_free (job);
}

static void
copy_thread (GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
    CopyJob *job = data;
    guint    i;

    (void) source; (void) cancel;

    for (i = 0; i < job->files->len; i++)
        db_drop_copy_file (g_ptr_array_index (job->files, i), job->desktop);

    g_task_return_boolean (task, TRUE);
}

/* ---------- señales de arrastrar y soltar ---------- */

static gboolean
on_drag_drop (GtkWidget *widget, GdkDragContext *ctx,
              gint x, gint y, guint time_, DbPlugin *db)
{
    GdkAtom target;

    (void) x; (void) y;

    if (!db->cfg.drop_to_desktop)
        return FALSE;

    target = gtk_drag_dest_find_target (widget, ctx, NULL);
    if (target == GDK_NONE)
        return FALSE;

    gtk_drag_get_data (widget, ctx, target, time_);
    return TRUE;
}

static void
on_drag_data_received (GtkWidget *widget, GdkDragContext *ctx, gint x, gint y,
                       GtkSelectionData *sel, guint info, guint time_, DbPlugin *db)
{
    const gchar *path;
    gchar      **uris;
    CopyJob     *job;
    GTask       *task;
    guint        i;

    (void) widget; (void) x; (void) y; (void) info;

    path = g_get_user_special_dir (G_USER_DIRECTORY_DESKTOP);
    uris = db->cfg.drop_to_desktop ? gtk_selection_data_get_uris (sel) : NULL;

    /* sin Escritorio configurado (o si apunta a la carpeta personal) no se copia */
    if (uris == NULL || path == NULL
        || g_strcmp0 (path, g_get_home_dir ()) == 0
        || !g_file_test (path, G_FILE_TEST_IS_DIR))
    {
        g_strfreev (uris);
        gtk_drag_finish (ctx, FALSE, FALSE, time_);
        return;
    }

    job = g_new0 (CopyJob, 1);
    job->files   = g_ptr_array_new_with_free_func (g_object_unref);
    job->desktop = g_file_new_for_path (path);
    for (i = 0; uris[i] != NULL; i++)
        g_ptr_array_add (job->files, g_file_new_for_uri (uris[i]));
    g_strfreev (uris);

    /* el hilo no toca el plugin: si se cierra el panel a mitad, no hay problema */
    task = g_task_new (NULL, NULL, NULL, NULL);
    g_task_set_task_data (task, job, copy_job_free);
    g_task_run_in_thread (task, copy_thread);
    g_object_unref (task);

    /* éxito, y "delete" = FALSE: el origen nunca se borra */
    gtk_drag_finish (ctx, TRUE, FALSE, time_);
}

void
db_drop_init (DbPlugin *db)
{
    GtkTargetList *list = gtk_target_list_new (NULL, 0);

    gtk_target_list_add_uri_targets (list, 0);
    gtk_drag_dest_set_target_list (db->area, list);
    gtk_target_list_unref (list);

    g_signal_connect (db->area, "drag-drop",          G_CALLBACK (on_drag_drop),          db);
    g_signal_connect (db->area, "drag-data-received", G_CALLBACK (on_drag_data_received), db);
}
