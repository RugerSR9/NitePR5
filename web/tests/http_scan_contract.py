"""HTTP scan-results contract (ARCHITECTURE §5.3).

HTTP clients must never GET unbounded scan hits — omit limit-as-"all",
pass limit="all", or otherwise dump the survivor list. The default page is
RESULTS_MAX (256). Above that: count-first, results=[]. Refuse, do not crawl.
"""

from __future__ import annotations

from nitepr5_core import RESULTS_MAX, InvalidResultLimit, ResultsTooMany

# Sentinel the UI must never send as a results page size.
UNBOUNDED_RESULTS = "all"


def http_scan_results_limit(limit: int | str | None = None) -> int:
    """Resolve GET /scan/results limit. Default 256; never "all" / unbounded."""
    if limit == UNBOUNDED_RESULTS:
        raise ResultsTooMany(-1)
    if limit is None or limit == "":
        return RESULTS_MAX
    try:
        n = int(limit)
    except (TypeError, ValueError) as exc:
        raise InvalidResultLimit(limit) from exc
    if n <= 0:
        raise InvalidResultLimit(n)
    if n > RESULTS_MAX:
        raise ResultsTooMany(n)
    return n
