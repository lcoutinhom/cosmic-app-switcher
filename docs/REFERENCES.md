# Technical references

## Protocols used

- [COSMIC toplevel info](https://github.com/pop-os/cosmic-protocols/blob/e95d89504513e1407f89a189aca328fbecc9eeef/unstable/cosmic-toplevel-info-unstable-v1.xml)
  provides toplevel enumeration, `app_id`, title, state, and lifecycle events.
- [COSMIC toplevel management](https://github.com/pop-os/cosmic-protocols/blob/e95d89504513e1407f89a189aca328fbecc9eeef/unstable/cosmic-toplevel-management-unstable-v1.xml)
  provides the `activate` and `unset_minimized` requests.
- [COSMIC workspace](https://github.com/pop-os/cosmic-protocols/blob/e95d89504513e1407f89a189aca328fbecc9eeef/unstable/cosmic-workspace-unstable-v1.xml)
  provides workspace types referenced by the toplevel information protocol.
- [ext-foreign-toplevel-list v1](https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/1.45/staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml)
  provides foreign-toplevel types used by newer protocol versions.
- [ext-workspace v1](https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/1.45/staging/ext-workspace/ext-workspace-v1.xml)
  provides standard workspace types referenced by COSMIC protocols.

## Server implementation references

The following source files are **not copied or linked**. They were consulted
to understand how the COSMIC compositor advertises globals, sends initial
state, and processes management requests:

- [`toplevel_info.rs`](https://github.com/pop-os/cosmic-comp/blob/d3ffa814941f6294864d5ecdc9796f818ddb1ac8/src/wayland/protocols/toplevel_info.rs)
- [`toplevel_management.rs`](https://github.com/pop-os/cosmic-comp/blob/d3ffa814941f6294864d5ecdc9796f818ddb1ac8/src/wayland/protocols/toplevel_management.rs)
- [Upstream feature request: cycle windows of the same application](https://github.com/pop-os/cosmic-epoch/issues/2125)

## Tested baseline

- Pop!_OS 24.04
- `cosmic-comp 1.0.0`, commit
  `091583ac84abac02967ae358cf9570ddfef63b31`
- `libwayland` 1.23.1
- `wayland-protocols` 1.45
- `cosmic-protocols` snapshot
  `e95d89504513e1407f89a189aca328fbecc9eeef`

Because the COSMIC protocols are unstable, compatibility should be retested
if the compositor removes version 1 bindings or changes protocol exposure
policy.
