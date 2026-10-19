"""analysis/campaign_stats.py and analysis/fault_campaign.py: TC-035 steps 7 and 8 (REQ-TEST-006).

The statistics against values computed by hand and against numpy, the report generator's one rule
(only the generated blocks change), and the command end to end: build/fault_campaign over a few
cells, then the generator over its record and a copy of the report.
"""
from __future__ import annotations

import csv
import hashlib
import json
import shutil
import subprocess
from pathlib import Path

import numpy as np
import pytest

from analysis import campaign_stats as cs
from analysis import fault_campaign as fc
from conftest import REPO_ROOT, SHIPPED_CONFIG

REPORT = REPO_ROOT / "docs" / "fault_injection_report.md"
# A cell of each block's kinds, the lane cells at the shipped count: three replicates each.
CELLS = ["D01", "D03", "D04", "D05", "T03", "U01", "U02", "U03", "U04", "U06", "U07", "C00", "V04/P3", "V07/P3",
         "V09/P3", "V11/P3", "V12/P3"]
REPLICATES = 3


# --- the statistics ----------------------------------------------------------------------------

@pytest.mark.parametrize("k, n, low, high", [
    (0, 10, 0.0, 0.2775), (5, 10, 0.2366, 0.7634), (0, 1000, 0.0, 0.0038), (1, 100, 0.0018, 0.0545)])
def test_wilson_interval_matches_hand_computed_values(k, n, low, high):
    measured_low, measured_high = cs.wilson_interval(k, n)
    assert measured_low == pytest.approx(low, abs=5e-5)
    assert measured_high == pytest.approx(high, abs=5e-5)


