# Editing settings

Always start with your own LuminAMI export and keep its matching capture folder.
Don't copy another board's settings file or swap captures between exports.

For an option list, move the `*` to the selected option. Keep exactly one star:

```text
Options = [00]Disabled
          *[01]Enabled
```

Numeric fields use `Value = <123>` for decimal values. String fields use a quoted
JSON string, such as `Value = "Example"`. Preserve quotes and escapes. Save as
UTF-8 or the original ANSI encoding, not UTF-16.

Leave `Token`, `Offset`, `Width`, `Signed`, question names, and the HII checksum
alone. A setting with all its lines commented out is read-only. Don't uncomment
it, remove questions, or edit `Dupes.txt` as your import file. Duplicate entries
can refer to the same physical field; conflicting changes are rejected.

You can also edit by token without touching the file structure:

```powershell
.\LuminAMI.exe edit --script settings.txt --output edited.txt --token 0x2B --value 0
.\LuminAMI.exe diff --before settings.txt --after edited.txt
```

Choose the token and supported value from your export. Tokens are specific to
LuminAMI and the captured HII ordering. Numeric `edit --value` accepts decimal
and `0x` hexadecimal; string fields accept text.

`Import.cmd` first compares your edited file with `OriginalSettings.txt`, then
validates it against the capture and saves a plan. Without `-Apply` it performs
no firmware writes. A live import checks the current firmware against the
baseline again; a stale capture should fail instead of overwriting newer values.

After a successful import, make a fresh export into a new workspace before your
next set of edits. Keep previous backups and journals for recovery.
