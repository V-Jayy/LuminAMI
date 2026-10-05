# Provided AMI drivers

`amigendrv64.bin` and `amifldrv64.bin` are the original Windows driver files,
stored as raw binary bytes. They are not encoded, patched, or rebuilt.
`resources.rc` embeds them in the executable as Windows RCDATA resources.

The installer verifies the resource bytes and extracts them as
`amigendrv64.sys` and `amifldrv64.sys`. Original file bytes and signatures are
preserved. Installing files does not load them.

Credits: [American Megatrends (AMI)](https://www.ami.com/) created and signed
these drivers. The original files were supplied by the project maintainers;
their original download archive is not documented. AMI's
[Aptio utilities](https://www.ami.com/products/aptio-v/) describe the vendor's
firmware tools. That link is vendor information, not a claim about the archive
these particular files came from.

These are third-party AMI binaries. LuminAMI's MIT license covers its own source
and does not relicense these driver files. LuminAMI is not affiliated with AMI.

Hashes and usage: [driver setup](../docs/DRIVERS.md).
