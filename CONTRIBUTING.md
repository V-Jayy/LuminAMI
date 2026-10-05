# Contributing

Build with `./build.ps1 -Test` before opening a pull request. Keep offline tests
offline; don't add a test that writes to someone's BIOS during a normal build.
Use the included `.clang-format` for C++ changes.

If you're reporting a board compatibility issue, include your board model, BIOS
version, driver hash, command, and error message. Remove serial numbers, raw
firmware contents, and anything private before attaching files.

The code is split by job:

- `windows.cpp`: platform checks and Windows firmware reads
- `ami.cpp` / `protocol.cpp`: AMI driver sessions and packet layouts
- `hii.cpp`: HII/IFR decoding
- `script.cpp` / `value.cpp`: settings text and typed values
- `engine.cpp` / `transaction.cpp`: plans, imports, readback, rollback, restore
- `main.cpp`: command parsing and JSON output

For questions, join [discord.gg/lumin](https://discord.gg/lumin).
