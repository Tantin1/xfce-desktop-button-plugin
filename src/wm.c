/*
 * wm.c — mostrar escritorio y manejo de ventanas
 *
 * Dos implementaciones con la misma interfaz (plugin.h):
 *   - libxfce4windowing (Xfce 4.20): X11 y Wayland, igual que el plugin
 *     "Mostrar escritorio" oficial.
 *   - libwnck (Xfce 4.18): solo X11.
 * CMake elige una al compilar (HAVE_XFW).
 */

#include "plugin.h"

#ifdef HAVE_XFW
#include <libxfce4windowing/libxfce4windowing.h>
#else
#define WNCK_I_KNOW_THIS_IS_UNSTABLE
#include <libwnck/libwnck.h>
#include <gdk/gdkx.h>
#endif

typedef struct
{
#ifdef HAVE_XFW
    XfwScreen  *screen;
#else
    WnckHandle *handle;
    WnckScreen *screen;
#endif
    gulong      showing_handler;
} DbWm;

#define WM(db) ((DbWm *) (db)->wm)

gboolean
db_wm_available (DbPlugin *db)
{
    return db->wm != NULL && WM (db)->screen != NULL;
}

gboolean
db_wm_can_shade (DbPlugin *db)
{
#ifdef HAVE_XFW
    return db_wm_available (db) && xfw_windowing_get () == XFW_WINDOWING_X11;
#else
    return db_wm_available (db);
#endif
}

#ifdef HAVE_XFW
/* =====================================================================
 *  libxfce4windowing
 * ===================================================================== */

static void
on_show_desktop_notify (XfwScreen *screen, GParamSpec *pspec, DbPlugin *db)
{
    (void) screen; (void) pspec;
    db_plugin_showing_changed (db);
}

gboolean
db_wm_init (DbPlugin *db)
{
    DbWm *wm = g_new0 (DbWm, 1);

    db->wm = wm;
    wm->screen = xfw_screen_get_default ();
    if (wm->screen == NULL)
        return FALSE;

    wm->showing_handler = g_signal_connect (wm->screen, "notify::show-desktop",
                                            G_CALLBACK (on_show_desktop_notify), db);
    return TRUE;
}

void
db_wm_free (DbPlugin *db)
{
    DbWm *wm = WM (db);

    if (wm == NULL)
        return;

    if (wm->screen != NULL)
    {
        if (wm->showing_handler != 0)
            g_signal_handler_disconnect (wm->screen, wm->showing_handler);
        g_object_unref (wm->screen);
    }

    g_free (wm);
    db->wm = NULL;
}

gboolean
db_wm_get_showing (DbPlugin *db)
{
    return db_wm_available (db) && xfw_screen_get_show_desktop (WM (db)->screen);
}

void
db_wm_set_showing (DbPlugin *db, gboolean show)
{
    if (db_wm_available (db) && xfw_screen_get_show_desktop (WM (db)->screen) != show)
        xfw_screen_set_show_desktop (WM (db)->screen, show);
}

/* escritorio virtual activo del primer grupo; en X11 hay un único grupo */
static XfwWorkspaceGroup *
first_group (DbPlugin *db)
{
    XfwWorkspaceManager *manager = xfw_screen_get_workspace_manager (WM (db)->screen);
    GList               *groups;

    if (manager == NULL)
        return NULL;

    groups = xfw_workspace_manager_list_workspace_groups (manager);
    return groups != NULL ? XFW_WORKSPACE_GROUP (groups->data) : NULL;
}

/* ventanas "normales" del escritorio virtual actual.
 * En Wayland las ventanas no informan su escritorio virtual
 * (workspace == NULL): en ese caso se incluyen todas. */
