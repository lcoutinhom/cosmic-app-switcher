# Local application switcher

## Visual mode

`cosmic-cycle-app --switcher` opens a centered native Wayland layer-shell
overlay with one icon per distinct nonempty application ID. Tab advances,
Shift+Tab goes backwards, releasing Alt activates the selection, and Esc
cancels. Enter confirms; arrow keys navigate. Pointer motion highlights icons;
a matching left-button press/release activates the icon. Releasing Alt also
confirms a hovered selection. Initial pointer entry does not change selection
until actual motion; the scroll wheel steps through apps. Keyboard focus and shortcut
inhibition last only while the panel is open. Repeated shortcut invocations
forward their direction to the existing panel via a private runtime socket.
A 15-second safety timeout cancels without switching.

The visible range uses stable pages of seven apps, so hover does not move
targets underneath the pointer; at most 128 apps are supported.
Minimized targets are restored and closed targets are checked before activation.
The physical keyboard handling targets the current US alt-intl layout.
App order is the compositor's first-appearance order, not MRU history.
This resembles GNOME's interaction without its window previews or animations.

Rendering uses system desktop icons via PyGObject GTK3/Gio and Python Cairo.
Pixel frames are cached in the private XDG runtime directory for up to five
minutes. Reopening with the same app order skips Python/GTK startup entirely.
The cache is regenerated when apps/order, panel dimensions, locale or renderer
metadata change, and corrupted/truncated cache files are regenerated as well.
Atomic file replacement preserves the backing data of any existing SHM buffers.
To build/install from a fresh clone: run `bash ./install-visual.sh`.
To return to immediate switching:

```sh
python3 scripts/install_visual_switcher.py --rollback
```

Base backups reside in `~/.local/state/cosmic-cycle-app/backups/`.
Helper backups and checksums reside in `~/.local/state/cosmic-app-switcher/`.
The grave-key bindings retain the US alt-intl dead_grave/dead_tilde adjustment.
The upstream installer overwrites local bindings, so rerun the local installer
after upstream reinstallation. `COSMIC_CYCLE_RENDERER` can specify an absolute
renderer path for development/tests.

To generate a static preview without opening a desktop window:

```sh
python3 scripts/render_switcher.py --preview /tmp/panel 384 204 firefox code terminal
```

This writes `/tmp/panel.png`.

## Immediate mode and cleanup

`--apps` cycles immediately between distinct apps; `--reverse` reverses direction.
`--dry-run` prints the selected target without changing focus. Without either
application flag, same-application window cycling is unchanged.
The optional `~/.local/lib/cosmic-cycle-app/show_app.py` posts a silent transient
notification in immediate mode only; the visual panel does not use it.

Remove the two entries containing `--switcher` (or `--apps`) from the custom
COSMIC shortcut map to restore native Alt+Tab. To uninstall the whole helper,
run `bash ./uninstall-visual.sh`. This removes the managed shortcuts and binary,
then restores or removes the installed Python helpers. Later changes to other
shortcuts are preserved unless exact snapshot restoration was requested.

## Validation

`make test` covers app selection, installer lifecycle and overlay keyboard
states: forward/reverse/wrap, Esc, Alt release, fast taps, Enter and focus loss.
A miniature compositor test exercises native layer-shell and keyboard behavior
on hosts permitting socket peer queries; it explicitly skips when those
queries are forbidden by the managed environment.
Pixel frame sizes and distinct selection frames were checked, and the static
preview was visually inspected. Actual COSMIC placement and keyboard capture
require verification in the real graphical session, inaccessible in the sandbox.
