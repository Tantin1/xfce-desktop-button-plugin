#!/usr/bin/env bash
#
# check.sh — pruebas automáticas mínimas del plugin
#
# Uso (desde la carpeta src, funciona igual en fish):
#     bash check.sh
#
# Qué comprueba, sin instalar nada ni tocar tu panel:
#   1. Que compile con cada motor de ventanas disponible, sin avisos.
#   2. Que la biblioteca exporte la función que el panel necesita para cargarla.
#   3. Que el archivo .desktop generado sea válido para el panel.
#   4. Que copiar al Escritorio nunca sobrescriba ni duplique (en carpetas temporales).
#   5. (Opcional) Que un panel de prueba, en una pantalla virtual aparte,
#      cargue el plugin INSTALADO sin errores. El panel solo busca plugins
#      en la carpeta del sistema, así que para probar la versión nueva
#      corré este script después de instalarla. Se omite si falta Xvfb.
#
# Todo se hace en una carpeta temporal que se borra al terminar.

set -u

SRC=$(cd "$(dirname "$0")" && pwd)
TMP=$(mktemp -d)
PIDS=()
cleanup ()
{
    for p in "${PIDS[@]}"; do kill "$p" 2>/dev/null; done
    rm -rf "$TMP"
}
trap cleanup EXIT

OK=0; FAIL=0; SKIP=0
pass () { printf '  \e[32mOK\e[0m     %s\n' "$1"; OK=$((OK + 1)); }
bad  () { printf '  \e[31mFALLA\e[0m  %s\n' "$1"; FAIL=$((FAIL + 1)); }
skip () { printf '  --     %s\n' "$1"; SKIP=$((SKIP + 1)); }

MODULE=xfce-desktop-button-plugin
FIRST_BUILD=""

# ------------------------------------------------------------------
echo "1. Compilación (los avisos cuentan como error)"

build ()   # $1 = nombre, $2.. = opciones de cmake
{
    local name=$1; shift
    local dir="$TMP/build-$name"

    if cmake -S "$SRC" -B "$dir" -DCMAKE_C_FLAGS=-Werror "$@" >"$dir.log" 2>&1 \
       && cmake --build "$dir" >>"$dir.log" 2>&1; then
        pass "$(grep -o 'Motor de ventanas: .*' "$dir.log" | head -1)"
        [ -z "$FIRST_BUILD" ] && FIRST_BUILD=$dir
    else
        bad "compilar ($name) — últimas líneas del registro:"
        tail -15 "$dir.log" | sed 's/^/           /'
    fi
}

if pkg-config --exists 'libxfce4windowing-0 >= 4.19.2'; then
    build xfw
else
    skip "libxfce4windowing no instalado (normal en Xfce 4.18)"
fi

if pkg-config --exists libwnck-3.0; then
    build wnck -DFORCE_WNCK=ON
else
    skip "libwnck no instalado"
fi

if [ -z "$FIRST_BUILD" ]; then
    echo
    echo "No se pudo compilar: el resto de las pruebas no tiene sentido."
    exit 1
fi

SO="$FIRST_BUILD/lib$MODULE.so"

# ------------------------------------------------------------------
echo "2. Biblioteca"

if nm -D --defined-only "$SO" 2>/dev/null | grep -q ' xfce_panel_module_construct$'; then
    pass "exporta xfce_panel_module_construct"
else
    bad "no exporta xfce_panel_module_construct: el panel no podría cargarla"
fi

# ------------------------------------------------------------------
echo "3. Archivo .desktop"

DESK="$FIRST_BUILD/$MODULE.desktop"
for key in "Type=X-XFCE-PanelPlugin" "X-XFCE-Module=$MODULE" "X-XFCE-API=2.0"; do
    if grep -qx "$key" "$DESK" 2>/dev/null; then pass "$key"; else bad "falta $key"; fi
done
MODPATH=$(sed -n 's/^X-XFCE-Module-Path=//p' "$DESK")
if [ -d "$MODPATH" ]; then
    pass "Module-Path existe ($MODPATH)"
else
    bad "Module-Path no existe: $MODPATH"
fi

# ------------------------------------------------------------------
echo "4. Copiar al Escritorio (nunca sobrescribir)"

