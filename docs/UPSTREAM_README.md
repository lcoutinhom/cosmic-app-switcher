# Cosmic Cycle App

A small Wayland-native shortcut helper that runs without root privileges and
cycles through windows of the same application on COSMIC Desktop:

- <kbd>Alt</kbd>+<kbd>`</kbd> → next window of the active application.
- <kbd>Alt</kbd>+<kbd>Shift</kbd>+<kbd>`</kbd> → previous window of the active
  application.

The project targets COSMIC 1.0 on Pop!_OS 24.04. The helper reads each window's
`app_id` and activation state through COSMIC's Wayland protocols, then
asks the compositor to activate the matching window. It does not use
`xdotool`, X11 automation, a daemon, a systemd service, or privilege
escalation.

## One-step installation

From a local clone:

```bash
bash ./install.sh
```

Or directly from GitHub:

```bash
curl -fsSL https://raw.githubusercontent.com/muharamdani/cosmic-cycle-app/main/install.sh | bash
```

For safety, inspect the script before running it if you do
not yet trust the repository:

```bash
curl -fsSLO https://raw.githubusercontent.com/muharamdani/cosmic-cycle-app/main/install.sh
less install.sh
bash install.sh
```

The installer requires `cc`, `make`, `pkg-config`, `python3`,
`wayland-scanner`, and the `wayland-client` development files. On Pop!_OS
24.04, the Wayland components are provided by `libwayland-bin` and
`libwayland-dev`. The installer only checks dependencies; it never invokes a
package manager or `sudo`.

## Files changed by the installer

Every change is scoped to the currently logged-in user:

| Default path                                                       | Change                                                                             |
|--------------------------------------------------------------------|------------------------------------------------------------------------------------|
| `~/.local/bin/cosmic-cycle-app`                                    | Installs the helper binary. A pre-existing file with the same name is backed up first. |
| `~/.config/cosmic/com.system76.CosmicSettings.Shortcuts/v1/custom` | Adds two bindings within a clearly marked `cosmic-cycle-app` block.                    |
| `~/.local/state/cosmic-cycle-app/`                                 | Stores initial backups, the installed binary checksum, and rollback metadata.          |

The paths honor `XDG_BIN_HOME`, `XDG_CONFIG_HOME`, and `XDG_STATE_HOME` when
those variables are set.

Before modifying the binary or shortcut file, the installer backs up each
existing file. Each original file is backed up only **once**. Reinstallation is
idempotent: it does not duplicate the managed block or overwrite an existing
backup. Existing manual bindings that invoke `cosmic-cycle-app-windows` are
migrated to the installer-managed block.

COSMIC reloads shortcut configuration dynamically, so no restart, logout, or
reboot is required.

## Uninstall and rollback

Use the default targeted cleanup mode if the shortcut configuration might have
been edited after installation:

```bash
bash ./uninstall.sh
```

This mode removes only the marked shortcut block, restores any binary that
existed before installation, and preserves unrelated configuration changes
made afterward.

To restore the exact pre-installation state:

```bash
bash ./uninstall.sh --restore-backup
```

In exact-restore mode, the shortcut file is replaced with its initial backup.
Any manual changes made **after** installation are therefore discarded. If the
shortcut file or binary did not exist before installation, the
installer-created file is removed.

Uninstall directly from GitHub:

```bash
curl -fsSL https://raw.githubusercontent.com/muharamdani/cosmic-cycle-app/main/uninstall.sh | bash
```

To restore the backup when running the remote script:

```bash
curl -fsSL https://raw.githubusercontent.com/muharamdani/cosmic-cycle-app/main/uninstall.sh | bash -s -- --restore-backup
```

The uninstaller refuses targeted cleanup when the installed binary checksum
has changed, preventing it from silently deleting a user-provided replacement.
Use `--restore-backup` only when you intentionally want the original snapshot.

## How it works

1. The client connects to the current user's Wayland compositor socket.
2. `zcosmic_toplevel_info_v1` version 1 reports top-level windows, their
   `app_id` values, and their `activated` state.
3. The selection logic finds the next or previous window with the same
   `app_id`.
4. `zcosmic_toplevel_manager_v1.activate` asks COSMIC to focus the target
   window, restoring it from a minimized state first when necessary.

Version 1 is selected deliberately. In the tested COSMIC 1.0 build, its direct
top-level window stream provides the initial activation state synchronously,
while the split `ext_foreign_toplevel` protocol path used by newer bindings
does not always do so.

## Source and references

The project is implemented in C under `src/`. It neither copies code from nor
links against `cosmic-comp`; the compositor's source code was consulted only
to verify server-side protocol behavior.

At build time, `wayland-scanner` generates C client bindings from five
official protocol XML files stored under `vendor/`. This keeps fresh clones
self-contained and avoids downloading another source repository during
installation. Origins, revisions, and licenses are recorded in
[VENDORED_SOURCES.md](VENDORED_SOURCES.md), with further technical references
in [docs/REFERENCES.md](docs/REFERENCES.md).

## Build and test

```bash
make test
```

The test suite covers forward, reverse, and wraparound selection as well as
the complete installation lifecycle: byte-for-byte backups, idempotent
reinstallation, paths containing spaces, targeted cleanup, exact restoration,
handling of initially absent configuration, installation from a piped remote
script, and protection against partial uninstallation.

To inspect the windows visible to the helper without changing focus:

```bash
./build/cosmic-cycle-app --list
```

## Limitations

- The compositor must expose the required COSMIC top-level window information
  and management protocols.
- Cycling follows the compositor's top-level window order rather than its most
  recently used (MRU) order.
- The shortcut uses the `grave` key symbol; its physical location depends on
  the keyboard layout configured in COSMIC.

## License

The project's source code is licensed under the MIT License; see
[LICENSE](LICENSE).
Vendored protocol files retain the copyright and permission notices embedded
in their respective XML files.
