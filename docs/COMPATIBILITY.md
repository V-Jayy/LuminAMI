# Firmware compatibility

LuminAMI targets the AMI HII/NVRAM interfaces used by the supported AMI drivers.
Motherboard branding alone does not identify those interfaces. There is no
ASUS, MSI, ASRock, Gigabyte, CPU-vendor, or motherboard-model allowlist.
Compatibility is established by driver negotiation and successful firmware reads.
Universal support for all motherboards is not established: non-AMI firmware,
vendor-specific interfaces, unavailable HII data, unsupported IFR forms, and
Windows driver policy can prevent export or import.

## Transport selection

| Firmware capabilities | Behavior |
| --- | --- |
| WSMT fixed-buffer protections advertised | Negotiate through the supported `amigendrv64.sys`; the driver owns the SMI context |
| WSMT protections with no Windows-visible UEFI ACPI table | Use the same checked negotiation; the missing table is not a rejection |
| Recognized AMI UEFI table with a context address | Require the negotiated context address to match |
| No fixed-buffer protections advertised | Use the existing checked AMI allocation/SMI transport |
| Negotiation failure, inconsistent buffers, invalid SMI port, blocked driver | Stop and return the error; never disable Windows security or bypass WSMT |

The JSON capture manifest records `platform.ami_session`, and live export
reports include `transport`: the selected mode, driver version, requested buffer
capacity, and successful negotiation. `probe` alone does not load a driver or
prove compatibility. Use a live export into a new backup folder to establish
read compatibility on each machine.

## Verified read-only export

On October 4, 2026, local testing used an ASUS TUF GAMING B650E-E WIFI with
American Megatrends BIOS 0215, WSMT flags `7`, and no Windows-visible UEFI ACPI
table. The supported `amigendrv64.sys` matched the hash in DRIVERS.md.

The supplied SCEWIN_64.exe identifies itself as AMISCE 5.05.01.0002. Its read-only
export succeeded. LuminAMI's corrected export also succeeded with exit code `0`:

- 1,100 exported LuminAMI settings, with no script parsing issues.
- All 1,080 settings parsed from the SCEWIN export had a matching LuminAMI
  question with the same name, offset, width, and current value.
- Both exports produced identical raw HII data (825,284 bytes, SHA256
  `889d7100cd7611607c2049d34abc5f05171eff38ee50741426b2d1dadb34f209`).
- No BIOS settings were written. Dynamic varstores absent from NVRAM were
  recorded as unavailable, rather than misreported as a transport failure.

The settings text formats and token identities differ intentionally; use the
LuminAMI settings file with its matching capture, not a SCEWIN file for live import.
Export comparison does not validate BIOS writes, reboot persistence, every
setting type, or other motherboard firmware. Earlier NumLock validation remains
limited to the previously tested system.

## Regression coverage

Offline tests cover negotiation without a published ACPI context, matching and
conflicting published contexts, opaque template state, truncated packets, port
and capacity mismatches, null/wrapping addresses, and inconsistent buffer
pointers. The normal build tests never load a driver or write firmware.
Script tests also cover firmware option labels containing `=`, including
`UCLK=MEMCLK/2`: parsing, unchanged import planning, and an offline selection edit.

For a new compatibility report, include the board model, BIOS version, driver
hash, `probe` output, command, and failure report. Keep raw captures and serial
numbers private. A successful export is evidence of read compatibility on that
firmware version, not evidence that a live import is safe or supported.
