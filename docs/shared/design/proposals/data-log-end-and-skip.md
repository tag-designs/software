---
type: proposal
status: proposed
summary: The tag marks a hole in its data log with a new Ack.Err value, DATASKIPPED; every other reply without records ends the log, so host downloaders follow one rule and no longer read Status data counts, whose units differ by family.
---

# Skipped Pages in a Data-Log Download

A download asks the tag for the data-log page at an index, advances by the
records that page held, and stops when a reply holds none. The one thing a host
cannot infer from a reply is that an index is a hole to step over rather than
the end of the log, so today it guesses from `Status.external_data_count`, whose
unit differs on every family. This proposal adds one `Ack.Err` value the tag
returns from `data_logAck()`:

- **`DATASKIPPED`**: this index is inside the log but holds nothing; continue at
  the next index.

Every other reply without records ends the log, as it already does on every
family. Each downloader follows that one rule, implemented once in tagcore, and
no download decision reads a data count. It closes the "missing blocks" item in
the [shared contracts worklist](../../TODO.md#data-log-download).

## Why the counts cannot be the rule

Interior holes are new with IMUTag: a NAND page that fails to program is
skipped and the next page's header carries `IMUTAG_HEADER_RESYNC_STORAGE_SKIP`.
To let downloaders step over such a page, IMUTag answers `NODATA`, and the
hosts were taught "`NODATA` below the status's data count is a hole; skip one
index". That rule is correct on IMUTag only, the one family that both has holes
and counts in the units of the index:

| Firmware | `index` is | `external_data_count` counts | End of log | Holes |
| --- | --- | --- | --- | --- |
| BitTag | header record | nothing (always 0) | `OK`, short or empty list | none |
| BitTagNG | page | samples, 15 per page | `NODATA` | none |
| BitPresTag | page | samples, 12 per page (24 after `restoreLog()`) | `OK`, no payload | none |
| UIUCTag | 2-hour block | samples, 24 per block | `NODATA` | none |
| PresTag, PresTagRaw | page, or first page of a batch | samples, 60 per page | `OK`, no payload | none |
| CompassTag | page | 16-bit words, 190 per page | `OK`, no payload | none |
| IMUTag | logical page | logical pages | `NODATA` at `index >= external_blocks` | `NODATA` |

`internal_data_count` is no better: it is a header, checkpoint or record count
depending on the family, and IMUTag sets it to `UINT32_MAX` (read as -1) when
its internal region fills.

Applied to every family, the count-based rule costs:

- **Wasted requests on UIUCTag and BitTagNG.** Both answer `NODATA` at the end
  of the log while their count is in samples, so `tag-dwnld`, qtmonitor and
  `tag_dwnld.py` keep skipping one index at a time until the index reaches the
  sample count. A 30-day UIUCTag log is 360 blocks and 8,640 samples: the
  download fetches 360 pages, then makes about 8,280 requests that each return
  `NODATA`. The data is complete; the download is slow.
- **Silent truncation in qtmonitor**, which stops when the index reaches the
  count (`abstractdownload.cpp`). Most counts are inflated, which hides it; a
  low one cuts the download short.
- **No download when the count is zero.** `tag-dwnld` and qtmonitor refuse,
  though IMUTag zeroes its counts on some flash errors.
- **Meaningless progress**, such as `1/25` for a one-page CompassTag log whose
  count is in words.

Only the hole needs the firmware's help. The end of the log is already a reply
with no records on every family; what broke it was the host consulting a count.

## Alternatives considered

- **A `DATAEND` value as well**, so the tag states the end explicitly. Every
  family already ends with a reply holding no records, and every downloader
  already reads that as the end, so it would change every `data_logAck()` and
  tell the host nothing new.
- **A skip payload carrying a count** (`LogSkip { uint32 count }`), so one reply
  steps over a run of holes. IMUTag learns a page is bad only by reading it, so
  counting a run means reading ahead through NAND in one monitor call, and runs
  are short because collection stops after
  `IMUTAG_MAX_CONSECUTIVE_PAGE_ERRORS`. It is a new payload for a few requests.
- **A `next_index` on every page reply.** This would also replace the host's
  per-type record counting (BitTag entries, PresTag raw blocks), and is the
  general form of the skip payload. It touches every family and every writer,
  and is a separate proposal if that counting becomes a problem.

## The protocol change

In [`proto/tag.proto`](../../../../proto/tag.proto), appended to `Ack.Err`:

```proto
    DATASKIPPED = 9; // data log: index is inside the log but holds nothing
```

It carries no payload. `NODATA` keeps its number and meaning; for a data log it
is one of the replies that ends the log.

The `Status` comments for `internal_data_count` and `external_data_count`
change from "number of ... data entries" to say the unit is defined by each
family and that the counts are not a download bound. Their values do not
change: the state log and the SQLite `states` table keep recording them.

The proto-c generated sources under `embedded/proto-c/*/generated` are
regenerated by the build, as for any `tag.proto` change.

## The firmware change

Only IMUTag changes. In `embedded/tags/families/IMUTag/src/datalog.c`,
`data_logAck()` returns `DATASKIPPED`, with no payload, for an index below
`external_blocks` whose page fails its read, ECC check or epoch check. An index
at or past `external_blocks` keeps returning `NODATA`. The comment there that
tells host downloaders to "skip the hole and continue until
external_data_count is reached" is replaced.

No other family has holes, so none changes. A family that gains holes later
returns `DATASKIPPED` for them, and no host changes.

## The host rule

One function in `host/libraries/tagcore` classifies each data-log reply, and
`tag-dwnld` (`host/commandline/dwnld.cc`), qtmonitor
(`host/applications/qtmon/abstractdownload.cpp`) and the capture rebuild
(`host/libraries/tagcore/recovery/capturesource.cc`) all call it. The Python
example `host/python/tagclient/examples/tag_dwnld.py` mirrors it. In order:

1. `error_message` set: an error. The download fails.
2. `DATASKIPPED`: skip; continue at `index + 1`.
3. `NODATA`, or `OK` with no payload: the log has ended.
4. `OK` with a payload: give it to the log writer. A positive return advances
   the index by that many records; zero means the log has ended (BitTag's empty
   list, an empty PresTag raw batch); a negative return is an error.
5. Any other `err`: an error. Today these end the download silently.

Around the loop:

- **The count is never a bound or a gate.** The download always asks for index
  0, so a zero count produces a header-only log rather than a refusal, and it
  continues until the log ends, past any count.
- **Progress** reports records downloaded and pages skipped, without a
  denominator, because there is no unit common to all families. A denominator
  would need a new `Status` field defined in index units on every family; that
  is left until something needs it.
- **Gaps** are already recorded by the firmware: the IMUTag SQLite writer turns
  `RESYNC_STORAGE_SKIP` into an event row. The host adds the number of skipped
  indices to the tool's closing summary and adds no schema.

The host change alone fixes the UIUCTag and BitTagNG wasted requests, the
qtmonitor truncation and the zero-count refusal, because they come from the
host reading the count, not from the firmware.

## Compatibility

When this was proposed (2026-10-20), the IMUTag prototypes were at fabrication
and UIUCTag existed only as a breakout, so no fielded tag can return
`DATASKIPPED` or depends on the count-based skip. The fielded families end their
logs with `OK` and no payload or an empty list, which the new rule reads exactly
as the old one did.

| Host | Firmware | Result |
| --- | --- | --- |
| new | any family but IMUTag | correct, and without the faults above |
| new | IMUTag before the change | the download ends at the first hole, which IMUTag still answers with `NODATA`; acceptable while only prototypes exist |
| new | IMUTag with `DATASKIPPED` | the explicit rule |
| old | IMUTag with `DATASKIPPED` | the download ends at the first hole: `DATASKIPPED` has no payload, so the writer returns 0 |

So the host should ship before IMUTag firmware with `DATASKIPPED` is deployed.
No legacy branch is needed in the host.

## What this does not fix

These are separate firmware faults. The first two become harmless to downloads,
because counts no longer drive them, but they still misreport status. The
third is not affected by this change:

- BitPresTag `restoreLog()` sets `external_blocks` to 24 per page, twice its
  unit, until the next T_INIT.
- UIUCTag `restoreLog()` seeds its sample count with the block count, and its
  comment calls that "a valid, if stale, download bound for the host". The
  comment, and the note in the UIUCTag
  [interface section](../../../../embedded/tags/UIUCTag/design/overview.md#firmwarehost-interface)
  explaining what the count is not, should be updated with phase 1.
- **IMUTag `restoreLog()` stops at a hole.** It advances `external_blocks` over
  the last checkpoint's group only up to the first unreadable page, so a hole in
  that group ends the log there, and pages after it that hold data are not
  served. Fixing that means recovery must step over holes, not the downloader.

The tools also differ in what they keep after a failed or cancelled download:
`tag-dwnld` rolls back, qtmonitor commits, `tag_dwnld.py` leaves the partial
file. That is outside this proposal.

## Phases

1. **Host.** Add `DATASKIPPED` to `tag.proto`, the classifier in tagcore, and
   use it in `tag-dwnld`, qtmonitor, the capture rebuild and `tag_dwnld.py`.
   Correct the `NODATA`-at-end wording in `proto/tag_service.proto` and the
   [tag-server README](../../../../host/tagserver/README.md), and the UIUCTag
   comments above. Check the classifier with a standalone assertion program, as
   in `host/libraries/tagcore/test/`, and download one tag of each family on
   hardware, comparing record counts and request counts against the current
   host.
2. **IMUTag firmware**, with the prototypes: return `DATASKIPPED` for holes, and
   check a download across a forced hole on hardware.
