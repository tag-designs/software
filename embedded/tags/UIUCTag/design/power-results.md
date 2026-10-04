---
type: results
status: current
summary: Append-only log of UIUCTag power measurements. No entries yet; the first qualification sets the baseline.
---

# UIUCTag Power Measurement Log

**Append-only.** Each completed measurement gets one entry with a timestamp,
the conditions it was taken under, and the numbers. Never edit or delete an
entry — a measurement that turned out to be wrong gets a later entry saying
so, because the wrong ones are how the reasoning is reconstructed.

Procedure: [`power-test-plan.md`](power-test-plan.md), and the shared
[power testing procedure](../../../../docs/bench/power-testing.md).

## Format

```
### YYYY-MM-DD HH:MM  <short title>
- **build**: release tag and image SHA-256, or git hash and whether dirty
- **board**: UUID, and which physical unit if it matters
- **conditions**: supply, what was attached, settle time, window
- **result**: the numbers
- **notes**: anything that qualifies them
```

Rig, unless an entry says otherwise: ST-Link, Joulescope JS220 via
`joulescope_server.py --use-server`, `charge/time` figure, supply ~2.496 V,
qtmonitor and the Joulescope desktop app detached.

---

_No measurements recorded yet._
