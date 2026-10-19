"""The fault-injection campaign's report generator (REQ-TEST-006; FTS-FI-001, FTS-TP-001 TC-036).

Reads one campaign's record, the directory build/fault_campaign writes:

- runs.csv, one row per run, its columns named by its header (tests/fault_injection/campaign_record.cpp);
- manifest.json, what was run, on what and with what;
- config.yaml, the copy of the configuration the runs used, whose SHA-256 the report names;

and rewrites only the generated blocks of FTS-FI-001, the text between `<!-- generated:NAME -->` and
`<!-- end generated -->`, and the report's three figures. The prose around the blocks (the method, the
predictions, the findings) is a person's and is never touched, so a rerun changes numbers only and
`git diff` shows which moved. Every number in a block is computed here from the record; none is typed
by hand.

    python -m analysis.fault_campaign build/campaign/<commit> --report docs/fault_injection_report.md --figures docs/img
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
import re
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable, Sequence

from analysis.campaign_stats import rule_of_three, summarize, wilson_interval, zero_bound_exact
from analysis.noise_profile import GRAVITY_MPS2, ISA_SEA_LEVEL_HPA, isa_slope_m_per_hpa

CHANNELS = ("baro", "imu", "gps")
CHANNEL_NAMES = {"baro": "barometer", "imu": "IMU", "gps": "GPS"}
BLOCK = re.compile(r"<!-- generated:(?P<name>[a-z0-9-]+) -->\n(?P<body>.*?)<!-- end generated -->", re.S)
BLOCK_START = re.compile(r"<!-- generated:([a-z0-9-]+) -->")
FIGURE_FILES = {"d03": "fi_d03_latency.png", "sweep": "fi_persistence_sweep.png",
                "uncovered": "fi_uncovered_error.png"}

# Section 5.3's "about 8.3 m per hPa": the standard atmosphere's slope at sea-level pressure.
ALTITUDE_PER_HPA_M = -isa_slope_m_per_hpa(ISA_SEA_LEVEL_HPA)

# The catalogue's draws behind Section 5.5's rates (tests/fault_injection/fault_catalogue.cpp): V07's
# upset lasts 2 to 6 cycles, and V11's and V12's second fault comes 0 to 10 cycles after the first.
V07_LENGTHS = range(2, 7)
GAPS = range(0, 11)

# The figures' colours: the reference palette's light surface, ink and chrome, and categorical slots 1
# and 2, which pass every check of the palette validator in light mode.
SURFACE = "#fcfcfb"
INK = "#0b0b0b"
INK_SECONDARY = "#52514e"
INK_MUTED = "#898781"
GRID = "#e1e0d9"
AXIS = "#c3c2b7"
SERIES = ("#2a78d6", "#eb6834")
FIGURE_STYLE = {
    "figure.facecolor": SURFACE, "axes.facecolor": SURFACE, "savefig.facecolor": SURFACE,
    "axes.edgecolor": AXIS, "axes.linewidth": 0.8, "axes.labelcolor": INK_SECONDARY,
    "axes.titlecolor": INK, "axes.titlesize": 10.5, "axes.titleweight": "bold", "axes.titlelocation": "left",
    "axes.spines.top": False, "axes.spines.right": False, "axes.axisbelow": True,
    "axes.grid": True, "axes.grid.axis": "y", "grid.color": GRID, "grid.linewidth": 0.8, "grid.linestyle": "-",
    "xtick.color": AXIS, "ytick.color": AXIS, "xtick.labelcolor": INK_MUTED, "ytick.labelcolor": INK_MUTED,
    "font.family": "sans-serif", "font.size": 9,
    "legend.frameon": False, "legend.fontsize": 8.5, "legend.labelcolor": INK_SECONDARY,
    "lines.linewidth": 1.5, "lines.solid_capstyle": "round", "lines.solid_joinstyle": "round",
}
DPI = 110


# ---------------------------------------------------------------- the record

@dataclass(frozen=True)
class Record:
    rows: list[dict[str, str]]
    manifest: dict
    config_sha256: str | None       # of the record's copy of the configuration, when it has one


def load_record(directory: str | Path) -> Record:
    directory = Path(directory)
    with open(directory / "runs.csv", newline="") as f:
        rows = list(csv.DictReader(f))
    manifest = json.loads((directory / "manifest.json").read_text())
    config = directory / "config.yaml"
    sha256 = hashlib.sha256(config.read_bytes()).hexdigest() if config.is_file() else None
    if len(rows) != manifest["runs"]:
        raise ValueError(f"{directory}: runs.csv has {len(rows):,} rows and the manifest {manifest['runs']:,} runs; "
                         "the record is incomplete")
    return Record(rows, manifest, sha256)


def whole(row: dict[str, str], column: str) -> int:
    return int(row[column])


def real(row: dict[str, str], column: str) -> float:
    return float(row[column])          # "nan", "-nan" and "inf" parse too


def yes(row: dict[str, str], column: str) -> bool:
    return row[column] == "1"


def cells_of(record: Record, cell_class: str) -> list[tuple[dict, list[dict[str, str]]]]:
    """Each cell of one class with its rows, in the catalogue's order."""
    rows_by_cell: dict[str, list[dict[str, str]]] = {}
    for row in record.rows:
        rows_by_cell.setdefault(row["cell"], []).append(row)
    return [(cell, rows_by_cell.get(cell["name"], [])) for cell in record.manifest["cells"]
            if cell["class"] == cell_class]


