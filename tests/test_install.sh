#!/usr/bin/env bash
set -Eeuo pipefail

REPO_DIR=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
TEST_ROOT=$(mktemp -d)
trap 'rm -rf -- "$TEST_ROOT"' EXIT

fail()
{
    printf 'FAIL: %s\n' "$*" >&2
    exit 1
}

assert_file()
{
    [[ -f $1 ]] || fail "expected file: $1"
}

assert_missing()
{
    [[ ! -e $1 ]] || fail "expected missing path: $1"
}

assert_contains()
{
    grep -Fq -- "$2" "$1" || fail "expected '$2' in $1"
}

assert_not_contains()
{
    if [[ -f $1 ]] && grep -Fq -- "$2" "$1"; then
        fail "did not expect '$2' in $1"
    fi
}

configure_environment()
{
    export HOME="$1"
    export XDG_BIN_HOME="$HOME/.local/bin"
    export XDG_CONFIG_HOME="$HOME/.config"
    export XDG_STATE_HOME="$HOME/.local/state"
    CONFIG_FILE="$XDG_CONFIG_HOME/cosmic/com.system76.CosmicSettings.Shortcuts/v1/custom"
    STATE_DIR="$XDG_STATE_HOME/cosmic-cycle-app"
    BIN_FILE="$XDG_BIN_HOME/cosmic-cycle-app"
    mkdir -p -- "$(dirname -- "$CONFIG_FILE")" "$XDG_BIN_HOME"
}

test_surgical_uninstall_and_idempotent_reinstall()
{
    local scenario="$TEST_ROOT/surgical"
    local backup_hash
    configure_environment "$scenario/home with spaces"

    printf '%s\n' \
        '{' \
        '    (modifiers: [Super], key: "x"): System(Close),' \
        '    (modifiers: [Alt], key: "grave"): Spawn("/old/cosmic-cycle-app-windows"),' \
        '    (modifiers: [Alt, Shift], key: "grave"): Spawn("/old/cosmic-cycle-app-windows --reverse"),' \
        '}' >"$CONFIG_FILE"
    printf 'pre-existing binary\n' >"$BIN_FILE"
    chmod 700 "$BIN_FILE"

    bash "$REPO_DIR/install.sh"

    assert_file "$BIN_FILE"
    assert_file "$STATE_DIR/backups/shortcuts.original"
    assert_file "$STATE_DIR/backups/binary.original"
    cmp -s "$STATE_DIR/backups/shortcuts.original" <(
        printf '%s\n' \
            '{' \
            '    (modifiers: [Super], key: "x"): System(Close),' \
            '    (modifiers: [Alt], key: "grave"): Spawn("/old/cosmic-cycle-app-windows"),' \
            '    (modifiers: [Alt, Shift], key: "grave"): Spawn("/old/cosmic-cycle-app-windows --reverse"),' \
            '}'
    ) || fail "shortcut backup is not exact"
    assert_contains "$CONFIG_FILE" '// cosmic-cycle-app: begin'
    assert_contains "$CONFIG_FILE" 'modifiers: [Alt, Shift]'
    assert_not_contains "$CONFIG_FILE" 'cosmic-cycle-app-windows'
    [[ $(grep -Fc '// cosmic-cycle-app: begin' "$CONFIG_FILE") -eq 1 ]] ||
        fail "installer marker is duplicated"

    backup_hash=$(sha256sum "$STATE_DIR/backups/shortcuts.original")
    bash "$REPO_DIR/install.sh"
    [[ $backup_hash == "$(sha256sum "$STATE_DIR/backups/shortcuts.original")" ]] ||
        fail "reinstall overwrote the original backup"
    [[ $(grep -Fc '// cosmic-cycle-app: begin' "$CONFIG_FILE") -eq 1 ]] ||
        fail "reinstall duplicated shortcut entries"

    sed -i '$i\\    // user edit after installation' "$CONFIG_FILE"
    bash "$REPO_DIR/uninstall.sh"

    assert_contains "$CONFIG_FILE" '(modifiers: [Super], key: "x")'
    assert_contains "$CONFIG_FILE" '// user edit after installation'
    assert_not_contains "$CONFIG_FILE" '// cosmic-cycle-app: begin'
    assert_not_contains "$CONFIG_FILE" 'cosmic-cycle-app-windows'
    assert_contains "$BIN_FILE" 'pre-existing binary'
    assert_missing "$STATE_DIR"
}

test_exact_restore()
{
    local scenario="$TEST_ROOT/restore"
    local original_hash
    configure_environment "$scenario/home"

    printf '%s\n' \
        '{' \
        '    (modifiers: [Super], key: "q"): System(Quit),' \
        '}' >"$CONFIG_FILE"
    original_hash=$(sha256sum "$CONFIG_FILE")

    bash "$REPO_DIR/install.sh"
    sed -i '$i\\    // this edit is intentionally discarded by exact restore' \
        "$CONFIG_FILE"
    bash "$REPO_DIR/uninstall.sh" --restore-backup

    [[ $original_hash == "$(sha256sum "$CONFIG_FILE")" ]] ||
        fail "exact restore did not restore the original shortcut file"
    assert_missing "$BIN_FILE"
    assert_missing "$STATE_DIR"
}

test_restore_when_config_was_absent()
{
    local scenario="$TEST_ROOT/missing"
    configure_environment "$scenario/home"
    assert_missing "$CONFIG_FILE"

    bash "$REPO_DIR/install.sh"
    assert_file "$CONFIG_FILE"
    bash "$REPO_DIR/uninstall.sh" --restore-backup

    assert_missing "$CONFIG_FILE"
    assert_missing "$BIN_FILE"
    assert_missing "$STATE_DIR"
}