def test_wilson_interval_keeps_its_ends_exact_and_inside_the_unit_interval():
    for n in (1, 7, 1000):
        assert cs.wilson_interval(0, n)[0] == 0.0
        assert cs.wilson_interval(n, n)[1] == 1.0
        for k in range(0, n + 1, max(1, n // 10)):
            low, high = cs.wilson_interval(k, n)
            assert 0.0 <= low <= k / n <= high <= 1.0
    for k, n in ((1, 0), (3, 2), (-1, 5)):
        with pytest.raises(ValueError):
            cs.wilson_interval(k, n)


def test_rule_of_three_bounds_a_zero_count_just_above_the_exact_value():
    assert cs.rule_of_three(1000) == 0.003
    assert cs.zero_bound_exact(1000) == pytest.approx(0.0029912, abs=1e-7)
    for n in (10, 300, 1000, 10000):
        exact = cs.zero_bound_exact(n)
        assert (1.0 - exact) ** n == pytest.approx(0.05)     # zero events at that rate: 5% of the time
        assert exact < cs.rule_of_three(n)
    for n in (300, 1000, 10000):
        assert cs.rule_of_three(n) < 1.01 * cs.zero_bound_exact(n)
    with pytest.raises(ValueError):
        cs.rule_of_three(0)


def test_nearest_rank_on_the_gps_dropouts_predicted_grid():
    grid = list(range(1020, 2001, 20))
    assert len(grid) == 50
    assert (cs.nearest_rank(grid, 50), cs.nearest_rank(grid, 95), cs.nearest_rank(grid, 100)) == (1500, 1960, 2000)
    assert cs.summarize(grid) == cs.Summary(50, 1500, 1960, 2000)
    assert cs.nearest_rank(list(reversed(grid)), 50) == 1500


def test_nearest_rank_equals_numpys_inverted_cdf():
    rng = np.random.default_rng(20261018)
    for n in (1, 2, 7, 50, 999, 1000):
        values = rng.integers(0, 100, n).tolist()
        for percent in (1, 5, 25, 50, 95, 99, 100):
            assert cs.nearest_rank(values, percent) == np.percentile(values, percent, method="inverted_cdf")


def test_nearest_rank_refuses_what_has_no_percentile():
    for values, percent in (([], 50), ([1.0], 0), ([1.0], 101)):
        with pytest.raises(ValueError):
            cs.nearest_rank(values, percent)


def test_percentages_never_round_a_share_to_its_ends():
    assert (fc.percent(0.0), fc.percent(1.0)) == ("0%", "100%")
    assert fc.percent(0.0038268) == "0.383%"
    assert fc.percent(0.99962) == "99.96%"
    assert fc.percent(0.000004) == "0.0004%"
    assert fc.bounded(0, 1000) == "0 (< 0.3%)"
    assert fc.bounded(5, 10) == "5 (50%; 23.7%–76.3%)"


# --- the generator's rule: only the blocks change -----------------------------------------------

SAMPLE = """# A report

Prose before, kept.

<!-- generated:alpha -->
old alpha
<!-- end generated -->

Prose between | with a pipe, kept.

<!-- generated:beta -->
old beta
<!-- end generated -->
Prose after.
"""


def outside_blocks(text: str) -> str:
    return fc.BLOCK.sub("", text)


def test_replace_blocks_rewrites_the_blocks_and_nothing_else():
    blocks = {"alpha": "new alpha", "beta": "| a | b |\n|---|---|\n| 1 | 2 |\n"}
    out = fc.replace_blocks(SAMPLE, blocks)
    assert "old alpha" not in out and "new alpha" in out and "| 1 | 2 |" in out
    assert outside_blocks(out) == outside_blocks(SAMPLE)
    assert fc.replace_blocks(out, blocks) == out


@pytest.mark.parametrize("text, blocks, message", [
    (SAMPLE, {"alpha": "a"}, "unknown \\['beta'\\]"),
    (SAMPLE, {"alpha": "a", "beta": "b", "gamma": "c"}, "missing \\['gamma'\\]"),
    (SAMPLE.replace("old alpha\n<!-- end generated -->", "old alpha", 1), {"alpha": "a", "beta": "b"}, "no end marker"),
    (SAMPLE + "<!-- generated:beta -->\nagain\n<!-- end generated -->\n", {"alpha": "a", "beta": "b"},
     "duplicated \\['beta'\\]"),
])
def test_replace_blocks_refuses_a_report_that_does_not_match(text, blocks, message):
    with pytest.raises(ValueError, match=message):
        fc.replace_blocks(text, blocks)


# --- the command end to end ----------------------------------------------------------------------

@pytest.fixture(scope="module")
def campaign_record(campaign_bin: Path, tmp_path_factory: pytest.TempPathFactory) -> Path:
    out = tmp_path_factory.mktemp("campaign")
    subprocess.run([str(campaign_bin), "--out", str(out), "--replicates", str(REPLICATES), "--cells", ",".join(CELLS),
                    "--config", str(SHIPPED_CONFIG), "--commit", "pytest-commit", "--platform", "pytest-platform"],
                   check=True, capture_output=True, text=True, timeout=120)
    return out


def generate(record: Path, directory: Path) -> tuple[int, str]:
    report = directory / "report.md"
    if not report.exists():
        shutil.copy(REPORT, report)
    status = fc.main([str(record), "--report", str(report), "--figures", str(directory / "img")])
    return status, report.read_text()


def test_the_record_holds_every_run_its_manifest_and_the_configuration(campaign_record):
    with open(campaign_record / "runs.csv", newline="") as f:
        reader = csv.DictReader(f)
        header, rows = reader.fieldnames, list(reader)
    manifest = json.loads((campaign_record / "manifest.json").read_text())
    assert len(rows) == manifest["runs"] == REPLICATES * len(CELLS)
    assert all(len(row) == len(header) and None not in row and None not in row.values() for row in rows)
    assert [cell["name"] for cell in manifest["cells"]] == CELLS
    for cell in manifest["cells"]:
        assert cell["first_run_id"] == cell["cell"] * 100_000
        assert cell["last_run_id"] == cell["cell"] * 100_000 + REPLICATES - 1
        assert [int(row["run_id"]) for row in rows if row["cell"] == cell["name"]] == \
            list(range(cell["first_run_id"], cell["last_run_id"] + 1))
    assert (manifest["commit"], manifest["platform"]) == ("pytest-commit", "pytest-platform")
    assert (campaign_record / "config.yaml").read_bytes() == SHIPPED_CONFIG.read_bytes()
    assert all(row["as_predicted"] == "1" for row in rows), [row["mismatch"] for row in rows if row["as_predicted"] != "1"]


def test_the_generator_fills_every_block_and_leaves_the_prose(campaign_record, tmp_path):
    status, text = generate(campaign_record, tmp_path)
    assert status == 0
    assert "*Not yet run.*" not in text
    assert outside_blocks(text) == outside_blocks(REPORT.read_text())
    assert f"SHA-256 `{hashlib.sha256(SHIPPED_CONFIG.read_bytes()).hexdigest()}`" in text
    assert "`pytest-commit`" in text and "pytest-platform" in text
    for name in fc.FIGURE_FILES.values():
        assert (tmp_path / "img" / name).read_bytes()[:8] == b"\x89PNG\r\n\x1a\n"
        assert f"](img/{name})" in text
    assert generate(campaign_record, tmp_path) == (0, text)          # the same record, the same report


def test_a_departure_from_section_5_is_named_in_section_7(campaign_record, tmp_path):
    record = tmp_path / "record"
    shutil.copytree(campaign_record, record)
    with open(record / "runs.csv", newline="") as f:
        reader = csv.DictReader(f)
        header, rows = reader.fieldnames, list(reader)
    planted = rows[1]
    planted["as_written"], planted["mismatch"] = "0", "a planted departure"
    with open(record / "runs.csv", "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=header, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    manifest = json.loads((record / "manifest.json").read_text())
    manifest["as_written"] -= 1
    (record / "manifest.json").write_text(json.dumps(manifest))
    status, text = generate(record, tmp_path)
    departures = text.split("<!-- generated:departures -->")[1].split("<!-- end generated -->")[0]
    assert status == 0
    assert planted["run_id"] in departures and "a planted departure" in departures
    assert f"1 of {len(rows):,} runs depart from Section 5 as written" in departures


def test_an_incomplete_record_is_refused(campaign_record, tmp_path):
    record = tmp_path / "record"
    shutil.copytree(campaign_record, record)
    manifest = json.loads((record / "manifest.json").read_text())
    manifest["runs"] += 1
    (record / "manifest.json").write_text(json.dumps(manifest))
    assert generate(record, tmp_path)[0] == 1
