project(
  'xfce-desktop-button-plugin',
  'c',
  version: '0.4.0',
  license: 'GPL-2.0-or-later',
  meson_version: '>= 0.60.0',
  default_options: [
    'c_std=gnu11',
    'warning_level=2',        # equivale a -Wall -Wextra
    'buildtype=debugoptimized',
    'prefix=/usr',            # el panel busca los plugins en /usr
  ],
)

gettext_package = meson.project_name()
i18n = import('i18n')
cc = meson.get_compiler('c')

# ---------- dependencias ----------

gtk_dep    = dependency('gtk+-3.0', version: '>= 3.24')
util_dep   = dependency('libxfce4util-1.0', version: '>= 4.18')
xfconf_dep = dependency('libxfconf-0', version: '>= 4.18')
m_dep      = cc.find_library('m', required: false)

# En Xfce 4.20, libxfce4panel-2.0.pc pide libxfce4ui-2 (Requires.private),
# pero Debian 13 no instala su paquete de desarrollo junto con el del panel.
# Sin él, pkg-config no da las rutas del panel y el error es confuso.
panel_dep = dependency('libxfce4panel-2.0', version: '>= 4.18', required: false)
if not panel_dep.found()
  error('''No se pudo usar libxfce4panel-2.0.
  Instalá su paquete de desarrollo y el de libxfce4ui:
    Debian / Ubuntu:  sudo apt install libxfce4panel-2.0-dev libxfce4ui-2-dev
    Arch / CachyOS:   sudo pacman -S xfce4-panel libxfce4ui
  Para ver el motivo exacto:  pkg-config --cflags libxfce4panel-2.0''')
endif

# Motor de ventanas:
#   Xfce 4.20 -> libxfce4windowing (X11 y Wayland, igual que el plugin oficial)
#   Xfce 4.18 -> libwnck (solo X11)
# Con -Dwindowing=wnck se fuerza libwnck aunque exista libxfce4windowing.
windowing = get_option('windowing')
xfw_dep = dependency('libxfce4windowing-0', version: '>= 4.19.2',
                     required: windowing == 'xfw',
                     disabler: false)
if windowing != 'wnck' and xfw_dep.found()
  wm_dep = xfw_dep
  wm_c_args = ['-DHAVE_XFW']
  wm_summary = 'libxfce4windowing @0@ (X11 + Wayland)'.format(xfw_dep.version())
else
  wm_dep = dependency('libwnck-3.0', version: '>= 3.0')
  wm_c_args = []
  wm_summary = 'libwnck @0@ (solo X11)'.format(wm_dep.version())
endif

# ---------- rutas ----------

# carpeta de plugins del panel instalado:
#   Arch / CachyOS  -> /usr/lib/xfce4/panel/plugins
#   Debian / Ubuntu -> /usr/lib/x86_64-linux-gnu/xfce4/panel/plugins
plugin_dir = panel_dep.get_variable(pkgconfig: 'libdir') / 'xfce4' / 'panel' / 'plugins'
desktop_dir = get_option('prefix') / get_option('datadir') / 'xfce4' / 'panel' / 'plugins'
locale_dir = get_option('prefix') / get_option('localedir')

# ---------- plugin ----------

shared_module(
  'xfce-desktop-button-plugin',
  'src/plugin.c',
  'src/config.c',
  'src/dialog.c',
  'src/wm.c',
  'src/tone.c',
  'src/drop.c',
  c_args: wm_c_args + [
    '-DGETTEXT_PACKAGE="@0@"'.format(gettext_package),
    '-DLOCALEDIR="@0@"'.format(locale_dir),
    '-DPLUGIN_VERSION="@0@"'.format(meson.project_version()),
  ],
  dependencies: [gtk_dep, panel_dep, util_dep, xfconf_dep, wm_dep, m_dep],
  gnu_symbol_visibility: 'default',
  install: true,
  install_dir: plugin_dir,
)

# .desktop: primero se completa la ruta del módulo, después se le agregan
# las traducciones de Name y Comment desde po/
desktop_in = configure_file(
  input: 'src/xfce-desktop-button-plugin.desktop.in',
  output: 'xfce-desktop-button-plugin.desktop.in',
  configuration: {'XFCE_PLUGIN_DIR': plugin_dir},
)

i18n.merge_file(
  input: desktop_in,
  output: 'xfce-desktop-button-plugin.desktop',
  type: 'desktop',
  po_dir: 'po',
  install: true,
  install_dir: desktop_dir,
)

subdir('po')

summary({
  'Motor de ventanas': wm_summary,
  'Plugin': plugin_dir,
  '.desktop': desktop_dir,
  'Traducciones': locale_dir,
}, section: 'xfce-desktop-button-plugin ' + meson.project_version())
