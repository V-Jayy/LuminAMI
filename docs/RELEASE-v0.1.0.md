# LuminAMI v0.1.0

First public release of LuminAMI.

Export your AMI BIOS settings, edit the text file, and import it back. There's
also a command menu if you'd rather use that.

Included:

- Windows x64 executable with no separate C++ runtime install
- Export, import, and restore scripts
- Inspect, edit-by-token, diff, and offline import planning
- SCEWIN-style `/O`, `/I`, `/S`, and `/SD` aliases
- Captures, transaction journals, readback, and rollback attempts
- Command reference and driver setup instructions

Download `LuminAMI-v0.1.0-windows-x64.zip`, extract it, and run `LuminAMI.cmd`.
AMI drivers aren't included. Live operations need administrator access and a
driver matching one of the documented hashes.

This is an early release. Previous hardware tests covered NumLock
write/readback/restore on one system. Other boards and reboot persistence aren't
verified. Keep your full backup folder and know your board's recovery process
before changing BIOS settings.

Made by [Lumin](https://discord.gg/lumin). Help, feedback, and board results:
[discord.gg/lumin](https://discord.gg/lumin).

MIT licensed. Includes nlohmann/json under its MIT license.
