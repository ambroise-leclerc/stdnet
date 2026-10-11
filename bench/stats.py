"""Labels, colours and statistics shared by run.py's consumers (infographic.py)."""
import math
import statistics

from scipy import stats as sps

LABEL = {
    "raw_blocking": "raw blocking",
    "raw_poll": "raw poll (BSD sockets)",
    "raw_epoll": "raw epoll",
    "asio_callbacks": "Asio callbacks",
    "asio_coroutines": "Asio coroutines",
    "stdnet_senders": "stdnet senders",
    "stdnet_coroutines": "stdnet task",
    "stdnet_senders_poll": "stdnet senders (poll)",
}
COLOR = {
    "raw_blocking": "#2a78d6",
    "raw_poll": "#8b6b4a",
    "raw_epoll": "#eb6834",
    "asio_callbacks": "#1baf7a",
    "asio_coroutines": "#eda100",
    "stdnet_senders": "#e87ba4",
    "stdnet_coroutines": "#008300",
    "stdnet_senders_poll": "#6250d6",
}

def pick(rows, **kw):
    return [r for r in rows if all(r.get(k) == v for k, v in kw.items())]


def one(rows, **kw):
    m = pick(rows, **kw)
    return m[0] if m else None


def samples(row, key):
    """Per-run values of a metric (valid runs only)."""
    return [r[key] for r in row.get("runs", []) if not r.get("problems") and key in r]


def welch(a, b):
    """Welch's t-test on two samples. Returns (ratio a/b, 95 % CI of the ratio of means, p-value),
    or None when a sample has fewer than 2 values. The CI uses the delta method on the means."""
    if len(a) < 2 or len(b) < 2:
        return None
    ma, mb = statistics.fmean(a), statistics.fmean(b)
    if mb == 0:
        return None
    va, vb = statistics.variance(a) / len(a), statistics.variance(b) / len(b)
    if va + vb == 0:
        return (ma / mb, (ma / mb, ma / mb), 0.0 if ma != mb else 1.0)
    t, p = sps.ttest_ind(a, b, equal_var=False)
    df = (va + vb) ** 2 / (va ** 2 / (len(a) - 1) + vb ** 2 / (len(b) - 1))
    tcrit = sps.t.ppf(0.975, df)
    ratio = ma / mb
    se = math.sqrt(va / mb ** 2 + ma ** 2 * vb / mb ** 4)
    return ratio, (ratio - tcrit * se, ratio + tcrit * se), float(p)
