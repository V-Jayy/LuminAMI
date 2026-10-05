# LuminAMI v0.2.2

Add a reproducible Windows benchmark script and real export/import charts to the
README. Ten runs per tool on an MSI PRO Z790-P WIFI / i9-14900KF, BIOS A.AJ:

- Export median: LuminAMI 1.014 s, SCEWIN 9.906 s. Process CPU time: 0.938 vs 9.133 s.
- Peak resident export RAM: 64.5 vs 38.9 MiB. The chart includes that tradeoff.
- Unchanged full-profile import median: 0.951 vs 9.576 s. LuminAMI skips unchanged
  writes; this does not measure applying a changed settings profile.
- Final starting-settings import completed. A fresh capture matched all 34
  starting varstores byte for byte and matched the HII identity.

Includes the measurement samples, hardware/software hashes, resource counters,
methodology, export-comparison limits, and an offline chart renderer. Raw BIOS
captures/settings and the reference SCEWIN binary are not included.

The core export/import engine is unchanged from v0.2.1. BIOS password authentication
remains unsupported. Created by Jayy and Billz, from [Lumin](https://discord.gg/lumin).
