# Installer security model

## Trust boundaries

The installer treats the repository source, XDG environment variables, the
user's filesystem, and the shortcut configuration as inputs. Its primary
risks are unintended file replacement, malformed configuration, partial
uninstallation, and the supply-chain implications of `curl | bash`.

## Controls

- Refuses to run as UID 0 and never invokes `sudo`, `su`, Polkit, a package
  manager, or a service manager.
- Uses user-scoped XDG paths and quotes every shell path.
- Applies `umask 077` to state and backup files.
- Backs up both the shortcut configuration and binary before changing either.
- Never overwrites the original backup during reinstallation.
- Validates the basic RON map shape and checks for `Alt+grave` conflicts before
  writing.
- Writes configuration through a temporary file in the same directory and
  then calls `os.replace` for atomic replacement.
- Stores the installed binary checksum. Surgical cleanup stops before changing
  anything if the binary has been replaced.
- Removes only explicitly named state files during uninstall, then uses
  `rmdir`; it never recursively deletes configuration or home directories.

## Rollback choices

The default uninstall removes only the marked block so unrelated later edits
remain intact. `--restore-backup` restores the exact snapshot and can therefore
discard edits made after installation. Both modes delete backup state after a
successful uninstall.

## Supply chain

`curl | bash` is convenient but executes the current branch contents. For
auditing, download and inspect `install.sh` first or clone an immutable commit
or tag. The bootstrap script downloads an archive from the same repository and
branch. A future release should publish immutable tags and checksums.