cat > "$TMP/test_drop.c" <<'EOF'
#include <stdio.h>
#include <string.h>
#include "plugin.h"

static int fails = 0;

static void
expect_name (const char *base, gboolean dir, guint n, const char *want)
{
    gchar *got = db_drop_unique_name (base, dir, n);
    if (strcmp (got, want) != 0)
    {
        printf ("nombre: \"%s\" n=%u -> \"%s\" (se esperaba \"%s\")\n", base, n, got, want);
        fails++;
    }
    g_free (got);
}

static gchar *
read_file (const char *dir, const char *name)
{
    gchar *path = g_build_filename (dir, name, NULL), *text = NULL;
    g_file_get_contents (path, &text, NULL, NULL);
    g_free (path);
    return text;
}

static void
expect_content (const char *dir, const char *name, const char *want)
{
    gchar *got = read_file (dir, name);
    if (g_strcmp0 (got, want) != 0)
    {
        printf ("contenido de %s: \"%s\" (se esperaba \"%s\")\n", name, got ? got : "(no existe)", want);
        fails++;
    }
    g_free (got);
}

static void
write_file (const char *dir, const char *name, const char *text)
{
    gchar *path = g_build_filename (dir, name, NULL);
    g_file_set_contents (path, text, -1, NULL);
    g_free (path);
}

static guint
count_entries (const char *dir)
{
    GDir *d = g_dir_open (dir, 0, NULL);
    guint n = 0;
    while (d && g_dir_read_name (d)) n++;
    if (d) g_dir_close (d);
    return n;
}

static void
copy (const char *src_dir, const char *name, GFile *desktop)
{
    gchar *path = g_build_filename (src_dir, name, NULL);
    GFile *src  = g_file_new_for_path (path);
    db_drop_copy_file (src, desktop);
    g_object_unref (src);
    g_free (path);
}

int
main (int argc, char **argv)
{
    const char *root = argv[1];
    gchar *desk = g_build_filename (root, "Escritorio", NULL);
    gchar *orig = g_build_filename (root, "origen", NULL);
    gchar *sub  = g_build_filename (orig, "carpeta", NULL);
    GFile *desktop;

    (void) argc;

    /* nombres */
    expect_name ("foto.jpg",     FALSE, 1, "foto.jpg");
    expect_name ("foto.jpg",     FALSE, 2, "foto (2).jpg");
    expect_name ("README",       FALSE, 3, "README (3)");
    expect_name (".bashrc",      FALSE, 2, ".bashrc (2)");
    expect_name ("copia.tar.gz", FALSE, 2, "copia (2).tar.gz");
    expect_name ("mis.fotos",    TRUE,  2, "mis.fotos (2)");
    expect_name ("raro.",        FALSE, 2, "raro. (2)");

    /* copia real en carpetas temporales */
    g_mkdir_with_parents (desk, 0700);
    g_mkdir_with_parents (sub, 0700);
    write_file (desk, "nota.txt", "ORIGINAL DEL ESCRITORIO");
    write_file (orig, "nota.txt", "nueva");
    write_file (sub,  "dentro.txt", "hola");
    desktop = g_file_new_for_path (desk);

    copy (orig, "nota.txt", desktop);
    copy (orig, "nota.txt", desktop);
    expect_content (desk, "nota.txt",     "ORIGINAL DEL ESCRITORIO");   /* no se pisó */
    expect_content (desk, "nota (2).txt", "nueva");
    expect_content (desk, "nota (3).txt", "nueva");
    expect_content (orig, "nota.txt",     "nueva");                     /* origen intacto */

    copy (orig, "carpeta", desktop);
    expect_content (desk, "carpeta/dentro.txt", "hola");                /* carpetas completas */

    /* lo que ya está en el Escritorio no se duplica */
    {
        guint before = count_entries (desk);
        copy (desk, "nota.txt", desktop);
        if (count_entries (desk) != before) { printf ("se duplicó un archivo del Escritorio\n"); fails++; }
    }

    /* una carpeta que contiene al Escritorio no se copia (sería infinito) */
    {
        guint  before = count_entries (desk);
        GFile *parent = g_file_new_for_path (root);
        db_drop_copy_file (parent, desktop);
        g_object_unref (parent);
        if (count_entries (desk) != before) { printf ("se copió una carpeta dentro de sí misma\n"); fails++; }
    }

    g_object_unref (desktop);
    return fails == 0 ? 0 : 1;
}
EOF

