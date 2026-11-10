#!/usr/bin/env bash
set -Eeuo pipefail
SOURCE_DIR=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
python3 -c 'import cairo, gi; gi.require_version("Gtk", "3.0"); from gi.repository import Gtk'
bash "$SOURCE_DIR/install.sh"
python3 "$SOURCE_DIR/scripts/install_visual_switcher.py"
