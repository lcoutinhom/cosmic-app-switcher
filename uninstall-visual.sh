#!/usr/bin/env bash
set -Eeuo pipefail
SOURCE_DIR=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
python3 "$SOURCE_DIR/scripts/visual_assets.py" verify "$@"
bash "$SOURCE_DIR/uninstall.sh" "$@"
python3 "$SOURCE_DIR/scripts/visual_assets.py" remove "$@"
