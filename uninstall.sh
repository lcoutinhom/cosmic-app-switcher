#!/usr/bin/env bash
set -Eeuo pipefail
umask 077

readonly PROJECT='cosmic-cycle-app'
readonly GITHUB_REPOSITORY='lcoutinhom/cosmic-app-switcher'
readonly GITHUB_ARCHIVE="https://github.com/${GITHUB_REPOSITORY}/archive/refs/heads/main.tar.gz"

TEMP_DIR=''
RESTORE_BACKUP=false

cleanup_temp()
{
    if [[ -n $TEMP_DIR && -d $TEMP_DIR ]]; then
        rm -rf -- "$TEMP_DIR"
    fi
}
trap cleanup_temp EXIT

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

prepare_source()
{
    local script_dir
    if [[ -n ${BASH_SOURCE[0]:-} && -f ${BASH_SOURCE[0]} ]]; then
        script_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &&
            pwd)
    else
        script_dir=''
    fi
    if [[ -n $script_dir && -f $script_dir/scripts/shortcut_config.py ]]; then
        SOURCE_DIR=$script_dir
        return
    fi

    need_command curl
    need_command tar
    TEMP_DIR=$(mktemp -d)
    curl --fail --silent --show-error --location "$GITHUB_ARCHIVE" |
        tar -xz --strip-components=1 -C "$TEMP_DIR"
    SOURCE_DIR=$TEMP_DIR
}

restore_file()
{
    local backup=$1
    local destination=$2
    local destination_dir
    local temporary

    destination_dir=$(dirname -- "$destination")
    mkdir -p -- "$destination_dir"
    temporary=$(mktemp "$destination_dir/.${destination##*/}.XXXXXX")
    cp -p -- "$backup" "$temporary"
    mv -f -- "$temporary" "$destination"
}

remove_state()
{
    rm -f -- \
        "$BACKUP_DIR/shortcuts.original" \
        "$BACKUP_DIR/shortcuts.missing" \
        "$BACKUP_DIR/binary.original" \
        "$BACKUP_DIR/binary.missing" \
        "$STATE_DIR/installed-binary.sha256" \
        "$STATE_DIR/install-complete" \
        "$STATE_DIR/backup-complete"
    rmdir -- "$BACKUP_DIR" "$STATE_DIR"
}

while (($# > 0)); do
    case $1 in
        --restore-backup)
            RESTORE_BACKUP=true
            ;;
        -h|--help)
            printf 'Usage: %s [--restore-backup]\n' "${0##*/}"
            exit 0
            ;;
        *)
            die "unknown argument: $1"
            ;;
    esac
    shift
done

[[ ${EUID:-$(id -u)} -ne 0 ]] ||
    die "do not run this uninstaller as root or with sudo"
[[ ${HOME:-} == /* ]] || die 'HOME must be an absolute path'
need_command python3
need_command mktemp
need_command sha256sum
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
[[ -f $STATE_DIR/backup-complete ]] ||
    die "backup state not found at $STATE_DIR; refusing an unsafe uninstall"
if [[ ! -e $BACKUP_DIR/shortcuts.original &&
      ! -L $BACKUP_DIR/shortcuts.original &&
      ! -f $BACKUP_DIR/shortcuts.missing ]]; then
    die "shortcut backup metadata is incomplete"
fi
if [[ ! -e $BACKUP_DIR/binary.original &&
      ! -L $BACKUP_DIR/binary.original &&
      ! -f $BACKUP_DIR/binary.missing ]]; then
    die "binary backup metadata is incomplete"
fi
if ! $RESTORE_BACKUP && [[ -L $CONFIG_FILE ]]; then
    die "shortcut config became a symbolic link; use --restore-backup to replace it"
fi
if ! $RESTORE_BACKUP && [[ -e $BIN_FILE || -L $BIN_FILE ]]; then
    [[ -f $STATE_DIR/installed-binary.sha256 ]] ||
        die "installed binary checksum is missing"
    expected_hash=$(<"$STATE_DIR/installed-binary.sha256")
    current_hash_output=$(sha256sum "$BIN_FILE")
    current_hash=${current_hash_output%% *}
    [[ $current_hash == "$expected_hash" ]] ||
        die "installed binary changed; rerun with --restore-backup to overwrite it"
fi

if $RESTORE_BACKUP; then
    if [[ -e $BACKUP_DIR/shortcuts.original ||
          -L $BACKUP_DIR/shortcuts.original ]]; then
        restore_file "$BACKUP_DIR/shortcuts.original" "$CONFIG_FILE"
    elif [[ -f $BACKUP_DIR/shortcuts.missing ]]; then
        rm -f -- "$CONFIG_FILE"
    else
        die "shortcut backup metadata is incomplete"
    fi
else
    delete_if_empty=()
    if [[ -f $BACKUP_DIR/shortcuts.missing ]]; then
        delete_if_empty=(--delete-if-empty)
    fi
    if [[ -e $CONFIG_FILE || -L $CONFIG_FILE ]]; then
        python3 "$SOURCE_DIR/scripts/shortcut_config.py" remove "$CONFIG_FILE" \
            "${delete_if_empty[@]}"
    fi
fi

if [[ -e $BIN_FILE || -L $BIN_FILE ]]; then
    if [[ -e $BACKUP_DIR/binary.original ||
          -L $BACKUP_DIR/binary.original ]]; then
        restore_file "$BACKUP_DIR/binary.original" "$BIN_FILE"
    elif [[ -f $BACKUP_DIR/binary.missing ]]; then
        rm -f -- "$BIN_FILE"
    else
        die "binary backup metadata is incomplete"
    fi
elif [[ -e $BACKUP_DIR/binary.original ||
        -L $BACKUP_DIR/binary.original ]]; then
    restore_file "$BACKUP_DIR/binary.original" "$BIN_FILE"
fi

remove_state

if $RESTORE_BACKUP; then
    printf 'Restored the exact pre-install files and removed %s.\n' "$PROJECT"
else
    printf 'Removed %s while preserving later shortcut edits.\n' "$PROJECT"
fi
