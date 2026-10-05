# LuminAMI

Export your AMI BIOS settings to a text file, edit them, and import them back.
Built in C++20 for Windows x64. Use the executable directly or use the included
scripts if you just want to get going.

Made by [Lumin](https://discord.gg/lumin). If you need help or want to share
results from your board, join [discord.gg/lumin](https://discord.gg/lumin).

## Download

Grab **LuminAMI-v0.1.0-windows-x64.zip** from the
[releases page](https://github.com/V-Jayy/LuminAMI/releases/latest) and extract it.
Run `LuminAMI.cmd` for the command menu, or open a terminal in that folder.

Live export/import needs an administrator terminal and a supported AMI driver.
Drivers are **not included**. See [driver requirements](docs/DRIVERS.md) for the
two accepted files and their hashes. Offline editing and import planning don't
need a driver or administrator access.

## Export, edit, import

From an administrator terminal:

```powershell
.\Export.cmd -Driver .\drivers\amigendrv64.sys
```

This creates `LuminAMI-backup` with `BIOSSettings.txt`, an untouched
`OriginalSettings.txt`, `Dupes.txt`, and the raw `capture` folder. Keep that whole
folder. The capture is needed to validate an import and restore your backup.

Open `BIOSSettings.txt` in a text editor. Move the `*` to the option you want, or
edit the `Value` for a numeric/string setting. Leave tokens, offsets, widths,
and commented-out settings alone.

```powershell
# Check what changed and generate a plan. This does not write to the BIOS.
.\Import.cmd

# Apply the edited file, with readback and a transaction journal.
.\Import.cmd -Driver .\drivers\amigendrv64.sys -Apply
```

Use `-Workspace .\my-backup` on both scripts to choose another backup folder.
Use `-Script .\edited.txt` on import to load a separate edited file. Export
requires a new folder so it doesn't overwrite an older backup.

## Direct commands

```powershell
.\LuminAMI.exe --help
.\LuminAMI.exe export --driver .\drivers\amigendrv64.sys --capture .\capture --script settings.txt --dupes Dupes.txt
.\LuminAMI.exe inspect --script settings.txt
.\LuminAMI.exe edit --script settings.txt --output edited.txt --token 0x2B --value 0
.\LuminAMI.exe diff --before settings.txt --after edited.txt
.\LuminAMI.exe import --capture .\capture --script edited.txt --output plan.json
.\LuminAMI.exe import --driver .\drivers\amigendrv64.sys --capture .\capture --script edited.txt --journal import.json
```

The token and value above are examples. Use the ones from your own export.

The `/O`, `/I`, `/S`, and `/SD` aliases are there if you're used to SCEWIN syntax:

```powershell
.\LuminAMI.exe /O /S settings.txt /SD Dupes.txt --driver .\drivers\amigendrv64.sys --capture .\capture
.\LuminAMI.exe /I /S edited.txt --capture .\capture
```

LuminAMI uses its own export identities. Use its exports with their matching
captures; a SCEWIN export is not a drop-in live import. It doesn't run SCEWIN.

[Full command reference](docs/COMMANDS.md) ·
[Editing settings](docs/SETTINGS.md) ·
[Driver requirements](docs/DRIVERS.md)

## Restore a backup

```powershell
.\Restore.cmd -Driver .\drivers\amigendrv64.sys -Workspace .\LuminAMI-backup
```

Restore reads fresh firmware and restores supported settings from your capture,
preserving unrelated current bytes. Keep the journals if an operation fails.

## Build

Install Visual Studio 2022 C++ build tools and the Windows SDK, then run:

```powershell
.\build.ps1 -Test
.\build\LuminAMI.exe --help
```

The executable uses the static C++ runtime. `scripts/package.ps1` builds, tests,
and creates the release ZIP and SHA256 file. GitHub Actions runs the same checks
and packages tagged releases.

## Current limits

This is the first public release. BIOS writes can leave a system unable to boot;
know your board's recovery process before changing settings.

Imports check capture identity, field bounds, available options, numeric ranges,
variable attributes, and read-only constraints before writing. Writes are
journaled and read back. A failed transaction attempts rollback, which can also
fail if the firmware or driver stops responding.

Earlier hardware validation covered NumLock write/readback/restore on one system.
Other boards, general string/signed writes on hardware, and reboot persistence
are not verified. Password unlock, authenticated variables, computed fields,
and unsupported vendor flows are rejected or kept read-only. Windows security
settings are never changed to load a driver.

## Credits

- [Lumin / discord.gg/lumin](https://discord.gg/lumin)
- [nlohmann/json](https://github.com/nlohmann/json), by Niels Lohmann — MIT

LuminAMI is MIT licensed. AMI's drivers are separate third-party files, and this
project is not affiliated with AMI.
