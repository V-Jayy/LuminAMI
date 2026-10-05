# LuminAMI

Export your AMI BIOS settings, edit the text file, and import them back.
Made by **Jayy and Billz**, from [Lumin / discord.gg/lumin](https://discord.gg/lumin).
Windows x64, C++20.

## Install

Paste this into PowerShell. It downloads the latest GitHub release, checks its
SHA256, installs to your user folder, adds LuminAMI to PATH, and extracts the
provided drivers. It keeps any custom driver you've already selected.

```powershell
irm https://raw.githubusercontent.com/V-Jayy/LuminAMI/main/install.ps1 | iex
```

Or download the ZIP from [releases](https://github.com/V-Jayy/LuminAMI/releases/latest).
Run `LuminAMI.cmd` for the menu or `InstallDrivers.cmd` to choose a driver.
Open a new terminal after installing. To update later: `LuminAMI install`.
That command starts the installer in the background; check the log path it prints.

## Use

Open an **administrator terminal** for live export/import:

```powershell
LuminAMI -e BIOSSettings.txt
notepad BIOSSettings.txt
LuminAMI -i BIOSSettings.txt --plan
LuminAMI -i BIOSSettings.txt
```

Move the `*` to your chosen option, or edit the `Value` for numeric/string
settings. Keep `BIOSSettings.txt.capture` with the file; imports need that backup.
`--plan` only checks changes. `-i` applies them, reads them back, and saves a
unique journal. Existing export files are preserved.

```powershell
# Provided drivers, headless export, or your own supported driver path.
LuminAMI install-drivers
LuminAMI -e backup.txt --non-interactive
LuminAMI use-driver --driver "C:\Drivers\amigendrv64.sys"
LuminAMI driver-status

# Help and advanced commands.
LuminAMI -h
LuminAMI help
LuminAMI ami help
LuminAMI ami import --capture BIOSSettings.txt.capture --script BIOSSettings.txt --output plan.json
```

Your driver choice is saved and reused from any folder. Export offers setup if
none is saved. Both short commands accept `--driver PATH`, `--install-drivers`,
`--non-interactive`, and `--capture DIR` for a moved/renamed settings file.
`-p <password>` is reserved for your existing BIOS password; authentication isn't
supported yet, so using it returns an error without logging the password.

BIOS writes can leave a system unable to boot. Know your board's recovery process.
Read compatibility and write testing are limited; see [compatibility](docs/COMPATIBILITY.md).
Use LuminAMI exports with their matching captures, not SCEWIN files.
[Commands](docs/COMMANDS.md) · [Editing](docs/SETTINGS.md) · [Drivers](docs/DRIVERS.md)

## Export benchmark

![LuminAMI vs SCEWIN export benchmark](docs/benchmarks/export.png)

On an MSI PRO Z790-P WIFI / i9-14900KF, BIOS A.AJ: **1.01 seconds vs 9.91 seconds**
median export time, about **9.8x faster** than SCEWIN 5.05.01.0002. Ten runs each,
alternating order, warm-ups excluded. LuminAMI used 0.94 CPU-seconds vs 9.13;
peak resident RAM was higher at 64.5 MiB vs 38.9 MiB.

LuminAMI reads HII and unique varstores into a snapshot, then resolves settings
from that data. Most of the measured CPU-time difference was in kernel work.
That supports the benefit of keeping firmware reads out of the settings loop;
it does not prove SCEWIN's internal call count or guarantee this speed on every board.

Unchanged full-profile imports measured 0.95 vs 9.58 seconds. LuminAMI skips
unchanged writes; this is not a changed-settings write benchmark. The final
import used the starting profile, and all 34 captured varstores matched afterward.
[Full results, resource counters, and reproducible commands](docs/BENCHMARK.md).

## Build and credits

With Visual Studio 2022 C++ tools and the Windows SDK:

```powershell
.\build.ps1 -Test
```

Created by **Jayy and Billz**. Community and support: [our Lumin Discord](https://discord.gg/lumin).
JSON: [nlohmann/json](https://github.com/nlohmann/json), by Niels Lohmann (MIT).
Original signed drivers: **American Megatrends (AMI)**;
[driver provenance and credits](drivers/README.md).
LuminAMI's source is MIT licensed; the third-party drivers aren't relicensed.
LuminAMI is not affiliated with AMI.
