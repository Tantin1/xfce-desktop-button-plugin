# Desktop Button for Xfce

A thin, customizable **Show Desktop** button for the Xfce panel. It can display a narrow line, an icon, or just a background, and works in horizontal, vertical, and deskbar-style panels.

## Features

- Adjustable button thickness (1–128 px) and an option to occupy only one row of a multi-row panel.
- Four built-in appearance presets: **Glass** (inspired by Windows 7), **Strip** (Windows 10), **Minimal** (Windows 11), and **Classic icon** (the Xfce-style button). A **Custom** profile provides individual controls.
- Configurable line width, length, alignment, visibility, colors, rounded ends, backgrounds, icon name, and icon size.
- Works anywhere in the panel. At either end, line styles put the line on the side facing the other items and leave an adjustable gap (as wide as a transparent separator by default) that does not react to the mouse. In the middle, the button is twice as wide, fully clickable, with the line in the center.
- Pick the icon with the same icon chooser as the Applications Menu (theme icons or your own image). In the icon style, the button widens to fit larger icons.
- Optional smooth transitions, automatic light/dark panel detection, and highlighting while the desktop is shown.
- Configurable middle-click and mouse-wheel actions; optional **peek** to show the desktop while hovering, with a delay.
- Optional drag-hover to show the desktop. Files dropped onto the button can be **copied** to the Desktop: originals are preserved, and existing names are not overwritten.
- Copy settings from another instance of the same button.
- Translations for 12 languages: German, Spanish, French, Italian, Japanese, Korean, Dutch, Polish, Brazilian Portuguese, Russian, Turkish, and Simplified Chinese.

## Compatibility

- **Xfce 4.20 with `libxfce4windowing`:** X11 and Wayland backends, as supported by the desktop session.
- **Xfce 4.18 with `libwnck`:** X11 only. Building with the `libwnck` backend does **not** provide Wayland support.
- The Meson build automatically selects `libxfce4windowing` when available (version 4.19.2 or later); otherwise it uses `libwnck`. Override the selection with `-Dwindowing=xfw` or `-Dwindowing=wnck`.

The GitHub Actions workflow builds against Debian 12, Debian 13, Ubuntu 24.04, and Arch Linux. These build jobs are not a guarantee of compatibility with every distribution or session configuration.

## Building

You need Meson (>= 0.60), Ninja, a C compiler, pkg-config, gettext, GTK 3.24+, `libxfce4panel-2.0`, `libxfce4util-1.0`, `libxfconf-0`, `exo-2` (the icon chooser; on Xfce 4.21 and later, `libxfce4ui-2` provides it instead), and **either** `libxfce4windowing-0` (>= 4.19.2) **or** `libwnck-3.0`, with the corresponding development files.

```sh
meson setup build --prefix=/usr
meson compile -C build
```

To inspect what would be installed **without** changing system files:

```sh
DESTDIR="$PWD/stage" meson install -C build
```

This creates a staging tree under `stage/`. The panel module and its `.desktop` registration must ultimately be installed in the panel's matching system directories. On Arch/CachyOS, prefer a package managed by Pacman; **do not use `sudo meson install` to overwrite files owned by Pacman**.

### Debian / Ubuntu packages

After installing the development dependencies, build a local `.deb`:

```sh
bash .github/scripts/build-deb.sh deb13
```

Use `deb12` or `ubuntu24.04` instead when building on those supported systems. The generated package appears in `dist/`; install it through your distribution's package manager. Tagged GitHub releases are configured to attach built `.deb` packages when the workflow succeeds.

### Tests

```sh
bash check.sh
```

The script checks compilation, module metadata, translations, and file-copy behavior. Its optional virtual-panel test exercises the **system-installed plugin**, not necessarily the library just compiled. It does not replace a real Xfce session test.

## Using the plugin

Add **Desktop Button** from *Panel Preferences → Items → Add*. Right-click the button for its settings. The *About* dialog links to the project repository.

## Licensing and attribution

The project declares **GPL-2.0-or-later**; see [COPYING](COPYING) for the GPL version 2 license text. The built-in presets are *inspired by* familiar desktop styles; the project is not affiliated with Microsoft. The project uses icon names from the active system icon theme; no third-party artwork is bundled in this source tree.

## Project

Maintained by **Tantin1**. Bug reports and source code: https://github.com/Tantin1/xfce-desktop-button-plugin