def rows_of_kind(record: Record, kind: str) -> list[dict[str, str]]:
    return [row for row in record.rows if row["kind"] == kind]


def lane_kinds(record: Record) -> dict[str, dict[int, list[dict[str, str]]]]:
    """Each lane kind's rows by persistence count."""
    out: dict[str, dict[int, list[dict[str, str]]]] = {}
    for cell, rows in cells_of(record, "LANE"):
        out.setdefault(cell["kind"], {})[cell["persistence"]] = rows
    return out


def lanes_altered(row: dict[str, str]) -> int:
    return len(row["lane_faults"].split(";")) if row["lane_faults"] else 0


# ---------------------------------------------------------------- formatting

def number(value: float, digits: int = 3) -> str:
    """A whole number with thousands separators, else `digits` significant figures."""
    if math.isnan(value):
        return "NaN"
    if math.isinf(value):
        return "+∞" if value > 0 else "−∞"
    if float(value).is_integer():
        return f"{int(value):,}"
    if abs(value) >= 10 ** digits:
        return f"{value:,.0f}"
    return f"{value:.{digits}g}"


def percent(share: float) -> str:
    """A share as a percentage to three significant figures, with more where three would round a share
    that is neither 0 nor 1 to 0% or 100%."""
    if share == 0.0:
        return "0%"
    if share == 1.0:
        return "100%"
    for digits in range(3, 10):
        text = f"{100.0 * share:.{digits}g}"
        if float(text) not in (0.0, 100.0):
            return text + "%"
    return f"{100.0 * share!r}%"


def bounded(k: int, n: int) -> str:
    """A count of runs with what it shows about the rate: zero carries its rule-of-three bound, any other
    count its share and 95% Wilson interval."""
    if n == 0:
        return "—"
    if k == 0:
        return f"0 (< {percent(rule_of_three(n))})"
    low, high = wilson_interval(k, n)
    return f"{k:,} ({percent(k / n)}; {percent(low)}–{percent(high)})"


def zero_note(ns: Iterable[int]) -> str:
    """The rule of three's exact value beside it, for the run counts a table holds."""
    exact = "; ".join(f"{percent(rule_of_three(n))} against {percent(zero_bound_exact(n))} exactly at {n:,} runs"
                      for n in sorted(set(ns)) if n > 0)
    return f"A count of zero in n runs bounds the rate below 3/n, the rule of three ({exact})."


def ratio(value: float) -> str:
    """A measured value over its prediction, to the third decimal, where 1 is the prediction exactly."""
    return f"{value:.3f}"


def spread(values: Sequence[float], fmt: Callable[[float], str] = number) -> str:
    """Median / 95th percentile / maximum by the nearest rank, or a dash for no values."""
    if not values:
        return "—"
    s = summarize(values)
    return f"{fmt(s.median)} / {fmt(s.p95)} / {fmt(s.maximum)}"


def distinct(values: Sequence[float], limit: int = 3) -> str:
    """The values seen with their counts, most frequent first, when there are few of them."""
    if not values:
        return "—"
    counts = Counter(values)
    if len(counts) > limit:
        return f"{len(counts)} values, {number(min(values))} to {number(max(values))}"
    ordered = sorted(counts.items(), key=lambda item: (-item[1], item[0]))
    return ", ".join(f"{number(value)} ({count:,})" for value, count in ordered)


def share_cell(k: int, n: int) -> str:
    """A pivot table's cell: the percentage of runs, its Wilson interval when it is neither 0 nor 100."""
    if n == 0:
        return "—"
    if k in (0, n):
        return "0" if k == 0 else "100"
    low, high = wilson_interval(k, n)
    return f"{100.0 * k / n:.1f} ({100.0 * low:.1f}–{100.0 * high:.1f})"


def table(header: Sequence[str], rows: Iterable[Sequence[str]]) -> str:
    def line(cells: Sequence[str]) -> str:
        return "| " + " | ".join(cell.replace("|", "\\|") for cell in cells) + " |"
    return "\n".join([line(header), "|" + "|".join("---" for _ in header) + "|", *(line(row) for row in rows)])


def as_predicted(rows: Sequence[dict[str, str]]) -> str:
    return f"{sum(yes(row, 'as_predicted') for row in rows):,}"


def image(links: dict[str, str], key: str, caption: str) -> str:
    return f"![{caption}]({links[key]})" if key in links else f"*{caption}: not drawn, its cells are not in this record.*"


# ---------------------------------------------------------------- Section 3: what was run

def consecutive(numbers: Sequence[int]) -> bool:
    return list(numbers) == list(range(numbers[0], numbers[0] + len(numbers)))


