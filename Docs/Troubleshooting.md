# Troubleshooting

**`ERROR: File assets/fonts/inter/docs/font-files/InterVariable.ttf does not exist.`**
The Inter submodule wasn't checked out. In the repo root run:
`git submodule update --init --recursive`, then `sudo just install` again.

**`meson: command not found` or a very old Meson**
Your distro packages lag. Install a newer one without root:
`pipx install meson` (log out and back in), then rerun `sudo just install`.

**Build fails naming a `-dev` / `-devel` package**
Install whatever Meson names in the error, then rerun `sudo just install`.
It resumes — no need to start over.

**Shell won't launch / nothing appears when you run `EventHorizon`**
The shell must run *inside* a Wayland compositor session with the compositor's
env vars set (`HYPRLAND_INSTANCE_SIGNATURE`, `NIRI_SOCKET`, `MANGO_SOCKET`, …).
Running it from a plain TTY (no compositor) won't work. Compositor detection
details: [Compositors.md](Compositors.md).