# Commands

## Quick commands on PATH

```powershell
LuminAMI -e BIOSSettings.txt
LuminAMI -i BIOSSettings.txt --plan
LuminAMI -i BIOSSettings.txt
LuminAMI -h
LuminAMI help
LuminAMI ami help
LuminAMI install
```

`-e` exports settings plus `BIOSSettings.txt.capture` and
`BIOSSettings.txt.dupes.txt`. Keep the capture with the file. `-i` applies edited
settings from the matching capture and saves a unique
`BIOSSettings.txt.import-<id>.json` journal and `.result.json` report. `--plan`
validates and prints changes offline. Existing output files are never overwritten.
Use `--capture DIR` if you move or rename the settings file.

Both short commands accept `--driver PATH`, `--install-drivers`, and
`--non-interactive`. `-p <password>` refers to the existing BIOS password, but
authentication is currently unsupported: the command fails before driver setup
or writes, and does not echo the password.

`install` launches the GitHub release installer in the background, waits for the
current executable to exit, and updates the per-user installation and PATH.
It returns `started: true`, not completion. Check the returned `install.log`
for completion or errors; open a new terminal afterward. `--directory DIR`
chooses another installation directory. `--no-path` skips PATH changes.
It preserves your saved driver choice.

The advanced commands below also work through `LuminAMI ami <command>`.
Existing bare commands and SCEWIN-style aliases remain available.

Run `LuminAMI.cmd` for the numbered menu, or `LuminAMI.exe --help` in a terminal.
Commands print JSON. Exit code `0` means success; `1` means failure. Options need
values except `--plan`, `--no-path`, `--install-drivers` and `--non-interactive`. Duplicate/unknown options
are rejected, and operation output files aren't replaced.

## Shortcuts

| Script | What it does |
| --- | --- |
| `InstallDrivers.cmd [-Driver PATH] [-Directory DIR] [-NonInteractive]` | Choose provided/custom drivers and save the choice |
| `Export.cmd [-Driver PATH] [-Workspace DIR]` | Live export, raw capture, duplicate list, original copy; offers driver setup if needed |
| `Import.cmd [-Workspace DIR] [-Script FILE]` | Show changes and save a validated plan; no writes |
| `Import.cmd -Apply [-Driver PATH] [-Workspace DIR] [-Script FILE]` | Plan, then apply with the selected driver, readback, and journal |
| `Restore.cmd [-Driver PATH] [-Workspace DIR]` | Restore supported settings from the backup capture |
| `LuminAMI.cmd [-Workspace DIR]` | Export, inspect, edit, diff, plan, apply, restore, help |

The default workspace is `LuminAMI-backup` in your current directory. Each script
also accepts `-Executable PATH`. It finds the release executable or `build` copy
automatically. Put paths with spaces in quotes.

Live scripts also accept `-InstallDrivers` to select the provided drivers and
`-NonInteractive` to prevent prompts. A supplied `-Driver PATH` takes priority
and is saved. Don't combine `-Driver` with `-InstallDrivers`.

## Driver commands

| Command | Required arguments | Optional arguments |
| --- | --- | --- |
| `install-drivers` | — | `--directory DIR --non-interactive` |
| `use-driver` | `--driver PATH` | — |
| `driver-status` | — | — |

`install-drivers` extracts both embedded files and selects `amigendrv64.sys`.
It always works without prompts or network access. `use-driver` verifies and
saves an absolute custom path. `driver-status` returns `configured: false`
with a reason if a saved file is missing or changed. None of these load a driver.
See [driver setup](DRIVERS.md) for storage and headless configuration.

## Offline commands

| Command | Required arguments | Optional arguments |
| --- | --- | --- |
| `inspect` | `--script FILE` | `--output JSON` |
| `edit` | `--script FILE --output NEW-FILE --token TOKEN --value VALUE` | — |
| `diff` | `--before FILE --after FILE` | `--output JSON` |
| `inspect-hii` | `--hii FILE` | `--output JSON` |
| `export` | `--capture DIR --script NEW-FILE` | `--dupes NEW-FILE` |
| `import` | `--capture DIR --script FILE` | `--output JSON` |

Offline export requires an existing capture. Export to a new capture is live.
For `inspect` and `inspect-hii`, use `--output` for the full catalog. Terminal
output shows the summary. Offline `import` validates and plans; it never writes.

## System and live commands

| Command | Required arguments | Optional arguments | Writes BIOS settings? |
| --- | --- | --- | --- |
| `probe` | — | `--output JSON` | No |
| `diagnose` | — | `--output JSON` | No |
| `list-variables` | — | `--output JSON` | No |
| `read-variable` | `--name NAME --guid GUID` | `--output JSON` | No |
| `capture-ami` | `--output NEW-DIR` | `--driver PATH --report JSON` | No |
| `export` | `--capture NEW-DIR --script NEW-FILE` | `--driver PATH --dupes NEW-FILE --report JSON` | No |
| `import` | `--capture DIR --script FILE --journal NEW-JSON` | `--driver PATH --report JSON` | Yes |
| `test-import` | `--capture DIR --script FILE --journal NEW-JSON` | `--driver PATH --report JSON` | Yes, then restores |
| `restore` | `--capture BACKUP-DIR --journal NEW-JSON` | `--driver PATH --report JSON` | Yes |

Driver commands reuse the saved choice when `--driver` is omitted. All live
commands accept `--install-drivers` and `--non-interactive`. An explicit
`--driver` is saved and verified again before loading. Only export offers driver
setup interactively; other native live commands report missing setup immediately.
Redirected input never triggers a native setup prompt.

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
| `test-roundtrip` | `--capture DIR --script FILE --journal NEW-JSON` | Temporarily changes NumLock and restores it |
| `stage-numlock-test` | Same as `test-roundtrip` | Leaves NumLock Off for a reboot test |
| `finish-numlock-test` | `--journal STAGED-JSON --restore-journal NEW-JSON` | Verifies a different boot, then restores NumLock |
| `restore-numlock-test` | Same as `finish-numlock-test` | Cancels the staged test and restores without a reboot |

All four accept `--driver PATH`, `--install-drivers`, `--non-interactive`, and
`--report NEW-JSON`. The roundtrip/staging commands only accept
the specific one-byte NumLock On-to-Off patch expected by the validation code.
