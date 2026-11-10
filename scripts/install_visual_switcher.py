#!/usr/bin/python3
"""Enable the visual app switcher after the base installer, or select immediate mode."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sys

from shortcut_config import MARKED_BLOCK, END_MARKER, atomic_write, read_config, validate_ron_map
import visual_assets


def main():
    if sys.argv[1:] not in ([], ['--rollback']):
        raise SystemExit('usage: install_visual_switcher.py [--rollback]')
    home = Path.home()
    config_home = Path(os.environ.get('XDG_CONFIG_HOME', home / '.config'))
    config = config_home / 'cosmic/com.system76.CosmicSettings.Shortcuts/v1/custom'
    state = Path(os.environ.get('XDG_STATE_HOME', home / '.local/state')) / 'cosmic-cycle-app'
    binary = Path(os.environ.get('XDG_BIN_HOME', home / '.local/bin')) / 'cosmic-cycle-app'
    content, mode = read_config(config)
    validate_ron_map(content)
    matches = list(MARKED_BLOCK.finditer(content))
    if len(matches) != 1 or not (state / 'backup-complete').exists():
        raise SystemExit('Run bash ./install.sh first')
    match = matches[0]
    block = match.group(0)
    rollback = '--rollback' in sys.argv
    if rollback:
        updated = block.replace(' --switcher', ' --apps')
        if updated.count(' --apps') != 2:
            raise SystemExit('Expected two application-switching shortcuts')
    else:
        # Stop before changing files if another shortcut owns Alt+Tab.
        other = content[:match.start()] + content[match.end():]
        for binding in re.finditer(r'\(\s*modifiers:\s*\[([^\]]*)\],?\s*key:\s*"Tab"', other):
            modifiers = {part.strip() for part in binding[1].split(',') if part.strip()}
            if modifiers in ({'Alt'}, {'Alt', 'Shift'}):
                raise SystemExit('Custom Alt+Tab binding conflict; reassign it first')
        updated = block.replace(' --apps', ' --switcher')
        if ' --switcher' not in updated:
            bindings = ''
            for modifiers, suffix in [('Alt', ''), ('Alt, Shift', ' --reverse')]:
                command = json.dumps(f'{binary} --switcher{suffix}')
                bindings += f'    (modifiers: [{modifiers}], key: "Tab"): Spawn({command}),\n'
            updated = updated.replace('    ' + END_MARKER, bindings + '    ' + END_MARKER)
        if updated.count(' --switcher') != 2:
            raise SystemExit('Unexpected application-switcher shortcut block')
        keyboard = config_home / 'cosmic/com.system76.CosmicComp/v1/xkb_config'
        if keyboard.exists() and re.search(r'variant:\s*"(?:alt-intl|intl)"', keyboard.read_text()):
            updated = updated.replace('key: "grave"', 'key: "dead_grave"')
            if 'key: "dead_tilde"' not in updated:
                command = json.dumps(f'{binary} --reverse')
                binding = f'    (modifiers: [Alt, Shift], key: "dead_tilde"): Spawn({command}),\n'
                updated = updated.replace('    ' + END_MARKER, binding + '    ' + END_MARKER)
        import cairo
        import gi
        gi.require_version('Gtk', '3.0')
        from gi.repository import Gtk
        assert cairo and Gtk
        built = Path(__file__).resolve().parent.parent / 'build/cosmic-cycle-app'
        if not built.is_file():
            raise SystemExit('Run make first')
        visual_assets.install()
        temporary = binary.with_name('.cosmic-cycle-app.visual')
        shutil.copy2(built, temporary)
        temporary.chmod(0o755)
        os.replace(temporary, binary)
        (state / 'installed-binary.sha256').write_text(hashlib.sha256(binary.read_bytes()).hexdigest() + '\n')
    atomic_write(config, content[:match.start()] + updated + content[match.end():], mode)
    print('Immediate application cycling enabled.' if rollback else
          'Visual switcher enabled: Alt+Tab; release Alt to confirm, Esc to cancel.')


if __name__ == '__main__':
    main()
