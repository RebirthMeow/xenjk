"""Simulate the g_tick.c timer rules over integer server frames, mirroring the C (docs/gameplay/TICK_INDEPENDENCE.md)."""
TODAY = 33


def rearm(deadline, period, now):
    period = max(1, period)
    if deadline > 0 and now - deadline < period:
        return deadline + period
    return now + period


def today_strict(ms):
    return (ms // TODAY + 1) * TODAY


def today_ceil(ms):
    return (ms + TODAY - 1) // TODAY * TODAY


def today_nested(ms, think):
    think = max(TODAY, today_ceil(think))
    s = today_strict(ms)
    return (s + think - 1) // think * think


def frames(fps, seconds=60):
    F = 1000 // fps
    return [F * i for i in range(1, int(seconds * 1000 / F))], F


def strict_rate(fps, ms, on, today=False, seconds=60):
    """A held timer: x < t (stock) or x <= t (on), re-armed. Returns mean period."""
    ts, F = frames(fps, seconds)
    d, fires = 0, []
    for t in ts:
        due = d <= t if on else d < t
        if due:
            fires.append(t)
            d = rearm(d, today_strict(ms) if today else ms, t) if on else t + ms
    return (fires[-1] - fires[0]) / (len(fires) - 1)


def think_rate(fps, period, on, seconds=60):
    """nextthink = level.time + period (stock) or G_TickNextThink(period)."""
    ts, F = frames(fps, seconds)
    nt, fires = ts[0], []
    for t in ts:
        if nt <= t:
            fires.append(t)
            nt = rearm(nt, period, t) if on else t + period
    return (fires[-1] - fires[0]) / (len(fires) - 1)


def nested_rate(fps, ms, think, on_today, seconds=60):
    """A strict ms timer checked only on think of `think` ms (sentry fire)."""
    ts, F = frames(fps, seconds)
    nt, d, fires = ts[0], 0, []
    for t in ts:
        if nt <= t:
            due = (d <= t) if on_today else (d < t)
            if due:
                fires.append(t)
                d = rearm(d, today_nested(ms, think), t) if on_today else t + ms
            tp = today_ceil(think) if on_today else think
            nt = rearm(nt, tp, t) if on_today else t + think
    return (fires[-1] - fires[0]) / (len(fires) - 1)


def saber_passes(fps, seconds=10):
    ts, F = frames(fps, seconds)
    nxt, passes = 0, []
    for t in ts:
        if F >= TODAY or not nxt or t - nxt > 4 * TODAY or nxt - t > 4 * TODAY:
            ok, nxt = True, t + TODAY
        elif t + F // 2 >= nxt:
            ok, nxt = True, nxt + TODAY
        else:
            ok = False
        if ok:
            passes.append(t)
    gaps = [b - a for a, b in zip(passes, passes[1:])]
    return sum(gaps) / len(gaps), min(gaps), max(gaps)


FPS = [20, 30, 40, 60, 90, 125, 250]
print("strict timer mean period (ms): stock | on code value | on today")
for ms in (100, 150, 300, 450, 600, 1000):
    print(" %4d:" % ms, " ".join("%d:%.0f/%.0f/%.0f" % (f, strict_rate(f, ms, False), strict_rate(f, ms, True),
                                                       strict_rate(f, ms, True, True)) for f in FPS))
print("think mean period: stock | on")
for p in (50, 100, 1000):
    print(" %4d:" % p, " ".join("%d:%.1f/%.1f" % (f, think_rate(f, p, False), think_rate(f, p, True)) for f in FPS))
print("sentry fire (200 nested in 100 think): stock | on today")
print("  ", " ".join("%d:%.1f/%.1f" % (f, nested_rate(f, 200, 100, False), nested_rate(f, 200, 100, True)) for f in FPS))
print("turretG2 wait 150 nested: stock | on today")
print("  ", " ".join("%d:%.1f/%.1f" % (f, nested_rate(f, 150, 100, False), nested_rate(f, 150, 100, True)) for f in FPS))
print("saber pass interval mean/min/max")
print("  ", " ".join("%d:%.2f/%d/%d" % ((f,) + saber_passes(f)) for f in FPS))
