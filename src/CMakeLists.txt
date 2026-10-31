cmake_minimum_required(VERSION 3.16)
project(xfce-desktop-button-plugin VERSION 0.3.0 LANGUAGES C)

# Por defecto instalar en /usr (donde el panel busca los plugins)
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Install prefix" FORCE)
endif()

find_package(PkgConfig REQUIRED)

pkg_check_modules(GTK3       REQUIRED gtk+-3.0>=3.24)
pkg_check_modules(XFCE4PANEL REQUIRED libxfce4panel-2.0>=4.18)
pkg_check_modules(XFCE4UTIL  REQUIRED libxfce4util-1.0>=4.18)
pkg_check_modules(XFCONF     REQUIRED libxfconf-0>=4.18)

# Motor de ventanas:
#   Xfce 4.20 -> libxfce4windowing (X11 y Wayland, igual que el plugin oficial)
#   Xfce 4.18 -> libwnck (solo X11)
# -DFORCE_WNCK=ON obliga a usar libwnck aunque exista libxfce4windowing.
option(FORCE_WNCK "Usar libwnck aunque libxfce4windowing esté disponible" OFF)

if(NOT FORCE_WNCK)
    pkg_check_modules(WM QUIET libxfce4windowing-0>=4.19.2)
endif()

if(WM_FOUND)
    set(HAVE_XFW ON)
    message(STATUS "Motor de ventanas: libxfce4windowing ${WM_VERSION} (X11 + Wayland)")
else()
    pkg_check_modules(WM REQUIRED libwnck-3.0>=3.0)
    set(HAVE_XFW OFF)
    message(STATUS "Motor de ventanas: libwnck ${WM_VERSION} (solo X11)")
endif()

# libdir correcto en cada distro:
#   Arch/CachyOS -> /usr/lib
#   Debian/Loc OS -> /usr/lib/x86_64-linux-gnu
pkg_get_variable(PANEL_LIBDIR libxfce4panel-2.0 libdir)
set(XFCE_PLUGIN_DIR "${PANEL_LIBDIR}/xfce4/panel/plugins")

add_library(xfce-desktop-button-plugin MODULE
    plugin.c
    config.c
    dialog.c
    wm.c
    tone.c
    drop.c
)

target_include_directories(xfce-desktop-button-plugin PRIVATE
    ${GTK3_INCLUDE_DIRS}
    ${XFCE4PANEL_INCLUDE_DIRS}
    ${XFCE4UTIL_INCLUDE_DIRS}
    ${XFCONF_INCLUDE_DIRS}
    ${WM_INCLUDE_DIRS}
)

target_compile_definitions(xfce-desktop-button-plugin PRIVATE
    GETTEXT_PACKAGE="xfce-desktop-button-plugin"
    LOCALEDIR="${CMAKE_INSTALL_PREFIX}/share/locale"
    PLUGIN_VERSION="${PROJECT_VERSION}"
    $<$<BOOL:${HAVE_XFW}>:HAVE_XFW>
)

target_compile_options(xfce-desktop-button-plugin PRIVATE
    ${GTK3_CFLAGS_OTHER}
    ${XFCE4PANEL_CFLAGS_OTHER}
    ${XFCE4UTIL_CFLAGS_OTHER}
    ${XFCONF_CFLAGS_OTHER}
    ${WM_CFLAGS_OTHER}
    -Wall -Wextra
)

target_link_libraries(xfce-desktop-button-plugin PRIVATE
    ${GTK3_LIBRARIES}
    ${XFCE4PANEL_LIBRARIES}
    ${XFCE4UTIL_LIBRARIES}
    ${XFCONF_LIBRARIES}
    ${WM_LIBRARIES}
    m
)

install(TARGETS xfce-desktop-button-plugin
    LIBRARY DESTINATION "${XFCE_PLUGIN_DIR}"
)

# .desktop con el formato que el panel acepta (el mismo de Net Plugin)
configure_file(
    "${CMAKE_SOURCE_DIR}/xfce-desktop-button-plugin.desktop.in"
    "${CMAKE_BINARY_DIR}/xfce-desktop-button-plugin.desktop"
    @ONLY
)

install(FILES "${CMAKE_BINARY_DIR}/xfce-desktop-button-plugin.desktop"
    DESTINATION "${CMAKE_INSTALL_PREFIX}/share/xfce4/panel/plugins"
)
