# adw-gtk3

An unofficial GTK3 port of [libadwaita](https://gnome.pages.gitlab.gnome.org/libadwaita/).

- Upstream: https://github.com/lassekongo83/adw-gtk3
- Author/maintainer: lassekongo83 (https://github.com/lassekongo83)
- License: GNU Lesser General Public License v2.1 — see [LICENSE](./LICENSE).

These `adw-gtk3/` and `adw-gtk3-dark/` theme directories are distributed as an
unmodified copy of the upstream project's built output. They are vendored only
so the Event Horizon build can install a matching GTK3/GTK4 system theme.

To avoid shipping this third-party theme entirely, install the distro package
instead and remove this `third_party/adw-gtk3/` directory (meson auto-skips the
theme install when it is absent):

- Fedora: `dnf install adw-gtk3-theme`
- Arch: `pacman -S adw-gtk-theme`
- Flatpak: `flatpak install org.gtk.Gtk3theme.adw-gtk3 org.gtk.Gtk3theme.adw-gtk3-dark`