def block_run(record: Record) -> str:
    m, t = record.manifest, record.manifest["timing"]
    n = len(record.rows)
    gate = sum(yes(row, "as_predicted") for row in record.rows)
    written = sum(yes(row, "as_written") for row in record.rows)
    invalid = sum(not yes(row, "valid") for row in record.rows)
    if gate != m["as_predicted"] or written != m["as_written"]:
        raise ValueError("runs.csv and manifest.json count different verdicts; the record is incomplete")
    config = (f"`{m['config_path']}`, SHA-256 `{record.config_sha256}`" if record.config_sha256
              else f"`{m['config_path']}` (the record holds no copy of it)")
    facts = [
        f"- **Commit:** `{m['commit']}`",
        f"- **Platform:** {m['platform']}",
        f"- **Toolchain:** {m['toolchain']}",
        f"- **Configuration:** {config}",
        f"- **Timing:** {t['period_ms']} ms frames; timeouts of {t['i2c_timeout_ms']:,} ms (barometer and IMU) and "
        f"{t['uart_timeout_ms']:,} ms (GPS); a GPS fix every {t['gps_interval_ms']:,} ms; a stuck count of "
        f"{t['stuck_count']} and a recovery count of {t['recovery_count']}",
        f"- **Runs:** {n:,} in {len(m['cells'])} cells, {m['wall_seconds']:,.0f} s on one core; {invalid:,} invalid",
        f"- **As predicted, run by run (the gate):** {gate:,} of {n:,}",
        f"- **As Section 5 was written:** {written:,} of {n:,} (Section 7)",
    ]
    by_kind: dict[str, list[dict]] = {}
    for cell in m["cells"]:
        by_kind.setdefault(cell["kind"], []).append(cell)
    rows = []
    for kind, cells in by_kind.items():
        first, last = cells[0], cells[-1]
        numbers = [cell["cell"] for cell in cells]
        if len(cells) == 1:
            where = str(first["cell"])
        else:
            listed = f"{numbers[0]}–{numbers[-1]}" if consecutive(numbers) else ", ".join(map(str, numbers))
            where = f"{listed} (P = {', '.join(str(cell['persistence']) for cell in cells)})"
        ids = f"{first['first_run_id']:,}–{first['last_run_id']:,}"
        if len(cells) > 1:
            ids += f" to {last['first_run_id']:,}–{last['last_run_id']:,}"
        replicates = ", ".join(f"{r:,}" for r in sorted({cell["replicates"] for cell in cells}))
        rows.append((f"{kind} {first['title']}", where, replicates, ids))
    return "\n".join([
        *facts, "",
        "A run's id is its seed, `cell × 100,000 + replicate` (Section 4.3); `build/fault_campaign --seed <id>` "
        "rebuilds any run.", "",
        table(("Kind", "Cells", "Runs per cell", "Run ids"), rows),
    ])


# ---------------------------------------------------------------- Section 6.1: the detector's faults

def gps_latency_grid(manifest: dict) -> list[int]:
    """Section 5.1's D03 prediction on the operator's clock: the latency from the first suppressed fix
    plus the onset's place in the fix second, every frame of the second equally likely."""
    t, p = manifest["timing"], manifest["predictions"]
    return [p["gps_fix_latency_ms"] + k for k in range(0, t["gps_interval_ms"], t["period_ms"])]


