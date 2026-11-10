# COSMIC App Switcher

A visual application switcher for COSMIC Desktop with GNOME-style Alt+Tab
interaction: one entry per application, navigation while holding Alt, and
confirmation when Alt is released.

![Application switcher preview](docs/preview.png)

## Shortcuts

| Shortcut | Action |
| --- | --- |
| Alt+Tab | Open the panel and select the next application |
| Alt+Shift+Tab | Select the previous application |
| Release Alt | Activate the selected application |
| Esc | Cancel without switching applications |
| Enter | Confirm the selection |
| Left/right arrows | Navigate the panel |
| Move the pointer over an icon | Highlight the application without activating it |
| Left-click an icon | Activate the selected application |
| Mouse wheel | Cycle through applications |
| Alt+grave | Cycle through windows of the current application |
| Alt+Shift+grave | Cycle through windows of the current application in reverse |

Multiple windows of the same application appear as a single entry.
The panel displays pages of up to seven icons. Positions remain stable while
you select an application with the mouse. Hover over an icon and release Alt,
or confirm with a click. A stationary pointer does not change the selection
when the panel opens. Minimized windows are restored when selected.

The panel orders **most recently used applications** first: the current
application comes first, and a quick Alt+Tab returns to the previous one.
Repeating the shortcut switches between the last two applications. The order
remains fixed while the panel is open, so icons do not move as you navigate.

## Requirements

- COSMIC Desktop on Wayland; developed and used on Pop!_OS 24.04.
- COSMIC toplevel information/management protocols and layer-shell.
- A C compiler, Make, pkg-config, and Wayland development files.
- Python 3, PyGObject with GTK 3, and Pycairo.

On Pop!_OS/Ubuntu, install the dependencies:

```bash
sudo apt install build-essential pkg-config libwayland-dev libwayland-bin \
  python3 python3-gi python3-cairo gir1.2-gtk-3.0
```

## Installation

```bash
git clone https://github.com/lcoutinhom/cosmic-app-switcher.git
cd cosmic-app-switcher
bash ./install-visual.sh
```

Run the installer as your regular user, without sudo. It builds the program,
backs up your previous shortcuts, and configures the switcher's bindings.
COSMIC normally reloads shortcuts without requiring you to log out.

The binary retains the name `cosmic-cycle-app` for compatibility with the
upstream project. Installed files are:

| Path | Contents |
| --- | --- |
| `~/.local/bin/cosmic-cycle-app` | Binary |
| `~/.local/lib/cosmic-cycle-app/` | Renderer and optional indicator |
| `~/.config/cosmic/com.system76.CosmicSettings.Shortcuts/v1/custom` | Managed shortcut block |
| `~/.local/state/cosmic-cycle-app/` | Shortcut/binary backups and checksum |
| `~/.local/state/cosmic-app-switcher/` | Visual helper backups |
| `~/.config/autostart/cosmic-app-switcher-focus.desktop` | Session startup entry for the focus tracker |

Binary, configuration, and state paths honor `XDG_BIN_HOME`,
`XDG_CONFIG_HOME`, and `XDG_STATE_HOME`. Python helpers use
`~/.local/lib/cosmic-cycle-app/`. Installation preserves unrelated shortcuts.
For US international layouts, the installer adapts the grave-key bindings to
`dead_grave`/`dead_tilde`.

A lightweight monitor records actual focus changes, including mouse clicks
and Alt+grave. It starts at login or on the first Alt+Tab invocation, without
root privileges or a system service. History contains only application IDs
for the current session, stored under `$XDG_RUNTIME_DIR`; it does not record
window titles or keystrokes. Notification events without activation do not
change the order. History from before the first run cannot be reconstructed;
applications not yet observed are placed at the end of the list. The monitor
exits when the session ends, and history is discarded with the runtime
directory.

## Uninstallation

```bash
bash ./uninstall-visual.sh
```

This removes managed shortcuts and restores files that existed before
installation, preserving unrelated shortcuts added afterward. If you modify
an installed file, the uninstaller refuses to overwrite it silently.

To restore the exact previous snapshot, discarding later changes to the
shortcut file:

```bash
bash ./uninstall-visual.sh --restore-backup
```

## Other modes

```bash
# Switch immediately between applications without the panel.
cosmic-cycle-app --apps
cosmic-cycle-app --apps --reverse

# Cycle through windows of the same application.
cosmic-cycle-app
cosmic-cycle-app --reverse

# Inspect windows or the next selection without activating it.
cosmic-cycle-app --list
cosmic-cycle-app --apps --dry-run
```

To configure Alt+Tab for immediate switching:

```bash
python3 scripts/install_visual_switcher.py --rollback
```

To restore the visual panel, run `bash ./install-visual.sh`.

## Performance and limitations

- The panel reuses images for up to five minutes when the application list
  and order remain unchanged, avoiding Python/GTK startup on each invocation.
  The two most recent orders are cached for quick switching between apps.
- The first invocation and changes to the application list may take longer.
- The panel uses recent focus history. The legacy `--apps` mode, without the
  panel, retains the original order provided by the compositor.
- Grouping depends on `app_id`: applications sharing an identifier are
  grouped together.
- Keyboard navigation was developed with the US international layout.
  Other layouts or key remappings may require adjustments.
- The panel supports up to 128 applications and cancels after 15 seconds.
- It does not include GNOME's animations, window previews, or all of its
  desktop integrations.

## Development and testing

```bash
make test
```

Tests cover application selection, forward/reverse navigation, panel keyboard
and mouse input, focus history, caching, shared-memory compatibility,
installation, reinstallation, restoration, and protection against removing
modified files.

A miniature Wayland compositor also checks the panel protocol on hosts that
allow socket credential queries. That test reports `SKIP` in restricted
environments; other tests continue to run. Integration with the actual COSMIC
desktop must be verified in a graphical session.

To generate a static preview:

```bash
python3 scripts/render_switcher.py --preview /tmp/switcher 496 204 \
  firefox com.system76.CosmicTerm com.system76.CosmicFiles code
```

The image is saved to `/tmp/switcher.png`.

## Credits and license

Based on [muharamdani's cosmic-cycle-app](https://github.com/muharamdani/cosmic-cycle-app),
under the MIT License. This version adds application switching, a visual
panel, temporary keyboard capture, focus history, and caching. See
[NOTICE.md](NOTICE.md), [LICENSE](LICENSE), and
[VENDORED_SOURCES.md](VENDORED_SOURCES.md).