CFLAGS_T=$(pkg-config --cflags gtk+-3.0 libxfce4panel-2.0)
LIBS_T=$(pkg-config --libs gtk+-3.0 libxfce4panel-2.0)
if gcc -o "$TMP/test_drop" "$TMP/test_drop.c" "$SRC/drop.c" -I"$SRC" $CFLAGS_T $LIBS_T \
       -DGETTEXT_PACKAGE='"x"' >"$TMP/test_drop.log" 2>&1; then
    mkdir -p "$TMP/drop"
    if "$TMP/test_drop" "$TMP/drop" >"$TMP/test_drop.out" 2>&1; then
        pass "nombres nuevos, sin sobrescribir, origen intacto, carpetas, sin bucles"
    else
        bad "copia al Escritorio:"
        sed 's/^/           /' "$TMP/test_drop.out"
    fi
else
    bad "no se pudo compilar la prueba de copia"
    tail -10 "$TMP/test_drop.log" | sed 's/^/           /'
fi

# ------------------------------------------------------------------
echo "5. Carga del plugin instalado en un panel de prueba (pantalla virtual)"

INSTALLED_DESK=$(ls /usr/share/xfce4/panel/plugins/$MODULE.desktop \
                    /usr/local/share/xfce4/panel/plugins/$MODULE.desktop 2>/dev/null | head -1)
missing=""
for tool in Xvfb dbus-launch xfce4-panel xfconf-query; do
    command -v "$tool" >/dev/null 2>&1 || missing="$missing $tool"
done

if [ -n "$missing" ]; then
    skip "falta:$missing (opcional; en Arch: sudo pacman -S xorg-server-xvfb)"
elif [ -z "$INSTALLED_DESK" ]; then
    skip "el plugin no está instalado todavía"
else
    # todo aislado: otra pantalla, otro bus de sesión y otra carpeta de
    # configuración. Tu panel y tu configuración real no se tocan.
    export XDG_CONFIG_HOME="$TMP/config" XDG_CACHE_HOME="$TMP/cache"
    mkdir -p "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME"

    DISP=:$((90 + RANDOM % 9))
    Xvfb "$DISP" -screen 0 800x200x24 >/dev/null 2>&1 & PIDS+=($!)
    sleep 1
    export DISPLAY=$DISP
    unset WAYLAND_DISPLAY
    eval "$(dbus-launch --sh-syntax)"; PIDS+=("$DBUS_SESSION_BUS_PID")

    q () { xfconf-query -c xfce4-panel "$@"; }
    q -p /configver -n -t int -s 2
    q -p /panels -n -a -t int -s 1
    q -p /panels/panel-1/position -n -t string -s "p=8;x=0;y=0"
    q -p /panels/panel-1/size -n -t uint -s 32
    q -p /panels/panel-1/plugin-ids -n -a -t int -s 1
    q -p /plugins/plugin-1 -n -t string -s "$MODULE"

    # sin gestor de ventanas en la pantalla virtual: no esperar por uno
    xfce4-panel --disable-wm-check >"$TMP/panel.log" 2>&1 & PIDS+=($!)
    sleep 4

    if grep -qiE "$MODULE.*(exited|removing|failed)|failed to load" "$TMP/panel.log"; then
        bad "el panel rechazó el plugin:"
        grep -iE "exited|removing|fail" "$TMP/panel.log" | sed 's/^/           /'
    elif pgrep -f "wrapper-2.0.*lib$MODULE.so 1 " >/dev/null; then
        pass "el panel cargó el plugin y sigue en marcha"
    else
        bad "no aparece el proceso del plugin (ver $TMP/panel.log)"
    fi
    xfce4-panel --quit >/dev/null 2>&1
fi

# ------------------------------------------------------------------
echo
if [ "$FAIL" -eq 0 ]; then
    printf '\e[32mTodo bien\e[0m: %d pruebas correctas, %d omitidas.\n' "$OK" "$SKIP"
    exit 0
else
    printf '\e[31m%d con fallas\e[0m, %d correctas, %d omitidas.\n' "$FAIL" "$OK" "$SKIP"
    exit 1
fi
