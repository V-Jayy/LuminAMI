# BIOS administrator password

Some AMI Aptio boards protect their setup variables with the BIOS **administrator
(supervisor) password** you set in BIOS setup. On such a board the firmware can
refuse a read or write with `EFI_SECURITY_VIOLATION`, which AMI's driver reports
as status `154` (`0x9A`). AMI's own utility, SCE, deals with this by letting you
supply the current administrator password (its `/cpwd` option).

LuminAMI can take that password from you, **optionally**, and uses it at most
once. It never sets, changes, clears or resets a BIOS password, on any board.

## Status: what works, and the one piece that does not yet

| Part | State |
| --- | --- |
| Recognising the firmware's "password needed" refusal on reads and writes | Done, same message text as before |
| Taking the password safely (`--ask-password`, `--password-stdin`) | Done |
| One attempt per command, never retried, never reused | Done, tested against a simulated firmware |
| Reporting a wrong password, remaining attempts and lockout | Done, tested against a simulated firmware |
| Keeping the password out of every output, file and error | Done, tested |
| **Sending the password to real AMI firmware** | **Not implemented.** LuminAMI says so and sends nothing |

The last row is deliberate. AMI's public documentation says *that* SCE accepts
the administrator password; it does not publish the request that carries it to
the firmware, and LuminAMI's own research (HII, variable get/set/next, the SMI
transport) does not contain it. This project does not guess at firmware byte
formats: a wrong guess could spend one of the board's limited password attempts
or leave the firmware in a state nobody has studied. Until that request is
established from sources the project may use, `--ask-password` and
`--password-stdin` are accepted and validated, and a board that needs the
password gets a clear message instead of a submission:

```
... A BIOS administrator password was supplied, but this build of LuminAMI cannot
submit one to firmware yet (...); nothing was sent. See docs/PASSWORD.md.
```

`password_submission_supported()` in the library reports this honestly, so a
program that embeds LuminAMI (Lumin App does) can decide whether to ask for a
password at all.

This is **implemented from AMI's public documentation and the project's own
research; not yet confirmed on password-protected hardware.**

## Using it

You only need this if LuminAMI stops with a refusal like
`AMI SetVariable rejected for ... (EFI_SECURITY_VIOLATION, firmware status 154 / 0x9A)`
or `AMI GetVariable failed for ... (firmware status 154)` on a board where you set an
administrator password, and AMD Variable Protection is not the cause
(see [compatibility](COMPATIBILITY.md#amd-firmware-write-protection)).

```powershell
# Hidden prompt at a terminal (recommended)
LuminAMI -e BIOSSettings.txt --ask-password
LuminAMI -i BIOSSettings.txt --ask-password

# From a script: the first line of redirected input
'the password' | LuminAMI -e BIOSSettings.txt --password-stdin
```

The shortcuts take `-AskPassword`: `Export.cmd -AskPassword`,
`Import.cmd -Apply -AskPassword`, `Restore.cmd -AskPassword`, `LuminAMI.cmd -AskPassword`.
They pass `--ask-password` to LuminAMI, which shows the hidden prompt itself, so
the password never passes through PowerShell.

The options work on `export` (live), `capture-ami`, `import` (live), `test-import`
and `restore`. Offline commands read no firmware and refuse them.

## Where the password may come from

| Channel | Used? | Why |
| --- | --- | --- |
| Hidden console prompt (`--ask-password`) | Yes | Echo is off; the characters go straight into a buffer LuminAMI owns |
| Redirected standard input (`--password-stdin`) | Yes | Not visible in the process list, the environment or a log; first line only |
| Command-line value (`-p`, `/CPWD`) | **Refused** | Any program can read another process's command line |
| Environment variable | **Not offered** | Inherited by every child process and copied by diagnostics tools |

An unusable password is refused before a driver is selected or loaded, so a
typo costs nothing. LuminAMI submits only 1 to 64 printable ASCII characters
(space to `~`). The firmware applies its own, usually shorter, limit; a password
inside the 64-character bound that the firmware rejects is reported as rejected,
once. Empty input is the same as no password.

## What LuminAMI promises

- **Optional and absent by default.** Without `--ask-password` / `--password-stdin`
  no code in this feature runs; output, journals and exit codes are unchanged.
- **One attempt.** The password is submitted at most once per command, only after
  the firmware itself refused something, and the refused operation is retried
  exactly once. A wrong password is reported immediately and is never retried,
  not by LuminAMI and not by a saved copy elsewhere. A restore, which reads and
  then writes, shares that one attempt.
- **No NVRAM writes to check it.** Verification is the firmware's own answer to
  the submission. LuminAMI writes nothing to check that a password is right.
- **Never sets, changes, clears or resets a password.** There is no such command.
- **Never shown.** The password is held in a buffer that is overwritten as soon as
  the one submission has been made (or the command ends), is never copied into a
  string that outlives it, and is never written to the console, JSON output,
  `--report` files, journals, captures or error messages. If a transport's own
  text ever contained it, the text is cleaned before it can be printed. Reports
  say only whether a password was needed, submitted and accepted.
- **Memory.** A process cannot promise more than this about its own memory: the
  copy in RAM is short-lived and overwritten, but Windows may have paged other
  parts of the process, and a crash dump taken during that short window would
  contain it. LuminAMI does not write dumps.

## If the firmware says no

A rejected password stops the command and says so, with the firmware's own count
of remaining attempts when it gives one:

```
The firmware did not accept the BIOS administrator password (password rejected).
The firmware reports 2 attempt(s) remaining before it locks further tries. LuminAMI does not retry.
```

If the firmware reports its limit, the message says to restart the computer
before trying again. The JSON error report carries
`"password": {"supplied": true, "submitted": true, "accepted": false, "locked_out": false, "remaining_attempts": 2, ...}`
and never the password. Check the password in BIOS setup before trying again;
do not try variations.

## What was used to build this

- AMI's public documentation of SCE: a BIOS administrator password, when set, is
  supplied to the tool, which is the behaviour this models.
- LuminAMI's own research notes and code: AMI status `0x9A` as the security
  violation on `GetVariable` and `SetVariable`, the variable get/set primitives,
  and the transaction, journal and rollback engine.
- The tests use a **simulated firmware** (`tests/password_tests.cpp`) that refuses
  protected variables until the right password is submitted, counts wrong
  attempts and can lock out. It checks behaviour, not firmware formats.

No new analysis of SCEWIN or any other third-party binary was done for this
feature, and none of its bytes, offsets or request layouts are used.

## Not verified without a real password-protected board

- That a protected board answers reads or writes of setup variables with status
  `0x9A` rather than another status, and which variables it protects.
- The request that submits the administrator password, how the firmware answers,
  and whether it reports remaining attempts or a lockout. **This is the missing piece.**
- Whether the unlocked state lasts for the driver session, one boot, or one request.
- The firmware's password length and character limits on real boards.
- That a wrong password costs exactly one firmware attempt.

## The first check on a password-protected board

An export, which only reads:

```powershell
LuminAMI -e BIOSSettings.txt
```

Run it with **no** password first. If it succeeds, nothing is protected for reading
and no password is needed to export. If it stops with a status `154` / `0x9A`
refusal, that confirms the board protects reads, which is worth reporting along
with the board model and BIOS version (see
[compatibility](COMPATIBILITY.md#regression-coverage) for what to include; leave out
captures and serial numbers). Do not try imports while testing.
