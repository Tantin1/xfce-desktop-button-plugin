# Traducciones. Para actualizar la plantilla y los .po después de cambiar textos:
#   meson compile -C build xfce-desktop-button-plugin-pot
#   meson compile -C build xfce-desktop-button-plugin-update-po
i18n.gettext(
  gettext_package,
  preset: 'glib',
  args: ['--from-code=UTF-8', '--add-comments=TRANSLATORS:',
         '--package-name=' + meson.project_name(),
         '--package-version=' + meson.project_version(),
         '--msgid-bugs-address=https://github.com/Tantin1/xfce-desktop-button-plugin/issues'],
)
