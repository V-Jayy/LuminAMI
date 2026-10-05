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

The implemented WSMT path is AMI v2 with command `0xD9`; it requires the listed
`amigendrv64.sys`. Other protected interfaces are rejected. Unprotected transport
also checks driver version, mapped buffer bounds, and the ACPI SMI port.

If Windows blocks the driver, the command reports the Windows error and stops.
LuminAMI does not change Secure Boot, Memory Integrity, or driver security policy.
Driver acceptance does not establish compatibility with every AMI BIOS.
