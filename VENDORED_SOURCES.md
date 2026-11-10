# Vendored protocol sources

The protocol XML files are stored in this repository so `wayland-scanner` can
generate client bindings without cloning dependencies during installation.

| Path                                                                                          | Upstream                    | Snapshot                                   |
|-----------------------------------------------------------------------------------------------|-----------------------------|--------------------------------------------|
| `vendor/cosmic-protocols/unstable/cosmic-toplevel-info-unstable-v1.xml`                       | `pop-os/cosmic-protocols`   | `e95d89504513e1407f89a189aca328fbecc9eeef` |
| `vendor/cosmic-protocols/unstable/cosmic-toplevel-management-unstable-v1.xml`                 | `pop-os/cosmic-protocols`   | `e95d89504513e1407f89a189aca328fbecc9eeef` |
| `vendor/cosmic-protocols/unstable/cosmic-workspace-unstable-v1.xml`                           | `pop-os/cosmic-protocols`   | `e95d89504513e1407f89a189aca328fbecc9eeef` |
| `vendor/wayland-protocols/staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml` | `wayland/wayland-protocols` | release `1.45`                             |
| `vendor/wayland-protocols/staging/ext-workspace/ext-workspace-v1.xml`                         | `wayland/wayland-protocols` | release `1.45`                             |

Each XML file retains its complete upstream copyright and permissive license
notice. The only local modification removes two trailing spaces from
`ext-workspace-v1.xml`; the protocol content is unchanged.

# Local visual switcher protocol additions

The visual switcher additionally vendors the following official protocol XMLs
(retrieved 2026-11-10). Their upstream copyright and permission notices are
preserved in each file:

- `vendor/wlr-layer-shell-unstable-v1.xml`:
  https://github.com/swaywm/wlr-protocols/blob/master/unstable/wlr-layer-shell-unstable-v1.xml
- `vendor/xdg-shell.xml`:
  https://github.com/wayland-mirror/wayland-protocols/blob/main/stable/xdg-shell/xdg-shell.xml
- `vendor/keyboard-shortcuts-inhibit-unstable-v1.xml`:
  https://github.com/wayland-mirror/wayland-protocols/blob/main/unstable/keyboard-shortcuts-inhibit/keyboard-shortcuts-inhibit-unstable-v1.xml
