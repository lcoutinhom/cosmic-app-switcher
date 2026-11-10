#!/usr/bin/python3
"""Back up, install and restore the visual switcher's user-scoped helpers."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil

from shortcut_config import atomic_write


def locations():
    state = Path(os.environ.get('XDG_STATE_HOME', Path.home() / '.local/state')) / 'cosmic-app-switcher'
    library = Path.home() / '.local/lib/cosmic-cycle-app'
    return state, library


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def destination(name):
    if name == 'focus-tracker.desktop':
        config = Path(os.environ.get('XDG_CONFIG_HOME', Path.home() / '.config'))
        return config / 'autostart/cosmic-app-switcher-focus.desktop'
    if name not in ('render_switcher.py', 'show_app.py'):
        raise ValueError(f'Unknown managed asset: {name}')
    return locations()[1] / name


def tracker_desktop():
    binary = Path(os.environ.get('XDG_BIN_HOME', Path.home() / '.local/bin')) / 'cosmic-cycle-app'
    quoted = '"' + str(binary).replace('\\', '\\\\').replace('"', '\\"').replace('$', '\\$').replace('`', '\\`') + '"'
    return ('[Desktop Entry]\nType=Application\nName=COSMIC App Switcher Focus Tracker\n'
            f'Exec={quoted} --track-focus\nOnlyShowIn=COSMIC;\nNoDisplay=true\n'
            'Comment=Remember recently used applications for Alt+Tab\n')


def install():
    state, library = locations()
    source = Path(__file__).resolve().parent
    manifest = state / 'assets.json'
    entries = json.loads(manifest.read_text()) if manifest.exists() else {}
    contents = {name: (source / name).read_text() for name in ('render_switcher.py', 'show_app.py')}
    contents['focus-tracker.desktop'] = tracker_desktop()
    for name in contents:
        target = destination(name)
        if target.is_symlink() or (target.exists() and not target.is_file()):
            raise ValueError(f'Unsupported helper path: {target}')
    state.mkdir(parents=True, exist_ok=True, mode=0o700)
    library.mkdir(parents=True, exist_ok=True)
    for name, content in contents.items():
        target = destination(name)
        if name not in entries:
            entries[name] = {'existed': target.exists()}
            if target.exists():
                shutil.copy2(target, state / (name + '.original'))
        atomic_write(target, content, 0o644)
        entries[name]['installed_hash'] = digest(target)
    atomic_write(manifest, json.dumps(entries, indent=2) + '\n', 0o600)


def verify(force=False):
    state, library = locations()
    manifest = state / 'assets.json'
    if not manifest.exists():
        return
    for name, entry in json.loads(manifest.read_text()).items():
        target = destination(name)
        if not force and (target.is_symlink() or
                (target.exists() and digest(target) != entry['installed_hash'])):
            raise ValueError(f'Helper changed: {target}; use --restore-backup to restore original files')
        if entry['existed'] and not (state / (name + '.original')).is_file():
            raise ValueError(f'Missing helper backup: {name}')


def remove(force=False):
    verify(force)
    state, library = locations()
    manifest = state / 'assets.json'
    if not manifest.exists():
        return
    for name, entry in json.loads(manifest.read_text()).items():
        target = destination(name)
        backup = state / (name + '.original')
        if entry['existed']:
            if target.is_symlink():
                target.unlink()
            atomic_write(target, backup.read_text(), backup.stat().st_mode & 0o777)
            backup.unlink()
        else:
            target.unlink(missing_ok=True)
    manifest.unlink()
    state.rmdir()
    if library.exists() and not any(library.iterdir()):
        library.rmdir()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('install', 'verify', 'remove'))
    parser.add_argument('--restore-backup', action='store_true')
    args = parser.parse_args()
    try:
        if args.action == 'install':
            install()
        elif args.action == 'verify':
            verify(args.restore_backup)
        else:
            remove(args.restore_backup)
    except (OSError, ValueError) as error:
        raise SystemExit(str(error)) from error
