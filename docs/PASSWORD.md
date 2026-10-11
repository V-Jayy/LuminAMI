# BIOS administrator password

LuminAMI accepts the existing BIOS administrator password through `--ask-password`
(a hidden console prompt) or `--password-stdin` (the first line of redirected
input). Leave both options absent on an unprotected board. Command-line password
values, including SCEWIN `/CPWD`, are refused without echoing them.

```powershell
LuminAMI -e BIOSSettings.txt --ask-password
LuminAMI -i BIOSSettings.txt --ask-password
```

The input limit is 64 printable ASCII characters. Spaces are preserved. Invalid
input is refused before loading a driver. BIOS firmware may impose a shorter
limit. An empty console response skips authentication; an empty redirected line
is refused to catch missing script input. The helper scripts use the hidden
prompt so the password does not pass through PowerShell.

## Authentication lifecycle

An ordinary protected read or write first runs without authentication. Only AMI
status `0x9A` requests the supplied password. There is at most one submission per
command; after acceptance the refused operation is retried once. A rejected
password or firmware lockout stops the command. There is no automatic retry or
password guessing. AMD Variable Protection remains a separate write restriction.

The authentication transport was reconstructed by static analysis of the
SCEWIN_64 executable (SHA-256
`2d78e7bb62fcab44c54fe853f51e8836ada45ea374027423bcb6d7a5db46502b`). It uses
the existing AMI SMI SetVariable function `0x300`, attributes `7`, control name
`$SETUPPASSWD`, and GUID `5855ce1b-fb8e-47e4-bc1a-39ecaa0c96cf`. The payload is
UTF-16 with its terminating null. This control request authenticates access; it
does not set, change, clear or reset the BIOS administrator password.

After a successful submission, a zero-capacity GetVariable checks that firmware
consumed the control request. `0x8E` (not found) confirms consumption. If the
variable instead exists (`0` or `0x85`), firmware has stored the credential rather
than authenticated it: LuminAMI deletes only that control variable and refuses
the operation as unsupported. Other responses fail closed.

At the end of the operation, including failure and rollback, the same control
variable receives a **single zero byte** to re-lock access. This differs from the
zero-length deletion used for unsupported firmware. Cleanup failures are reported
and require restarting before continuing. Ordinary settings cannot be deleted
through this exception. Restore shares one authenticated session across its
capture, comparison, writes and readback.

The traced firmware responses are:

| Status | Meaning |
| --- | --- |
| `0x9A` | Password rejected |
| `0x8F` | Retry limit exceeded |
| `0x86` | No administrator password configured |
| `0x82` | Password format rejected |

These responses do not supply a remaining-attempt count. Reports leave that
field null rather than inventing one. Reports contain outcome metadata only.

## Handling and compatibility

LuminAMI keeps credentials in temporary buffers, clears the input, request and
shared driver buffers after use, and excludes the password from files, journals,
reports and diagnostics. The authentication control request above is the only
credential-bearing write. On unsupported firmware, that request can briefly be
stored as a variable before the targeted deletion; a cleanup failure is an error.

The transport is shared by supported AMI driver paths and is not restricted to
one motherboard model. This does **not** establish compatibility with every BIOS:
OEM firmware can change the protocol, character limits or protection policy.
Protected hardware has not yet been exercised for this implementation.

Offline tests check exact packets, firmware responses, one submission, unsupported
control cleanup, failure paths, re-locking, transaction rollback and secret
redaction. The normal build tests load no driver and write no firmware. A real
board check should start with an export, first without a password and then with
the hidden prompt if needed. Check a known correct password in BIOS setup before
submitting it; rejected authentication is never retried automatically.
