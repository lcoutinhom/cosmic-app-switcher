#!/usr/bin/env bash
set -Eeuo pipefail
umask 077

readonly PROJECT='cosmic-cycle-app'
readonly GITHUB_REPOSITORY='lcoutinhom/cosmic-app-switcher'
readonly GITHUB_ARCHIVE="https://github.com/${GITHUB_REPOSITORY}/archive/refs/heads/main.tar.gz"

TEMP_DIR=''

cleanup()
{
    if [[ -n $TEMP_DIR && -d $TEMP_DIR ]]; then
        rm -rf -- "$TEMP_DIR"
    fi
}
trap cleanup EXIT

die()
{
    printf '%s: %s\n' "$PROJECT" "$*" >&2
    exit 1
}

need_command()
{
    command -v "$1" >/dev/null 2>&1 ||
        die "missing required command: $1"
}

require_regular_or_missing()
{
    if [[ -L $1 ]]; then
        die "symbolic links are not supported for managed paths: $1"
    fi
    if [[ -e $1 && ! -f $1 ]]; then
        die "managed path is not a regular file: $1"
    fi
}

prepare_source()
{
    local script_dir
    if [[ -n ${BASH_SOURCE[0]:-} && -f ${BASH_SOURCE[0]} ]]; then
        script_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &&
            pwd)
    else
        script_dir=''
    fi
    if [[ -n $script_dir && -f $script_dir/Makefile &&
          -f $script_dir/scripts/shortcut_config.py ]]; then
        SOURCE_DIR=$script_dir
        return
    fi

    need_command curl
    need_command tar
    TEMP_DIR=$(mktemp -d)
    printf 'Downloading %s source...\n' "$PROJECT"
    curl --fail --silent --show-error --location "$GITHUB_ARCHIVE" |
        tar -xz --strip-components=1 -C "$TEMP_DIR"
    SOURCE_DIR=$TEMP_DIR
}

backup_once()
{
    if [[ -f $STATE_DIR/backup-complete ]]; then
        printf 'Keeping the existing original backup.\n'
        return
    fi

    mkdir -p -- "$BACKUP_DIR"
    require_regular_or_missing "$CONFIG_FILE"
    require_regular_or_missing "$BIN_FILE"
    if [[ -e $CONFIG_FILE ]]; then
        cp -p -- "$CONFIG_FILE" "$BACKUP_DIR/shortcuts.original"
    else
        : >"$BACKUP_DIR/shortcuts.missing"
    fi

    if [[ -e $BIN_FILE ]]; then
        cp -p -- "$BIN_FILE" "$BACKUP_DIR/binary.original"
    else
        : >"$BACKUP_DIR/binary.missing"
    fi
    : >"$STATE_DIR/backup-complete"
}

[[ ${EUID:-$(id -u)} -ne 0 ]] ||
    die "do not run this installer as root or with sudo"
[[ ${HOME:-} == /* ]] || die 'HOME must be an absolute path'

need_command cc
need_command make
need_command pkg-config
need_command wayland-scanner
need_command python3
need_command install
need_command mktemp
need_command sha256sum
pkg-config --exists wayland-client ||
    die "wayland-client development files are missing"

prepare_source

readonly BIN_DIR="${XDG_BIN_HOME:-$HOME/.local/bin}"
readonly BIN_FILE="$BIN_DIR/$PROJECT"
readonly CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
readonly CONFIG_FILE="$CONFIG_HOME/cosmic/com.system76.CosmicSettings.Shortcuts/v1/custom"
readonly STATE_HOME="${XDG_STATE_HOME:-$HOME/.local/state}"
readonly STATE_DIR="$STATE_HOME/$PROJECT"
readonly BACKUP_DIR="$STATE_DIR/backups"

[[ $BIN_DIR == /* ]] || die 'XDG_BIN_HOME must resolve to an absolute path'
[[ $CONFIG_HOME == /* ]] ||
    die 'XDG_CONFIG_HOME must resolve to an absolute path'
[[ $STATE_HOME == /* ]] ||
    die 'XDG_STATE_HOME must resolve to an absolute path'
backup_once

make -C "$SOURCE_DIR" unit-test all
python3 "$SOURCE_DIR/scripts/shortcut_config.py" install "$CONFIG_FILE" \
    --binary "$BIN_FILE" --check

mkdir -p -- "$BIN_DIR"
temporary_binary=$(mktemp "$BIN_DIR/.$PROJECT.XXXXXX")
trap 'rm -f -- "$temporary_binary"; cleanup' EXIT
install -m 755 -- "$SOURCE_DIR/build/$PROJECT" "$temporary_binary"
mv -f -- "$temporary_binary" "$BIN_FILE"

python3 "$SOURCE_DIR/scripts/shortcut_config.py" install "$CONFIG_FILE" \
    --binary "$BIN_FILE"
installed_hash=$(sha256sum "$BIN_FILE")
printf '%s\n' "${installed_hash%% *}" >"$STATE_DIR/installed-binary.sha256"
: >"$STATE_DIR/install-complete"

printf '\nInstalled without root access:\n'
printf '  Binary:   %s\n' "$BIN_FILE"
printf '  Shortcuts: Alt+` (next), Alt+Shift+` (previous)\n'
printf '  Backup:   %s\n' "$BACKUP_DIR"
printf 'No logout or restart is required.\n'
