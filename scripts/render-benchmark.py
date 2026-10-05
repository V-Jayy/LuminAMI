"""Render saved benchmark measurements. Never runs firmware commands."""
import argparse
import json
import statistics
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

BACKGROUND = "#070510"
TEXT = "#ece6f5"
SECONDARY = "#8a7aad"
GRID = "#1e1735"
LUMIN = "#b07cff"
REFERENCE = "#4a3d6e"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("results", type=Path)
    parser.add_argument("--output", type=Path, default=Path("docs/benchmarks"))
    args = parser.parse_args()
    source = args.results
    metadata = json.loads((source / "metadata.json").read_text(encoding="utf-8-sig"))
    runs = json.loads((source / "runs.json").read_text(encoding="utf-8-sig"))
    verification = json.loads((source / "final-verification.json").read_text(encoding="utf-8-sig"))
    completion = json.loads((source / "completion.json").read_text(encoding="utf-8-sig"))
    if not completion["ok"] or not verification["verified"]:
        raise SystemExit("Publish only a completed benchmark with final settings verified.")
    measured = [r for r in runs if r["measured"]]
    plt.rcParams.update({"figure.facecolor": BACKGROUND, "axes.facecolor": BACKGROUND,
                         "text.color": TEXT, "axes.labelcolor": SECONDARY,
                         "xtick.color": SECONDARY, "ytick.color": TEXT})
    args.output.mkdir(parents=True, exist_ok=True)
    fields = ["wall_ms", "cpu_ms", "kernel_cpu_ms", "user_cpu_ms", "peak_working_set_mib",
              "peak_commit_mib", "read_mib", "write_mib", "other_io_mib", "system_busy_pct",
              "max_sampler_gap_ms", "p95_sampler_gap_ms"]
    summary = {}
    for operation in ("export", "import"):
        groups = {}
        for tool in ("LuminAMI", "SCEWIN"):
            rows = [r for r in measured if r["operation"] == operation and r["tool"] == tool]
            if not rows:
                continue
            if len(rows) != metadata["runs_per_tool"] or any(r["exit_code"] for r in rows):
                raise SystemExit("Incomplete or failed measured runs.")
            groups[tool] = {"runs": len(rows), "metrics": {
                field: {"median": statistics.median(r[field] for r in rows),
                        "min": min(r[field] for r in rows), "max": max(r[field] for r in rows)}
                for field in fields}}
        if not groups:
            continue
        summary[operation] = groups
        fig, axes = plt.subplots(1, 3, figsize=(12, 3.3), layout="constrained")
        labels = [("wall_ms", "Elapsed time", "seconds", .001),
                  ("cpu_ms", "Process CPU time", "CPU-seconds", .001),
                  ("peak_working_set_mib", "Peak resident memory", "MiB", 1)]
        for ax, (field, title, unit, scale) in zip(axes, labels):
            for y, tool in enumerate(("LuminAMI", "SCEWIN")):
                values = [r[field] * scale for r in measured if r["operation"] == operation and r["tool"] == tool]
                median = statistics.median(values)
                color = LUMIN if tool == "LuminAMI" else REFERENCE
                ax.barh(y, median, height=.48, color=color)
                offsets = [(i - (len(values) - 1) / 2) * .022 for i in range(len(values))]
                ax.scatter(values, [y + offset for offset in offsets], color=TEXT, s=11, zorder=3)
                axis_max = max(r[field] * scale for r in measured if r["operation"] == operation)
                ax.text(max(values) + axis_max * .025, y, f"{median:.2f}" if scale == .001 else f"{median:.1f}",
                        va="center", fontsize=10, weight="bold")
            ax.set_yticks([0, 1], ["LuminAMI", "SCEWIN"])
            ax.invert_yaxis()
            ax.set_xlabel(unit, fontsize=10)
            ax.set_title(title, loc="left", fontsize=12, weight="bold", pad=12)
            ax.set_xlim(0, max(r[field] * scale for r in measured if r["operation"] == operation) * 1.2)
            ax.spines[["top", "right", "left"]].set_visible(False)
            ax.spines["bottom"].set_color("#2d2450")
            ax.tick_params(axis="y", length=0)
            ax.grid(axis="x", color=GRID, linewidth=.6)
            ax.set_axisbelow(True)
        heading = "BIOS export" if operation == "export" else "Unchanged settings import"
        fig.suptitle(f"{heading}  |  {metadata['runs_per_tool']} runs per tool", fontsize=15, weight="bold", x=.01, ha="left")
        fig.supxlabel(f"{metadata['board']} · {metadata['cpu'][0].strip()} · BIOS {metadata['bios']}\n"
                      "Bars: median. Dots: individual runs. Lower values are better. One machine; warm-ups excluded.",
                      fontsize=9, color=SECONDARY)
        fig.savefig(args.output / f"{operation}.png", dpi=180, facecolor=BACKGROUND)
        plt.close(fig)
    if "export" in summary:
        fig, ax = plt.subplots(figsize=(7, 2.8), layout="constrained")
        export_rows = [r for r in measured if r["operation"] == "export"]
        maximum = max(r["max_sampler_gap_ms"] for r in export_rows)
        for y, tool in enumerate(("LuminAMI", "SCEWIN")):
            values = [r["max_sampler_gap_ms"] for r in export_rows if r["tool"] == tool]
            median = statistics.median(values)
            ax.barh(y, median, height=.48, color=LUMIN if tool == "LuminAMI" else REFERENCE)
            ax.scatter(values, [y + (i - (len(values) - 1) / 2) * .022 for i in range(len(values))],
                       color=TEXT, s=14, zorder=3)
            ax.text(max(values) + maximum * .025, y, f"{median:.0f} ms", va="center", weight="bold")
        idle = max(metadata["idle_before"]["MaxSampleGapMs"], metadata["idle_after"]["MaxSampleGapMs"])
        ax.axvline(idle, color=SECONDARY, linestyle="--", linewidth=1)
        ax.set_yticks([0, 1], ["LuminAMI", "SCEWIN"])
        ax.invert_yaxis()
        ax.set_xlim(0, maximum * 1.2)
        ax.set_xlabel("Longest scheduling gap per export (ms)", fontsize=10)
        ax.set_title("Export scheduling pauses", loc="left", fontsize=14, weight="bold", pad=12)
        ax.spines[["top", "right", "left"]].set_visible(False)
        ax.spines["bottom"].set_color("#2d2450")
        ax.tick_params(axis="y", length=0)
        ax.grid(axis="x", color=GRID, linewidth=.6)
        ax.set_axisbelow(True)
        fig.supxlabel(f"Bars: median. Dots: {metadata['runs_per_tool']} runs per tool. Dashed: idle peak {idle:.1f} ms.\n"
                      f"{metadata['sampler_interval_ms']} ms observer; scheduling gaps, not mouse/input latency. One machine.",
                      fontsize=9, color=SECONDARY)
        fig.savefig(args.output / "export-latency.png", dpi=180, facecolor=BACKGROUND)
        plt.close(fig)
    public = {"metadata": metadata, "measured_runs": measured, "summary": summary,
              "final_verification": verification,
              "notes": ["Import input was an unmodified full current-settings file. LuminAMI skipped unchanged writes.",
                        "Memory is the last observed OS-reported peak, sampled every 10 ms; late peaks may be missed.",
                        "I/O includes process disk/device operations and buffered I/O, not physical disk traffic.",
                        "System CPU includes background work and cannot be attributed solely to either tool.",
                        "Sampler gaps measure scheduling delays, not input latency or isolated SMI duration.",
                        "No altered settings profile or reboot-persistence benchmark was performed."]}
    validation = source / "export-validation.json"
    if validation.exists():
        public["export_validation"] = json.loads(validation.read_text(encoding="utf-8-sig"))
    (args.output / "results.json").write_text(json.dumps(public, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