static gboolean
window_is_eligible (XfwWindow *win, XfwWorkspace *active)
{
    XfwWindowType  type = xfw_window_get_window_type (win);
    XfwWorkspace  *ws;

    if (type == XFW_WINDOW_TYPE_DESKTOP || type == XFW_WINDOW_TYPE_DOCK)
        return FALSE;
    if (xfw_window_is_skip_tasklist (win))
        return FALSE;

    ws = xfw_window_get_workspace (win);
    if (active != NULL && ws != NULL && ws != active)
        return FALSE;

    return TRUE;
}

void
db_wm_minimize_all (DbPlugin *db)
{
    XfwWorkspaceGroup *group;
    XfwWorkspace      *active = NULL;
    GList             *l;

    if (!db_wm_available (db))
        return;

    group = first_group (db);
    if (group != NULL)
        active = xfw_workspace_group_get_active_workspace (group);

    for (l = xfw_screen_get_windows (WM (db)->screen); l != NULL; l = l->next)
    {
        XfwWindow *win = XFW_WINDOW (l->data);

        if (window_is_eligible (win, active) && !xfw_window_is_minimized (win))
            xfw_window_set_minimized (win, TRUE, NULL);
    }
}

void
db_wm_shade_all (DbPlugin *db)
{
    XfwWorkspaceGroup *group;
    XfwWorkspace      *active = NULL;
    GList             *l;
    gboolean           shade = FALSE;

    if (!db_wm_available (db))
        return;

    group = first_group (db);
    if (group != NULL)
        active = xfw_workspace_group_get_active_workspace (group);

    /* si alguna está desenrollada se enrollan todas; si no, se desenrollan */
    for (l = xfw_screen_get_windows (WM (db)->screen); l != NULL; l = l->next)
    {
        XfwWindow *win = XFW_WINDOW (l->data);
        if (window_is_eligible (win, active) && !xfw_window_is_minimized (win)
            && !xfw_window_is_shaded (win))
        {
            shade = TRUE;
            break;
        }
    }

    /* en Wayland set_shaded falla en silencio: no existe el concepto */
    for (l = xfw_screen_get_windows (WM (db)->screen); l != NULL; l = l->next)
    {
        XfwWindow *win = XFW_WINDOW (l->data);
        if (window_is_eligible (win, active) && !xfw_window_is_minimized (win))
            xfw_window_set_shaded (win, shade, NULL);
    }
}

void
db_wm_switch_workspace (DbPlugin *db, gint delta, guint32 timestamp)
{
    XfwWorkspaceGroup *group;
    XfwWorkspace      *active, *target;
    GList             *list;
    gint               idx, count;

    (void) timestamp;

    if (!db_wm_available (db) || (group = first_group (db)) == NULL)
        return;

    active = xfw_workspace_group_get_active_workspace (group);
    list   = xfw_workspace_group_list_workspaces (group);
    count  = g_list_length (list);
    if (active == NULL || count == 0)
        return;

    idx = g_list_index (list, active);
    if (idx < 0)
        idx = (gint) xfw_workspace_get_number (active);
    idx    = CLAMP (idx + delta, 0, count - 1);
    target = g_list_nth_data (list, idx);

    if (target != NULL && target != active)
        xfw_workspace_activate (target, NULL);
}

gboolean
db_wm_can_switch_workspace (DbPlugin *db)
{
    XfwWorkspaceGroup *group;

    if (!db_wm_available (db) || (group = first_group (db)) == NULL)
        return FALSE;

    return xfw_workspace_group_list_workspaces (group) != NULL;
}

#else
/* =====================================================================
 *  libwnck (solo X11)
 * ===================================================================== */

static void
on_showing_desktop_changed (WnckScreen *screen, DbPlugin *db)
{
    (void) screen;
    db_plugin_showing_changed (db);
}

gboolean
db_wm_init (DbPlugin *db)
{
    DbWm       *wm = g_new0 (DbWm, 1);
    GdkDisplay *display = gdk_display_get_default ();

    db->wm = wm;

    if (display == NULL || !GDK_IS_X11_DISPLAY (display))
        return FALSE;

    wm->handle = wnck_handle_new (WNCK_CLIENT_TYPE_PAGER);
    wm->screen = wnck_handle_get_default_screen (wm->handle);
    if (wm->screen == NULL)
    {
        g_clear_object (&wm->handle);
        return FALSE;
    }

    wnck_screen_force_update (wm->screen);
    wm->showing_handler = g_signal_connect (wm->screen, "showing-desktop-changed",
                                            G_CALLBACK (on_showing_desktop_changed), db);
    return TRUE;
}

