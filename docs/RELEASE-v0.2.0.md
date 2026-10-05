# LuminAMI v0.2.0

Made by Jayy and Billz, from [Lumin](https://discord.gg/lumin).

- Short commands on PATH: `LuminAMI -e settings.txt`, `LuminAMI -i settings.txt`,
  and `LuminAMI -i settings.txt --plan`. Imports validate first and save unique journals.
- `LuminAMI -h` / `LuminAMI help` for quick help; `LuminAMI ami help` for advanced commands.
- Install directly from GitHub with the README's PowerShell command. Checks release
  SHA256, installs per user, and adds PATH. `LuminAMI install` starts an update.
- Original AMI driver bytes embedded in the executable. Offline installer,
  interactive provided/custom choice, and a saved hash-checked driver path.
- Updated WSMT driver negotiation and BIOS export compatibility, including option
  labels containing `=`. See the compatibility report for the verified read-only export.
- README shortened with copyable commands and credits to Jayy, Billz, our Lumin
  Discord, nlohmann/json, and AMI.

`-p <password>` is reserved, but BIOS password authentication is unsupported and
fails before driver setup or writes. No passwords are logged. Windows security
settings are never changed. Firmware writes and reboot persistence have limited
hardware coverage; review the compatibility notes before importing.

AMI drivers are third-party binaries, not covered by LuminAMI's MIT license.
