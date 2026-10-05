# AMI driver requirements

LuminAMI needs an AMI kernel driver for live capture, export, import, and restore.
Both supported files are embedded in the executable. Setup works offline and
doesn't load a kernel driver, create a service, or write BIOS settings.

## Install the provided drivers

Run `InstallDrivers.cmd` and choose the provided drivers, or use:

```powershell
.\InstallDrivers.cmd -NonInteractive
LuminAMI install-drivers
```

The native command never prompts. Both methods extract to
`%LOCALAPPDATA%\LuminAMI\drivers` and select `amigendrv64.sys` by default.
Use `InstallDrivers.cmd -Directory DIR -NonInteractive` or
`LuminAMI install-drivers --directory DIR` to extract somewhere else.
Files with matching hashes are reused. A mismatched existing file is rejected
without being overwritten.

## Use a custom path

```powershell
.\InstallDrivers.cmd -Driver "C:\MyDrivers\amigendrv64.sys" -NonInteractive
LuminAMI use-driver --driver "C:\MyDrivers\amigendrv64.sys"
```

Use the correct filename for your file; renaming a different driver won't make
it compatible. The custom file is verified and its absolute path is saved.
It isn't copied. You can also pass `-Driver PATH` to a live script, or
`--driver PATH` to a live CLI command; that choice is then saved for later use.

Only these exact SHA256 hashes are accepted:

| File | SHA256 |
| --- | --- |
| `amigendrv64.sys` | `ffc72f0bde21ba20aa97bee99d9e96870e5aa40cce9884e44c612757f939494f` |
| `amifldrv64.sys` | `e7cbfb16261de1c7f009431d374d90e9eb049ba78246e38bc4c8b9e06f324b6f` |

Check a custom file before using it:

```powershell
Get-FileHash .\drivers\amigendrv64.sys -Algorithm SHA256
```

## Saved choice and automation

The choice is stored in `%LOCALAPPDATA%\LuminAMI\driver.json`, shared by the
scripts and native executable. It survives a terminal restart and works from
any directory. `LuminAMI driver-status` reports the path and verification result.
The file is hashed again on each lookup and before loading. Missing or changed
files fail instead of silently switching to another driver.

For a service account or an isolated configuration, set `LUMINAMI_CONFIG_DIR`
to an absolute directory. That replaces the default state directory.

`Export.cmd` offers provided/custom setup when no valid choice is saved and
input is interactive. A native export to a new capture does the same.
Use `-NonInteractive` in scripts or `--non-interactive` in native live commands
to prohibit prompts. Redirected input also suppresses setup prompts. Without
a saved or supplied driver these commands fail with setup instructions.
`-InstallDrivers` / `--install-drivers` explicitly chooses provided drivers
without prompting. Supplying a custom path at the same time is rejected.

Driver selection never makes an offline import live. An advanced `ami import` only
writes when `--journal` or `--driver` requests the live path; the journal is
always required. Script imports require `-Apply`. The short `-i FILE` command
requests a live import and creates a unique journal automatically; add `--plan`
to validate offline instead.

## Source files

[`drivers`](../drivers/README.md) contains the raw `.bin` payloads. The Windows
resource compiler embeds those bytes in `LuminAMI.exe`; the installer writes
them back as `.sys` files after checking their SHA256 values. There is no
download service or Dropbox dependency.

## Live operation requirements

Run live commands from an administrator terminal. Close other AMI utilities
first: LuminAMI refuses to use an existing `GENERICDRV` service. It creates its
own temporary service and removes that service when the session ends normally.

Protected WSMT transport requires the listed `amigendrv64.sys`. LuminAMI asks
that hash-verified driver to negotiate the firmware's fixed communication buffer
and manage its SMI context, as the supplied AMISCE 5.05.01.0002 driver does.
An AMI UEFI ACPI table exposed through Windows is optional. Its absence alone
does not mean the board is unsupported. When a recognized AMI table publishes
a context address, it must match the driver's negotiated address.

Negotiation must succeed and return consistent, mapped, non-overflowing buffers.
Failure stops the command; it does not fall back to unprotected allocation on
a WSMT-protected system. Unprotected transport still checks driver version,
mapped buffer bounds, and the ACPI SMI port.

If Windows blocks the driver, the command reports the Windows error and stops.
LuminAMI does not change Secure Boot, Memory Integrity, or driver security policy.
Driver acceptance does not establish compatibility with every AMI BIOS.

See [compatibility and hardware validation](COMPATIBILITY.md) for the supported
scope and the locally verified export results.