def first_fix_at_or_after(t_ms: int, manifest: dict) -> int:
    interval = manifest["timing"]["gps_interval_ms"]
    return -(-t_ms // interval) * interval


def predicted_detection(kind: str, manifest: dict) -> tuple[str, str]:
    """Section 5.1's latency and recovery, from the formulas the manifest evaluated."""
    p, period = manifest["predictions"], manifest["timing"]["period_ms"]
    recovery = number(p["recovery_ms"])
    if kind == "D03":
        return (f"{spread(gps_latency_grid(manifest))}, uniform",
                f"{number(p['gps_recovery_min_ms'])} to {number(p['gps_recovery_max_ms'])}")
    if kind in ("D05", "D06", "D07"):
        return (f"{number(p['stuck_latency_ms'])}; {number(p['stuck_latency_ms'] - period)} after a repeat",
                f"{recovery}; {number(p['recovery_ms'] + period)} after a repeat")
    if kind in ("D08", "D09", "D10"):
        return "0", recovery
    return number(p["i2c_timeout_latency_ms"]), recovery


def block_detector(record: Record, links: dict[str, str]) -> str:
    m = record.manifest
    rows_out = []
    runs = wrong = repeats = false_alarms = voter = 0
    for cell, rows in cells_of(record, "DETECTED"):
        runs += len(rows)
        injected = [c for c in CHANNELS if any(yes(row, f"{c}_injected") for row in rows)]
        latency_predicted, recovery_predicted = predicted_detection(cell["kind"], m)
        for c in injected:
            mine = [row for row in rows if yes(row, f"{c}_injected")]
            declared = [row for row in mine if yes(row, f"{c}_declared")]
            latencies = [whole(row, f"{c}_latency_ms") for row in declared]
            recoveries = [whole(row, f"{c}_recovery_ms") for row in mine if yes(row, f"{c}_recovered")]
            unrecovered = len(mine) - len(recoveries)
            recovery = spread(recoveries) + (f" ({unrecovered:,} never)" if unrecovered else "")
            label = f"{cell['name']} {cell['title']}" + (f", {CHANNEL_NAMES[c]}" if len(injected) > 1 else "")
            rows_out.append((label, f"{len(mine):,}", bounded(len(mine) - len(declared), len(mine)),
                             spread(latencies), latency_predicted, distinct(latencies),
                             recovery, recovery_predicted, as_predicted(mine)))
            if c == "gps":
                fix = [whole(row, "gps_fix_latency_ms") for row in declared]
                fix_recoveries = [whole(row, "gps_recovery_ms") + whole(row, "removal_ms")
                                  - first_fix_at_or_after(whole(row, "removal_ms"), m)
                                  for row in mine if yes(row, "gps_recovered")]
                rows_out.append((f"{cell['name']} on the fix clock", "", "", spread(fix),
                                 number(m["predictions"]["gps_fix_latency_ms"]), distinct(fix),
                                 spread(fix_recoveries), number(m["predictions"]["gps_recovery_min_ms"]), ""))
        for row in rows:
            wrong += sum(whole(row, f"{c}_wrong_channel") for c in CHANNELS)
            repeats += sum(whole(row, f"{c}_repeats") for c in CHANNELS)
            false_alarms += sum(whole(row, f"{c}_false_alarms") for c in CHANNELS)
            voter += whole(row, "voter_events")
    header = ("Cell", "Runs", "Missed", "Latency, ms", "Predicted", "Latencies seen (runs)", "Recovery, ms",
              "Predicted", "As predicted")
    return "\n".join([
        table(header, rows_out), "",
        "Latencies run from onset and recoveries from removal, each as median / 95th percentile / maximum by the "
        "nearest rank. On the fix clock, D03's latency runs from the first fix the dropout suppressed and its "
        "recovery from the first fix after removal. *Missed* is no declaration while the fault was on; a zero carries "
        "its rule-of-three bound. A run is *as predicted* when every per-run prediction held: the declaration's type, "
        "latency and value, the recovery, no repeat, no false alarm, no declaration on another channel and no voter "
        "event. " + zero_note(len(rows) for _, rows in cells_of(record, "DETECTED")), "",
        f"Over these {runs:,} runs: {wrong:,} declarations on another channel, {repeats:,} repeats, "
        f"{false_alarms:,} false alarms and {voter:,} voter events.", "",
        image(links, "d03", "D03: the GPS dropout's detection latency against the uniform prediction"),
    ])


# ---------------------------------------------------------------- Section 6.2: below the thresholds

def block_transients(record: Record) -> str:
    rows_out = []
    for cell, rows in cells_of(record, "SUB_THRESHOLD"):
        declared = sum(whole(row, "declarations") > 0 for row in rows)
        predicted = ("none, but a hold whose run with the readings around it reaches the stuck count, predicted "
                     "run by run from its frames" if cell["kind"] == "T03" else "none")
        rows_out.append((f"{cell['name']} {cell['title']}", f"{len(rows):,}", bounded(declared, len(rows)),
                         predicted, as_predicted(rows)))
    return "\n".join([table(("Cell", "Runs", "Runs with a declaration", "Predicted", "As predicted"), rows_out), "",
                      zero_note(len(rows) for _, rows in cells_of(record, "SUB_THRESHOLD"))])


# ---------------------------------------------------------------- Section 6.3: what no check covers

def frames_after_onset(row: dict[str, str], period_ms: int) -> int:
    return (whole(row, "end_ms") - whole(row, "onset_ms")) // period_ms


def block_uncovered(record: Record, links: dict[str, str]) -> str:
    period = record.manifest["timing"]["period_ms"]
    rows_out = []
    for cell, rows in cells_of(record, "UNCOVERED"):
        if not rows:
            continue
        kind, n = cell["kind"], len(rows)
        declared = sum(whole(row, "declarations") > 0 for row in rows)
        off_twin = [whole(row, "normal_error_frames") / frames_after_onset(row, period) for row in rows]
        lines: list[tuple[str, str, str]] = []
        if kind == "U01":
            lines.append(("barometric altitude error per hPa of bias, m",
                          spread([real(r, "err_baro_alt_m") / abs(real(r, "value")) for r in rows]),
                          f"about 8.3 ({ALTITUDE_PER_HPA_M:.2f} at {ISA_SEA_LEVEL_HPA:,} hPa)"))
            lines.append(("fused altitude error, share of the barometric",
                          spread([real(r, "err_fused_alt_m") / real(r, "err_baro_alt_m") for r in rows], percent),
                          "nearly all; the GPS carries about 0.2% of the weight"))
            lines.append(("vertical speed error, m/s", spread([real(r, "err_vs_mps") for r in rows]),
                          "not predicted"))
        elif kind == "U02":
            lines.append(("barometric altitude error at the end of the minute, m",
                          spread([real(r, "err_baro_alt_m") for r in rows]), "2.5 to 25"))
            lines.append(("that error against 8.33 m/hPa × the drift × the time since onset",
                          spread([real(r, "err_baro_alt_m") / (ALTITUDE_PER_HPA_M * abs(real(r, "rate_per_s"))
                                                               * (whole(r, "end_ms") - period - whole(r, "onset_ms"))
                                                               / 1000.0) for r in rows], ratio), "1"))
            lines.append(("vertical speed error per hPa/s of drift, m/s",
                          spread([real(r, "err_vs_mps") / abs(real(r, "rate_per_s")) for r in rows]),
                          "about 8.3"))
        elif kind == "U03":
            errors = [max(real(r, "err_pitch_deg"), real(r, "err_roll_deg")) for r in rows]
            tilts = [math.degrees(math.atan(abs(real(r, "value")) / GRAVITY_MPS2)) for r in rows]
            lines.append(("pitch or roll error, degrees", spread(errors), "0.3 to 2.9"))
            lines.append(("that error against atan(offset / g)", spread([e / a for e, a in zip(errors, tilts)], ratio),
                          "1"))
        elif kind == "U04":
            lines.append(("fused altitude error, share of the bias",
                          spread([real(r, "err_fused_alt_m") / abs(real(r, "value")) for r in rows], percent),
                          "under 1%, about 0.24%"))
        elif kind == "U05":
            lines.append(("fused altitude error, m", spread([real(r, "err_fused_alt_m") for r in rows]),
                          "negligible"))
        elif kind == "U06":
            to_end = sum(whole(r, "nm_episodes") == 1 and whole(r, "nm_frames") == frames_after_onset(r, period)
                         and real(r, "nm_lanes") == 3.0 for r in rows)
            nan_voted = sum(whole(r, "invariant_violations") > 0 for r in rows)
            lines.append(("no majority from the NaN to the end: one event, three lanes in the vote",
                          f"{to_end:,} of {n:,} runs", "every run"))
            lines.append(("runs breaking an invariant (a NaN voted among them)", f"{nan_voted:,} of {n:,}", "none"))
        elif kind == "U07":
            infinite = sum(yes(r, "roll_infinite_normal") for r in rows)
            lines.append(("roll +∞, NORMAL and VOTED on every frame from onset", f"{infinite:,} of {n:,} runs",
                          "every run"))
        lines.append(("frames after onset with a NORMAL value off the twin's, share", spread(off_twin, percent),
                      "—"))
        for i, (measured, values, predicted) in enumerate(lines):
            lead = (f"{cell['name']} {cell['title']}", f"{n:,}", bounded(declared, n), as_predicted(rows)) if i == 0 \
                else ("", "", "", "")
            rows_out.append((*lead[:3], measured, values, predicted, lead[3]))
    header = ("Cell", "Runs", "Runs with a declaration", "Consequence", "Median / 95th percentile / maximum, or runs",
              "Predicted", "As predicted")
    return "\n".join([
        table(header, rows_out), "",
        "Each error is the largest difference, over the frames after onset whose value is NORMAL, between the run's "
        "voted output and its twin's: the same seed with nothing injected. A share of frames off the twin is the "
        "fraction of the frames after onset on which a value the frame calls NORMAL differs from the twin's.", "",
        image(links, "uncovered", "U01 and U02: the barometric altitude error against the injected bias and drift"),
    ])


# ---------------------------------------------------------------- Section 6.4: false alarms

def per_channel_hour(events: int, hours: float) -> str:
    if hours <= 0:
        return "—"
    if events == 0:
        return f"< {3.0 / hours:.2g} (rule of three)"
    return f"{events / hours:.3g}"


def block_false_alarms(record: Record) -> str:
    # The six persistence counts of a lane kind show the detector the same frames: count each kind and
    # replicate once.
    once: dict[tuple[str, str], dict[str, str]] = {}
    for row in record.rows:
        once.setdefault((row["kind"], row["replicate"]), row)
    rows_out = []
    all_hours, all_alarms = 0.0, 0
    for c in CHANNELS:
        hours = sum(whole(row, f"{c}_exposure_ms") for row in once.values()) / 3.6e6
        alarms = sum(whole(row, f"{c}_false_alarms") for row in once.values())
        all_hours += hours
        all_alarms += alarms
        rows_out.append((CHANNEL_NAMES[c], f"{hours:,.1f}", f"{alarms:,}", per_channel_hour(alarms, hours)))
    rows_out.append(("all three", f"{all_hours:,.1f}", f"{all_alarms:,}", per_channel_hour(all_alarms, all_hours)))
    clean = rows_of_kind(record, "C00")
    injected = [row for row in record.rows if any(yes(row, f"{c}_injected") for c in CHANNELS)]
    wrong = sum(whole(row, f"{c}_wrong_channel") for row in injected for c in CHANNELS)
    lines = [
        table(("Channel", "Exposure, channel-hours", "False alarms", "Per channel-hour"), rows_out), "",
        f"Exposure counts each kind and replicate once ({len(once):,} of the {len(record.rows):,} runs), because the "
        "six persistence counts of a lane kind show the detector the same frames. A channel is exposed for the whole "
        "of a run that injects nothing into it and up to the onset of a run that does.", "",
    ]
    if clean:
        with_declaration = sum(whole(row, "declarations") > 0 for row in clean)
        with_voter_event = sum(whole(row, "voter_events") > 0 for row in clean)
        broken = sum(whole(row, "invariant_violations") > 0 for row in clean)
        lines.append(f"The clean runs (C00), {len(clean):,} of 60 s: runs with a declaration {bounded(with_declaration, len(clean))}; "
                     f"runs with a voter event {bounded(with_voter_event, len(clean))}; runs breaking an invariant, "
                     f"a frame without VOTED among them, {broken:,}.")
        lines.append("")
    lines.append(f"Declarations on another channel than the injected one, over the {len(injected):,} runs that "
                 f"inject a sensor fault: {wrong:,}.")
    return "\n".join(lines)


# ---------------------------------------------------------------- Section 6.5: the lane faults and P

def share_row(label: str, by_p: dict[int, list[dict[str, str]]], counts: Sequence[int],
              event: Callable[[dict[str, str]], bool], predicted: str) -> tuple[str, ...]:
    cells = [share_cell(sum(event(row) for row in by_p.get(p, [])), len(by_p.get(p, []))) for p in counts]
    return (label, *cells, predicted)


def by_count(values: Callable[[int], float], counts: Sequence[int]) -> str:
    return ", ".join(f"{100.0 * values(p):.1f}".rstrip("0").rstrip(".") for p in counts)


def block_voter(record: Record, links: dict[str, str]) -> str:
    kinds = lane_kinds(record)
    counts = sorted({cell["persistence"] for cell in record.manifest["cells"] if cell["class"] == "LANE"})
    if not kinds:
        return "*No lane cells in this record.*"
    isolation = {cell["persistence"]: cell["isolation_latency_ms"] for cell in record.manifest["cells"]}
    p_header = tuple(f"P = {p}" for p in counts)
    single = {kind: by_p for kind, by_p in kinds.items()
              if all(lanes_altered(row) == 1 for rows in by_p.values() for row in rows)}
    double = {kind: by_p for kind, by_p in kinds.items() if kind not in single}

    excluded_predicted = {
        "V01": "100 at every P", "V02": "100 at every P", "V03": "100 at every P",
        "V04": "100 at P = 1, else 0", "V05": "100 at P = 1, else 0", "V06": "100 at P = 1, else 0",
        "V07": by_count(lambda p: sum(n >= p for n in V07_LENGTHS) / len(V07_LENGTHS), counts),
        "V08": "100 but where the arithmetic absorbs the flip: measured, not predicted",
    }
    excluded_rows = [share_row(f"{kind}", by_p, counts, lambda row: whole(row, "isolation_ms") >= 0,
                               excluded_predicted.get(kind, "—")) for kind, by_p in single.items()]
    latency_rows = [("predicted, (P − 1) × 20", *(number(isolation[p]) for p in counts))]
    for kind, by_p in single.items():
        latency_rows.append((kind, *(spread([whole(r, "isolation_ms") for r in by_p.get(p, [])
                                             if whole(r, "isolation_ms") >= 0]) for p in counts)))
    wrong_single = sum(whole(row, "wrong_output_frames") > 0 for by_p in single.values()
                       for rows in by_p.values() for row in rows)
    single_runs = sum(len(rows) for by_p in single.values() for rows in by_p.values())

    def healthy_out(row: dict[str, str]) -> bool:
        return whole(row, "healthy_excluded") > 0

    def no_majority(row: dict[str, str]) -> bool:
        return whole(row, "nm_episodes") > 0

    def wrong_output(row: dict[str, str]) -> bool:
        return whole(row, "wrong_output_frames") > 0

    def first_excluded(row: dict[str, str]) -> bool:
        return whole(row, "exclusions") > 0

    late = lambda p: sum(g >= p for g in GAPS) / len(GAPS)    # noqa: E731
    early = lambda p: sum(g < p for g in GAPS) / len(GAPS)    # noqa: E731
    double_measures = {
        "V09": [("healthy lane thrown out", healthy_out, "100 at every P"),
                ("a wrong output voted", wrong_output, "100 at every P"),
                ("no majority", no_majority, "0")],
        "V10": [("no majority", no_majority, "100 at every P"), ("healthy lane thrown out", healthy_out, "0")],
        "V11": [("no majority", no_majority, "100 at every P"),
                ("lane a excluded first", first_excluded, f"g ≥ P: {by_count(late, counts)}"),
                ("healthy lane thrown out", healthy_out, "0")],
        "V12": [("a wrong majority: healthy lane thrown out", healthy_out, f"g < P: {by_count(early, counts)}"),
                ("no majority, lane a out first", no_majority, f"g ≥ P: {by_count(late, counts)}")],
    }
    double_rows = []
    for kind, by_p in double.items():
        for label, event, predicted in double_measures.get(kind, []):
            double_rows.append(share_row(f"{kind}: {label}", by_p, counts, event, predicted))
    lane_rows = [row for by_p in kinds.values() for rows in by_p.values() for row in rows]
    broken = sum(whole(row, "invariant_violations") > 0 for row in lane_rows)
    return "\n".join([
        "**The altered lane excluded**, % of runs (95% Wilson interval where the share is neither 0 nor 100; a 0 or a "
        "100 bounds the other outcome by the rule of three):", "",
        table(("Kind", *p_header, "Predicted"), excluded_rows), "",
        "**Isolation latency**, ms from the lane fault's onset to its exclusion, median / 95th percentile / maximum "
        "over the runs that excluded the lane:", "",
        table(("Kind", *p_header), latency_rows), "",
        f"One altered lane: {wrong_single:,} of {single_runs:,} runs voted a value other than a healthy lane's.", "",
        "**Two lanes altered**, % of runs:", "",
        table(("Kind and outcome", *p_header, "Predicted"), double_rows), "",
        f"Invariants over all {len(lane_rows):,} lane-fault runs (two lanes in the vote, one lane bit at most, VOTED "
        f"on every frame, no NaN voted, an event per no-majority episode): {broken:,} runs broke one.", "",
        image(links, "sweep", "V07 and V12: what the persistence count buys and costs"),
    ])


# ---------------------------------------------------------------- Section 7: departures

def block_departures(record: Record) -> str:
    rows_by_cell: dict[str, list[dict[str, str]]] = {}
    for row in record.rows:
        rows_by_cell.setdefault(row["cell"], []).append(row)
    rows_out = []
    for cell in record.manifest["cells"]:
        rows = rows_by_cell.get(cell["name"], [])
        written = [row for row in rows if not yes(row, "as_written")]
        gate = [row for row in rows if not yes(row, "as_predicted")]
        if not written and not gate:
            continue
        shown = gate or written
        seeds = ", ".join(row["run_id"] for row in shown[:5]) + (", …" if len(shown) > 5 else "")
        rows_out.append((cell["name"], f"{len(rows):,}", f"{len(written):,}", f"{len(gate):,}", seeds,
                         shown[0]["mismatch"]))
    n = len(record.rows)
    total_written = sum(not yes(row, "as_written") for row in record.rows)
    total_gate = sum(not yes(row, "as_predicted") for row in record.rows)
    if not rows_out:
        return f"*All {n:,} runs are as Section 5 was written and as the gate predicts.*"
    return "\n".join([
        table(("Cell", "Runs", "Departing from Section 5 as written", "Departing from the gate", "Seeds",
               "The first departure"), rows_out), "",
        f"Over the campaign: {total_written:,} of {n:,} runs depart from Section 5 as written, {total_gate:,} from the "
        "gate's per-run predictions. The seeds rebuild each run.",
    ])


# ---------------------------------------------------------------- the figures

def save_figures(record: Record, directory: Path) -> dict[str, Path]:
    """The three figures, each written only when its cells are in the record."""
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    directory.mkdir(parents=True, exist_ok=True)
    written: dict[str, Path] = {}
    with plt.rc_context(FIGURE_STYLE):
        for key, draw in (("d03", figure_d03), ("sweep", figure_sweep), ("uncovered", figure_uncovered)):
            fig = draw(record, plt)
            if fig is None:
                continue
            path = directory / FIGURE_FILES[key]
            fig.savefig(path, dpi=DPI, metadata={"Software": None})
            plt.close(fig)
            written[key] = path
    return written


def thousands(axis) -> None:
    from matplotlib.ticker import FuncFormatter
    axis.set_major_formatter(FuncFormatter(lambda value, _: f"{value:,.0f}"))


def figure_d03(record: Record, plt):
    rows = rows_of_kind(record, "D03")
    latencies = [whole(row, "gps_latency_ms") for row in rows if yes(row, "gps_declared")]
    if not latencies:
        return None
    grid = gps_latency_grid(record.manifest)
    period = record.manifest["timing"]["period_ms"]
    counts = Counter(latencies)
    positions = sorted(set(grid) | set(counts))
    expected = len(latencies) / len(grid)
    fig, ax = plt.subplots(figsize=(8.6, 3.6))
    ax.bar(positions, [counts.get(v, 0) for v in positions], width=0.7 * period, color=SERIES[0],
           label="measured: runs declared at each latency")
    ax.hlines(expected, grid[0] - period / 2, grid[-1] + period / 2, color=INK_SECONDARY, linewidth=1.5,
              label=f"predicted: {expected:,.0f} runs at each of the {len(grid)} frames from "
                    f"{grid[0]:,} to {grid[-1]:,} ms")
    ax.set_xlim(grid[0] - 3 * period, grid[-1] + 3 * period)
    ax.set_ylim(0, max(max(counts.values()), expected) * 1.35)
    ax.set_xticks([grid[0], *range(1200, grid[-1], 200), grid[-1]] if grid[-1] > 1200 else grid[::10])
    thousands(ax.xaxis)
    ax.set_xlabel("detection latency from onset, ms")
    ax.set_ylabel("runs")
    ax.set_title(f"D03 GPS dropout: {len(latencies):,} declarations, latency set by the onset's place in the fix second")
    ax.legend(loc="upper left", ncols=2)
    fig.tight_layout()
    return fig


def figure_sweep(record: Record, plt):
    kinds = lane_kinds(record)
    if "V07" not in kinds or "V12" not in kinds:
        return None
    from matplotlib.lines import Line2D
    counts = sorted(set(kinds["V07"]) & set(kinds["V12"]))
    shipped = next(cell["persistence"] for cell in record.manifest["cells"] if cell["class"] != "LANE")
    series = (
        ("V07 lane upset of 2 to 6 cycles: the lane excluded", "V07 lane excluded", kinds["V07"],
         lambda row: whole(row, "isolation_ms") >= 0,
         lambda p: sum(n >= p for n in V07_LENGTHS) / len(V07_LENGTHS)),
        ("V12 identical second fault 0 to 10 cycles later: a wrong majority", "V12 wrong majority", kinds["V12"],
         lambda row: whole(row, "healthy_excluded") > 0,
         lambda p: sum(g < p for g in GAPS) / len(GAPS)),
    )
    fig, ax = plt.subplots(figsize=(8.6, 4.0))
    ax.axvline(shipped, color=AXIS, linewidth=0.8, zorder=1)
    ax.annotate(f"shipped P = {shipped}", (shipped, 104), xytext=(4, 0), textcoords="offset points",
                color=INK_MUTED, fontsize=8.5, va="center")
    handles = []
    for (label, short, by_p, event, predicted), colour in zip(series, SERIES):
        ks = [sum(event(row) for row in by_p[p]) for p in counts]
        ns = [len(by_p[p]) for p in counts]
        shares = [k / n for k, n in zip(ks, ns)]
        bounds = [wilson_interval(k, n) for k, n in zip(ks, ns)]
        ax.plot(counts, [100.0 * predicted(p) for p in counts], color=colour, linewidth=1.5, zorder=2)
        ax.errorbar(counts, [100.0 * s for s in shares],
                    yerr=[[100.0 * (s - lo) for s, (lo, _) in zip(shares, bounds)],
                          [100.0 * (hi - s) for s, (_, hi) in zip(shares, bounds)]],
                    fmt="o", color=colour, markersize=7, markeredgecolor=SURFACE, markeredgewidth=1.5,
                    elinewidth=1.2, capsize=0, zorder=3)
        ax.annotate(short, (counts[-1], 100.0 * shares[-1]), xytext=(9, 0), textcoords="offset points",
                    color=INK_SECONDARY, fontsize=8.5, va="center")
        handles.append(Line2D([0], [0], color=colour, linewidth=1.5, marker="o", markersize=7,
                              markeredgecolor=SURFACE, markeredgewidth=1.5, label=label))
    ax.set_xticks(counts)
    ax.set_xlim(counts[0] - 0.5, counts[-1] + 1.9)
    ax.set_ylim(-4, 110)
    ax.set_xlabel("persistence count P: consecutive cycles outside the majority before a lane is excluded")
    ax.set_ylabel("% of runs")
    ax.set_title("What the persistence count buys and costs", pad=20)
    ax.text(0, 1.03, f"Lines: predicted before the campaign. Dots: measured, {ns[0]:,} runs each, with 95% Wilson "
                     "intervals.", transform=ax.transAxes, color=INK_SECONDARY, fontsize=8.5)
    ax.legend(handles=handles, loc="upper right")
    fig.tight_layout()
    return fig


def figure_uncovered(record: Record, plt):
    u01, u02 = rows_of_kind(record, "U01"), rows_of_kind(record, "U02")
    if not u01 or not u02:
        return None
    period = record.manifest["timing"]["period_ms"]
    minute_s = (whole(u02[0], "end_ms") - period - whole(u02[0], "onset_ms")) / 1000.0
    panels = (
        (u01, "value", "U01 pressure bias step", "|bias|, hPa", ALTITUDE_PER_HPA_M,
         f"predicted: {ALTITUDE_PER_HPA_M:.2f} m per hPa"),
        (u02, "rate_per_s", "U02 pressure drift for a minute", "|drift|, hPa/s", ALTITUDE_PER_HPA_M * minute_s,
         f"predicted: {ALTITUDE_PER_HPA_M:.2f} m per hPa × {minute_s:.2f} s"),
    )
    fig, axes = plt.subplots(1, 2, figsize=(8.6, 3.6))
    for ax, (rows, column, title, xlabel, slope, note) in zip(axes, panels):
        xs = [abs(real(row, column)) for row in rows]
        ys = [real(row, "err_baro_alt_m") for row in rows]
        top = max(xs) * 1.05
        ax.plot([0, top], [0, slope * top], color=INK_SECONDARY, linewidth=1.5, zorder=2)
        ax.scatter(xs, ys, s=30, color=SERIES[0], edgecolors=SURFACE, linewidths=1.0, zorder=3)
        ax.annotate(note, (0.04, 0.92), xycoords="axes fraction", color=INK_SECONDARY, fontsize=8.5)
        ax.set_xlim(0, top)
        ax.set_ylim(0, slope * top * 1.08)
        ax.set_title(f"{title}, {len(rows):,} runs")
        ax.set_xlabel(xlabel)
        ax.set_ylabel("barometric altitude error, m")
        ax.grid(axis="x", color=GRID, linewidth=0.8)
    fig.tight_layout()
    return fig


# ---------------------------------------------------------------- the report

def build_blocks(record: Record, links: dict[str, str]) -> dict[str, str]:
    return {
        "run": block_run(record),
        "detector": block_detector(record, links),
        "transients": block_transients(record),
        "uncovered": block_uncovered(record, links),
        "false-alarms": block_false_alarms(record),
        "voter": block_voter(record, links),
        "departures": block_departures(record),
    }


def replace_blocks(text: str, blocks: dict[str, str]) -> str:
    """The report with each generated block's body replaced, everything else byte for byte. Every block
    the report holds must be generated and every generated block must be in the report, once."""
    names = BLOCK_START.findall(text)
    matched = [m.group("name") for m in BLOCK.finditer(text)]
    if names != matched or any(BLOCK_START.search(m.group("body")) for m in BLOCK.finditer(text)):
        raise ValueError("a generated block in the report has no end marker")
    duplicated = sorted(name for name, count in Counter(names).items() if count > 1)
    missing = sorted(set(blocks) - set(names))
    unknown = sorted(set(names) - set(blocks))
    if duplicated or missing or unknown:
        raise ValueError(f"the report's generated blocks do not match: duplicated {duplicated}, "
                         f"missing {missing}, unknown {unknown}")
    return BLOCK.sub(lambda m: f"<!-- generated:{m.group('name')} -->\n{blocks[m.group('name')].rstrip()}\n"
                               "<!-- end generated -->", text)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="python -m analysis.fault_campaign",
                                     description="Rewrite FTS-FI-001's generated blocks and figures from a "
                                                 "campaign's record.")
    parser.add_argument("record", help="the directory fault_campaign wrote: runs.csv, manifest.json, config.yaml")
    parser.add_argument("--report", default="docs/fault_injection_report.md")
    parser.add_argument("--figures", default="docs/img", help="the directory for the three PNG figures")
    args = parser.parse_args(argv)
    try:
        record = load_record(args.record)
        report = Path(args.report)
        text = report.read_text()
        figures = save_figures(record, Path(args.figures))
        links = {key: Path(os.path.relpath(path, report.parent)).as_posix() for key, path in figures.items()}
        updated = replace_blocks(text, build_blocks(record, links))
    except (OSError, ValueError, KeyError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    if updated != text:
        report.write_text(updated)
    print(f"{report}: generated blocks rewritten from {len(record.rows):,} runs "
          f"({'changed' if updated != text else 'unchanged'}); {len(figures)} figures in {args.figures}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
