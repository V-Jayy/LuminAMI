# Commands

Run `LuminAMI.cmd` for the numbered menu, or `LuminAMI.exe --help` in a terminal.
Commands print JSON. Exit code `0` means success; `1` means failure. Options need
values, duplicate/unknown options are rejected, and output files aren't replaced.

## Shortcuts

| Script | What it does |
| --- | --- |
| `Export.cmd -Driver PATH [-Workspace DIR]` | Live export, raw capture, duplicate list, original copy |
| `Import.cmd [-Workspace DIR] [-Script FILE]` | Show changes and save a validated plan; no writes |
| `Import.cmd -Driver PATH -Apply [-Workspace DIR] [-Script FILE]` | Plan, then apply settings with readback and journal |
| `Restore.cmd -Driver PATH [-Workspace DIR]` | Restore supported settings from the backup capture |
| `LuminAMI.cmd [-Workspace DIR]` | Export, inspect, edit, diff, plan, apply, restore, help |

The default workspace is `LuminAMI-backup` in your current directory. Each script
also accepts `-Executable PATH`. It finds the release executable or `build` copy
automatically. Put paths with spaces in quotes.

## Offline commands

| Command | Required arguments | Optional arguments |
| --- | --- | --- |
| `inspect` | `--script FILE` | `--output JSON` |
| `edit` | `--script FILE --output NEW-FILE --token TOKEN --value VALUE` | — |
| `diff` | `--before FILE --after FILE` | `--output JSON` |
| `inspect-hii` | `--hii FILE` | `--output JSON` |
| `export` | `--capture DIR --script NEW-FILE` | `--dupes NEW-FILE` |
| `import` | `--capture DIR --script FILE` | `--output JSON` |

For `inspect` and `inspect-hii`, use `--output` for the full catalog. Terminal
output shows the summary. Offline `import` validates and plans; it never writes.

## System and live commands

| Command | Required arguments | Optional arguments | Writes BIOS settings? |
| --- | --- | --- | --- |
| `probe` | — | `--output JSON` | No |
| `diagnose` | — | `--output JSON` | No |
| `list-variables` | — | `--output JSON` | No |
| `read-variable` | `--name NAME --guid GUID` | `--output JSON` | No |
| `capture-ami` | `--driver PATH --output NEW-DIR` | `--report JSON` | No |
| `export` | `--driver PATH --capture NEW-DIR --script NEW-FILE` | `--dupes NEW-FILE --report JSON` | No |
| `import` | `--driver PATH --capture DIR --script FILE --journal NEW-JSON` | `--report JSON` | Yes |
| `test-import` | `--driver PATH --capture DIR --script FILE --journal NEW-JSON` | `--report JSON` | Yes, then restores |
| `restore` | `--driver PATH --capture BACKUP-DIR --journal NEW-JSON` | `--report JSON` | Yes |

Windows variable reads require the system environment privilege, normally an
administrator terminal. AMI driver commands require elevation. `probe` reads
platform information without loading an AMI driver.

Use `--output` for offline JSON results and `--report` for live operation reports.
A live import requires a fresh journal path. Don't reuse journals or place reports
over your input files. Restore keeps a fresh capture alongside its journal.

## SCEWIN-style aliases

| Alias | LuminAMI equivalent |
| --- | --- |
| `/O` | `export` |
| `/I` | `import` |
| `/S FILE` | `--script FILE` |
| `/SD FILE` | `--dupes FILE` |

The aliases are case-insensitive. They don't remove capture, driver, or journal
requirements. `/CPWD` and `--apply` aren't supported CLI arguments; use ordinary
`import --driver ... --journal ...`. The script flag `-Apply` is separate.

## Hardware validation commands

These are for a reviewed NumLock-only experiment, not ordinary imports. Each
command may write firmware. Reboot persistence has not been verified in this
release. Use normal `import` or `test-import` for the general engine.

| Command | Required arguments | Behavior |
| --- | --- | --- |
| `test-roundtrip` | `--capture DIR --script FILE --driver PATH --journal NEW-JSON` | Temporarily changes NumLock and restores it |
| `stage-numlock-test` | Same as `test-roundtrip` | Leaves NumLock Off for a reboot test |
| `finish-numlock-test` | `--driver PATH --journal STAGED-JSON --restore-journal NEW-JSON` | Verifies a different boot, then restores NumLock |
| `restore-numlock-test` | Same as `finish-numlock-test` | Cancels the staged test and restores without a reboot |

All four accept `--report NEW-JSON`. The roundtrip/staging commands only accept
the specific one-byte NumLock On-to-Off patch expected by the validation code.
