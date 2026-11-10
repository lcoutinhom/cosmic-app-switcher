#!/usr/bin/python3
"""Brief native desktop notification; repeated cycling replaces the banner."""
import fcntl
import json
import os
from pathlib import Path
import sys

import gi
gi.require_version('Gio', '2.0')
from gi.repository import Gio, GLib


def main():
    app_id = sys.argv[1]
    title = sys.argv[2] if len(sys.argv) > 2 else ''
    name, icon = app_id, 'application-x-executable'
    try:
        desktop = Gio.DesktopAppInfo.new(app_id + '.desktop')
    except TypeError:
        desktop = None
    if desktop is None:
        for candidate in Gio.AppInfo.get_all():
            candidate_id = candidate.get_id() or ''
            if candidate_id.removesuffix('.desktop').casefold() == app_id.casefold():
                desktop = candidate
                break
    if desktop:
        name = desktop.get_display_name()
        if desktop.get_icon():
            icon = desktop.get_icon().to_string()
    runtime = Path(os.environ['XDG_RUNTIME_DIR']) / 'cosmic-cycle-app'
    runtime.mkdir(mode=0o700, exist_ok=True)
    with (runtime / 'notification.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        state = runtime / 'notification.json'
        bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
        owner = bus.call_sync(
            'org.freedesktop.DBus', '/org/freedesktop/DBus',
            'org.freedesktop.DBus', 'GetNameOwner',
            GLib.Variant('(s)', ('org.freedesktop.Notifications',)),
            GLib.VariantType.new('(s)'), Gio.DBusCallFlags.NONE, 1000, None
        ).unpack()[0]
        replacement = 0
        try:
            previous = json.loads(state.read_text())
            if previous['owner'] == owner:
                replacement = previous['id']
        except (OSError, ValueError, KeyError):
            pass
        result = bus.call_sync(
            'org.freedesktop.Notifications', '/org/freedesktop/Notifications',
            'org.freedesktop.Notifications', 'Notify',
            GLib.Variant('(susssasa{sv}i)', (
                'Alternar aplicativos', replacement, icon, name,
                GLib.markup_escape_text(title), [],
                {'transient': GLib.Variant('b', True),
                 'urgency': GLib.Variant('y', 0),
                 'suppress-sound': GLib.Variant('b', True)}, 1000)),
            GLib.VariantType.new('(u)'), Gio.DBusCallFlags.NONE, 1000, None
        ).unpack()[0]
        state.write_text(json.dumps({'owner': owner, 'id': result}))


if __name__ == '__main__':
    try:
        main()
    except (GLib.Error, OSError, KeyError, IndexError):
        # Notification failures must never interfere with window activation.
        pass
