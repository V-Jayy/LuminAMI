# LuminAMI v0.2.3

Exports now show `latency_ms` directly in the JSON when you run
`LuminAMI -e BIOSSettings.txt`. The advanced export command and live report files
include it too. It's the export duration in milliseconds, measured after driver
selection/setup through capture, file writes, and cleanup.

The README also has a Lumin-colored chart of scheduling pauses from the saved
benchmark: 58 ms vs 403 ms median longest pause per export. Those sampler gaps
aren't mouse/input latency.

Created by Jayy and Billz, from [our Lumin Discord](https://discord.gg/lumin).