void
db_wm_free (DbPlugin *db)
{
    DbWm *wm = WM (db);

    if (wm == NULL)
        return;

    if (wm->screen != NULL && wm->showing_handler != 0)
        g_signal_handler_disconnect (wm->screen, wm->showing_handler);
    if (wm->handle != NULL)
        g_object_unref (wm->handle);

    g_free (wm);
    db->wm = NULL;
}

gboolean
db_wm_get_showing (DbPlugin *db)
{
    return db_wm_available (db) && wnck_screen_get_showing_desktop (WM (db)->screen);
}

void
db_wm_set_showing (DbPlugin *db, gboolean show)
{
    if (db_wm_available (db))
        wnck_screen_toggle_showing_desktop (WM (db)->screen, show);
}

static gboolean
window_is_eligible (WnckWindow *win, WnckWorkspace *active)
{
    WnckWindowType type = wnck_window_get_window_type (win);

    if (type == WNCK_WINDOW_DESKTOP || type == WNCK_WINDOW_DOCK)
        return FALSE;
    if (wnck_window_is_skip_tasklist (win) || wnck_window_is_minimized (win))
        return FALSE;
    /* solo las ventanas del escritorio virtual actual */
    if (active != NULL && !wnck_window_is_on_workspace (win, active))
        return FALSE;

    return TRUE;
}

void
db_wm_minimize_all (DbPlugin *db)
{
    WnckWorkspace *active;
    GList         *l;

    if (!db_wm_available (db))
        return;

    active = wnck_screen_get_active_workspace (WM (db)->screen);

    for (l = wnck_screen_get_windows (WM (db)->screen); l != NULL; l = l->next)
        if (window_is_eligible (WNCK_WINDOW (l->data), active))
            wnck_window_minimize (WNCK_WINDOW (l->data));
}

void
db_wm_shade_all (DbPlugin *db)
{
    WnckWorkspace *active;
    GList         *l;
    gboolean       shade = FALSE;

    if (!db_wm_available (db))
        return;

    active = wnck_screen_get_active_workspace (WM (db)->screen);

    for (l = wnck_screen_get_windows (WM (db)->screen); l != NULL; l = l->next)
    {
        WnckWindow *win = WNCK_WINDOW (l->data);
        if (window_is_eligible (win, active) && !wnck_window_is_shaded (win))
        {
            shade = TRUE;
            break;
        }
    }

    for (l = wnck_screen_get_windows (WM (db)->screen); l != NULL; l = l->next)
    {
        WnckWindow *win = WNCK_WINDOW (l->data);
        if (!window_is_eligible (win, active))
            continue;
        if (shade)
            wnck_window_shade (win);
        else
            wnck_window_unshade (win);
    }
}

void
db_wm_switch_workspace (DbPlugin *db, gint delta, guint32 timestamp)
{
    WnckWorkspace *active, *target;
    gint           idx, count;

    if (!db_wm_available (db))
        return;

    active = wnck_screen_get_active_workspace (WM (db)->screen);
    if (active == NULL)
        return;

    count = wnck_screen_get_workspace_count (WM (db)->screen);
    idx   = CLAMP (wnck_workspace_get_number (active) + delta, 0, count - 1);

    target = wnck_screen_get_workspace (WM (db)->screen, idx);
    if (target != NULL && target != active)
        wnck_workspace_activate (target, timestamp);
}

gboolean
db_wm_can_switch_workspace (DbPlugin *db)
{
    return db_wm_available (db) && wnck_screen_get_workspace_count (WM (db)->screen) > 1;
}

#endif
