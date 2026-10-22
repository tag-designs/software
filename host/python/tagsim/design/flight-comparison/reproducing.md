---
type: procedure
status: current
summary: How to rebuild the pattern-flight comparison - the tools it uses (tagsim's run, filter, score, compare and batch commands, tag-log-import, dataprocessing, scripts/flight_comparison.py), their setup, and the exact commands that produce its simulated logs, filter runs, pages, tables and supporting figures.
---

# Rebuilding the Pattern-Flight Comparison

Everything in [pattern-flight-comparison.md](pattern-flight-comparison.md)
comes from the commands below, run from `host/python/tagsim`. They need the
private flight logs; the pages and tables they produce do not contain
positions and can be committed.

## The tools

| Tool | Where | What it does here |
| --- | --- | --- |
| `tagsim run` | [cli.py](../../src/tagsim/cli.py) | One flight log to a simulated IMUTag log (`NAME.db3`) and its truth (`NAME.truth.db3`) |
| `tagsim filter` | [navfilter.py](../../src/tagsim/navfilter.py) | The navigation filter on a log: `FilterForward`, `FilterSmoothed` in `NAME-filt.db3` |
| `tagsim score` | [score.py](../../src/tagsim/score.py) | An estimate against truth: `NAME-smoothed.score.db3` |
| `tagsim compare` | [compare.py](../../src/tagsim/compare.py) | The side-by-side attitude and altitude page |
| `tagsim batch` | [cli.py](../../src/tagsim/cli.py) | All of the above over a directory of logs, with `--filter` |
| `tag-log-import` | `host/commandline` | Decodes the simulated pages into the SQLite log (the production importer) |
| `dataprocessing` | `host/commandline` | The tag's one-sample compass, which `batch` also scores |
| `scripts/flight_comparison.py` | [scripts/](../../scripts/flight_comparison.py) | Runs the three conditions, writes the pages and tables, and computes the supporting figures |

Every `tagsim` command and option is in the [README](../../README.md); how
the simulator and filter work is in [simulator.md](../simulator.md) and
[navigation-filter.md](../navigation-filter.md).

## Set up once

1. Check out the private data repository beside this one, as `data/`. The
   flight logs are in `data/ardupilot/pattern-2023`.
2. Build the two host tools:

   ```bash
   cmake --build <build-dir> --target tag-log-import dataprocessing
   ```

3. Make a Python environment with tagsim (Python 3.10 or later):

   ```bash
   python3 -m venv ~/.venvs/tagsim
   ```

   ```bash
   ~/.venvs/tagsim/bin/pip install pytest -e host/python/tagclient -e "host/python/tagsim[test]"
   ```

   (from the repository root).
4. Point tagsim at the tools and the logs:

   ```bash
   export TAG_LOG_IMPORT_BIN=<build-dir>/Release/bin/tag-log-import
   export DATAPROCESSING_BIN=<build-dir>/Release/bin/dataprocessing
   export TAGSIM_LOG_DIR=../../../../data/ardupilot/pattern-2023
   ```

Keep every generated file outside the repository, for example under
`~/Documents/tagsim-output`: the simulated logs and truth files carry the
flying site's positions. Only the pages go in the repository.

## 1. Simulate and filter every flight

```bash
~/.venvs/tagsim/bin/python scripts/flight_comparison.py run --log-dir $TAGSIM_LOG_DIR
```

This runs `tagsim batch --filter` three times, one per condition, into
`~/Documents/tagsim-output/comparison/{basic,realistic,miscalibrated}`
(`--out DIR` moves them):

| Condition | `tagsim batch` arguments beyond `--filter --seed 0` |
| --- | --- |
| `basic` | `--set-error` `accel_scale_error=0`, `gyro_scale_error=0`, `accel_misalignment_deg=0`, `gyro_misalignment_deg=0`, `accel_bias_gm_mg=0`, `gyro_bias_gm_dps=0` |
| `realistic` | none |
| `miscalibrated` | `--calibration perturbed` |

For each flight, `batch` simulates the log, runs `dataprocessing`'s compass,
runs the filter with and without pressure, scores everything, and writes
`scores.csv`. Expect a few minutes per flight per condition (the filter is
Python). `--only f173 f177 f176` limits it to the flights shown.

To repeat a single flight by hand, for example f173 in the miscalibrated
condition:

```bash
~/.venvs/tagsim/bin/tagsim run --log $TAGSIM_LOG_DIR/00000173.BIN -o f173.db3 --calibration perturbed
```

```bash
~/.venvs/tagsim/bin/tagsim filter --log f173.db3 --truth f173.truth.db3 -o f173-filt.db3
```

```bash
~/.venvs/tagsim/bin/tagsim score --truth f173.truth.db3 --estimate f173-filt.db3 --table FilterSmoothed -o f173-smoothed.score.db3
```

On 2026-10-22 this reproduced the published f173 miscalibrated figures
exactly (0.2478° median, 0.4054° maximum; 1.66 cm altitude).

## 2. Write the pages

```bash
~/.venvs/tagsim/bin/python scripts/flight_comparison.py pages --pages design/flight-comparison/pages f173 f177 f176
```

One page per flight and condition, `pages/<flight>-<condition>.html`, at the
source log's 25 Hz, about 0.5 MB each. Any flight works; the same page for a
single run is

```bash
~/.venvs/tagsim/bin/tagsim compare --truth f173.truth.db3 --score f173-smoothed.score.db3 -o f173-compare.html
```

Open a page in a browser with WebGL and an internet connection (three.js
loads from a CDN). Drag a view to turn both cameras; space plays and pauses;
the arrow keys step one sample; click an error strip to jump there;
`#t=SECONDS` on the URL opens at a time.

## 3. Print the tables

```bash
~/.venvs/tagsim/bin/python scripts/flight_comparison.py tables f173 f177 f176
```

A Markdown table per flight, with a line of facts about it (simulated
length, how it starts, glitch repairs, clipping), for pasting into the
results document.

## The figures behind the text

```bash
~/.venvs/tagsim/bin/python scripts/flight_comparison.py survey $TAGSIM_LOG_DIR/00000173.BIN
```

Every message type in the log, with its count, rate and columns: the
"what the logs contain" table. (`FMT`, `FMTU`, `UNIT` and `MULT` are written
at the start, so their "rates" are meaningless.)

```bash
~/.venvs/tagsim/bin/python scripts/flight_comparison.py spectra ~/Documents/tagsim-output/comparison/realistic/f173.truth.db3
```

Where the power of the ideal specific force and body rate lies: the
bandwidth figures.

```bash
~/.venvs/tagsim/bin/python scripts/flight_comparison.py deadreckon --run ~/Documents/tagsim-output/comparison/realistic f173 f177
```

Horizontal drift of the double-integrated accelerometer, with the filter's
and with the true attitude: the "no horizontal position" figures.

The strapdown consistency figures (0.02°, 1.9 cm/s, 5.2 cm over 627 s) are
printed by `tagsim run` and stored in the truth file's
`SimRun.ConsistencyJson`.

## Using earlier runs

Each subcommand takes `--run-dir CONDITION=PATH` to read a condition from
another directory. The published pages and tables were made from runs kept
under `~/Documents/tagsim-output/final`, made with `tagsim batch --filter`
as above:

```bash
F=~/Documents/tagsim-output/final
```

```bash
~/.venvs/tagsim/bin/python scripts/flight_comparison.py --run-dir basic=$F/simple/full --run-dir realistic=$F/harder-true/full --run-dir miscalibrated=$F/harder-perturbed/full pages --pages design/flight-comparison/pages f173 f177 f176
```

## After changing the document

From the repository root:

```bash
docs/tools/docs.py index
```

```bash
docs/tools/docs.py check
```
