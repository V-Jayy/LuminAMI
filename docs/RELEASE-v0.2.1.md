# LuminAMI v0.2.1

Fix the GitHub install/update checksum check in Windows PowerShell 5 background
processes. It now uses .NET SHA256 directly, so it works when `Get-FileHash`
isn't available through the inherited module path. Installer errors use the
same UTF-8 encoding as the update log.

Includes the v0.2.0 short export/import commands, offline driver installer,
saved custom drivers, updated BIOS export transport, and GitHub installation.
Made by Jayy and Billz, from [Lumin](https://discord.gg/lumin).

BIOS password authentication remains unsupported; `-p <password>` returns an
error before driver setup or writes. Review the README and compatibility notes
before importing.