test_tampered_binary_refuses_partial_uninstall()
{
    local scenario="$TEST_ROOT/tampered"
    local config_hash
    configure_environment "$scenario/home"

    printf '%s\n' \
        '{' \
        '    (modifiers: [Super], key: "w"): System(Close),' \
        '}' >"$CONFIG_FILE"
    bash "$REPO_DIR/install.sh"
    printf 'user replacement\n' >"$BIN_FILE"
    config_hash=$(sha256sum "$CONFIG_FILE")

    if bash "$REPO_DIR/uninstall.sh"; then
        fail "surgical uninstall accepted a changed binary"
    fi
    [[ $config_hash == "$(sha256sum "$CONFIG_FILE")" ]] ||
        fail "failed uninstall partially changed the shortcut config"
    assert_contains "$CONFIG_FILE" '// cosmic-cycle-app: begin'
    assert_file "$STATE_DIR/backup-complete"

    bash "$REPO_DIR/uninstall.sh" --restore-backup
    assert_missing "$BIN_FILE"
    assert_missing "$STATE_DIR"
}

test_piped_bootstrap()
{
    local scenario="$TEST_ROOT/piped"
    local fake_bin="$scenario/fake-bin"
    local archive="$scenario/source.tar.gz"
    configure_environment "$scenario/home"
    mkdir -p -- "$fake_bin"

    tar -czf "$archive" \
        --exclude='.git' \
        --exclude='build' \
        -C "$(dirname -- "$REPO_DIR")" "$(basename -- "$REPO_DIR")"
    printf '%s\n' \
        '#!/usr/bin/env bash' \
        'set -eu' \
        'exec /bin/cat "$COSMIC_CYCLE_TEST_ARCHIVE"' \
        >"$fake_bin/curl"
    chmod 755 "$fake_bin/curl"

    COSMIC_CYCLE_TEST_ARCHIVE="$archive" \
        PATH="$fake_bin:$PATH" \
        bash <"$REPO_DIR/install.sh"
    assert_file "$BIN_FILE"
    assert_contains "$CONFIG_FILE" '// cosmic-cycle-app: begin'

    COSMIC_CYCLE_TEST_ARCHIVE="$archive" \
        PATH="$fake_bin:$PATH" \
        bash <"$REPO_DIR/uninstall.sh"
    assert_missing "$BIN_FILE"
    assert_missing "$STATE_DIR"
}

test_visual_install_lifecycle()
{
    local scenario="$TEST_ROOT/visual"
    configure_environment "$scenario/home"
    local helper_dir="$HOME/.local/lib/cosmic-cycle-app"
    mkdir -p -- "$helper_dir" "$XDG_CONFIG_HOME/cosmic/com.system76.CosmicComp/v1"
    printf 'original renderer\n' >"$helper_dir/render_switcher.py"
    printf '(variant: "alt-intl")\n' >"$XDG_CONFIG_HOME/cosmic/com.system76.CosmicComp/v1/xkb_config"
    printf '{\n    (modifiers: [Super], key: "d"): System(WorkspaceOverview),\n}\n' >"$CONFIG_FILE"
    bash "$REPO_DIR/install-visual.sh"
    assert_contains "$CONFIG_FILE" ' --switcher'
    assert_contains "$CONFIG_FILE" 'dead_grave'
    assert_contains "$CONFIG_FILE" 'dead_tilde'
    [[ $(grep -c ' --switcher' "$CONFIG_FILE") -eq 2 ]] || fail 'missing visual shortcuts'
    assert_file "$helper_dir/render_switcher.py"
    assert_file "$helper_dir/show_app.py"
    bash "$REPO_DIR/install-visual.sh"
    [[ $(grep -c ' --switcher' "$CONFIG_FILE") -eq 2 ]] || fail 'duplicated visual shortcuts'
    [[ $(grep -c 'dead_tilde' "$CONFIG_FILE") -eq 1 ]] || fail 'duplicated international shortcut'
    python3 "$REPO_DIR/scripts/install_visual_switcher.py" --rollback
    assert_not_contains "$CONFIG_FILE" ' --switcher'
    assert_contains "$CONFIG_FILE" ' --apps'
    python3 "$REPO_DIR/scripts/install_visual_switcher.py"
    printf 'tampered\n' >>"$helper_dir/render_switcher.py"
    if bash "$REPO_DIR/uninstall-visual.sh"; then
        fail 'uninstall accepted tampered helper'
    fi
    assert_contains "$CONFIG_FILE" ' --switcher'
    bash "$REPO_DIR/uninstall-visual.sh" --restore-backup
    assert_contains "$CONFIG_FILE" 'WorkspaceOverview'
    assert_not_contains "$CONFIG_FILE" ' --switcher'
    assert_contains "$helper_dir/render_switcher.py" 'original renderer'
    assert_missing "$helper_dir/show_app.py"
    assert_missing "$STATE_DIR"
    assert_missing "$XDG_STATE_HOME/cosmic-app-switcher"
}

test_surgical_uninstall_and_idempotent_reinstall
test_exact_restore
test_restore_when_config_was_absent
test_tampered_binary_refuses_partial_uninstall
test_piped_bootstrap
test_visual_install_lifecycle
printf 'Installer integration tests passed.\n'
