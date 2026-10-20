"""The fault-injection campaign's statistics (REQ-TEST-006; FTS-FI-001 Section 4.5).

A rate is k events in n runs, stated with its 95% Wilson score interval. A count of zero
also carries the rule-of-three bound, 3/n, with its exact value beside it. A latency is a
distribution, summarized by the nearest-rank rule, so every reported quantile is a value
the campaign observed, on the 20 ms grid. Pure functions with no I/O: the report generator
(analysis.fault_campaign) and the pytest suite (TC-035 step 7) call them.
"""
from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Sequence

Z_95 = 1.959964           # the standard normal's two-sided 95% point
ZERO_ALPHA = 0.05         # the rule of three's confidence: zero events would be seen at most 5% of the time


def wilson_interval(k: int, n: int, z: float = Z_95) -> tuple[float, float]:
    """The Wilson score interval for k events in n runs.

    The textbook interval p +- z sqrt(p(1 - p)/n) collapses to the single point p at k = 0
    and k = n, the two counts a campaign that matches its predictions produces most, and can
    leave [0, 1]. Wilson's interval inverts the normal test instead: it keeps a width at
    both ends and stays inside [0, 1]. At k = 0 its lower end is exactly 0, at k = n its
    upper end exactly 1; both are set rather than computed, so rounding cannot move them."""
    if n <= 0 or not 0 <= k <= n:
        raise ValueError(f"a rate needs 0 <= k <= n and n > 0, got k = {k}, n = {n}")
    p = k / n
    z2 = z * z
    denominator = 1.0 + z2 / n
    centre = (p + z2 / (2.0 * n)) / denominator
    half_width = z * math.sqrt(p * (1.0 - p) / n + z2 / (4.0 * n * n)) / denominator
    low = 0.0 if k == 0 else max(0.0, centre - half_width)
    high = 1.0 if k == n else min(1.0, centre + half_width)
    return low, high


def rule_of_three(n: int) -> float:
    """3/n: zero events in n independent runs bounds the rate below this with 95%
    confidence (-ln 0.05 = 2.996, so 3). It does not show the rate is zero."""
    if n <= 0:
        raise ValueError(f"a bound needs n > 0, got {n}")
    return 3.0 / n


def zero_bound_exact(n: int, alpha: float = ZERO_ALPHA) -> float:
    """1 - alpha^(1/n): the rate at which zero events in n runs has probability alpha.
    The rule of three is its first-order approximation, a little above it."""
    if n <= 0:
        raise ValueError(f"a bound needs n > 0, got {n}")
    return 1.0 - alpha ** (1.0 / n)


def nearest_rank(values: Sequence[float], percent: int) -> float:
    """The nearest-rank percentile: the value at rank ceil(percent/100 x n) of the sorted
    data, the smallest value with at least percent% of the data at or below it. The rank
    is computed in integers, so the 95th percentile of 1,000 values is rank 950, never 951
    by a rounding error. It equals numpy's percentile(method="inverted_cdf")."""
    if len(values) == 0:
        raise ValueError("a percentile needs at least one value")
    if not 0 < percent <= 100:
        raise ValueError(f"a percentile is from 1 to 100, got {percent}")
    ordered = sorted(values)
    rank = -(-percent * len(ordered) // 100)       # ceil(percent x n / 100) without a float
    return ordered[rank - 1]


@dataclass(frozen=True)
class Summary:
    """A latency's distribution as FTS-FI-001 reports it."""
    n: int
    median: float
    p95: float
    maximum: float


def summarize(values: Sequence[float]) -> Summary:
    return Summary(len(values), nearest_rank(values, 50), nearest_rank(values, 95), max(values))
