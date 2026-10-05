# AMI driver requirements

LuminAMI needs an AMI kernel driver for live capture, export, import, and restore.
Get the driver from a source you're allowed to use, such as your board vendor's
AMI utility package. This repository and its releases don't distribute drivers.
Renaming a different driver won't make it work.

Only these exact SHA256 hashes are accepted:

| File | SHA256 |
| --- | --- |
| `amigendrv64.sys` | `ffc72f0bde21ba20aa97bee99d9e96870e5aa40cce9884e44c612757f939494f` |
| `amifldrv64.sys` | `e7cbfb16261de1c7f009431d374d90e9eb049ba78246e38bc4c8b9e06f324b6f` |

Check yours before using it:

```powershell
Get-FileHash .\drivers\amigendrv64.sys -Algorithm SHA256
```

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
