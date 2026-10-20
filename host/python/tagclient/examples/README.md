---
type: readme
status: current
summary: Python counterparts of tag-info, tag-start, tag-stop, tag-reset, tag-dwnld and tag-peek, plus a live status monitor, written against tag-server with tagclient.
---

# tagclient Examples

Python versions of the [command-line tools](../../../commandline/README.md),
talking to the tag through [`tag-server`](../../../tagserver/README.md) instead
of USB. They follow the C++ tools' behaviour, including the waits that make
those tools reliable, so they double as worked examples of using
[`tagclient`](../README.md).

| Example | Counterpart | What it does |
| --- | --- | --- |
| [`tag_info.py`](tag_info.py) | `tag-info` | Information, configuration, status and state log; `--json` for one record |
| [`tag_start.py`](tag_start.py) | `tag-start` | Start an IDLE tag, optionally with a JSON config (`--config`, `--merge`, `--start-now`, `--set-rtc`) |
| [`tag_stop.py`](tag_stop.py) | `tag-stop` | Stop and wait for FINISHED or ABORTED |
| [`tag_reset.py`](tag_reset.py) | `tag-reset` | Stop if running, erase, wait for IDLE. Discards the log |
| [`tag_dwnld.py`](tag_dwnld.py) | `tag-dwnld` | Download the data log as raw pages, one JSON object per line |
| [`tag_peek.py`](tag_peek.py) | `tag-peek` | Read words of live memory; needs `--enable-debug` |
| [`watch_status.py`](watch_status.py) | qtmonitor's status tab | State, voltage and log counts at an interval until Ctrl-C |

## Running

Start a server, then run the examples with the `tagclient` environment's
Python. `--attach` holds the tag across several examples; without it, each
example attaches and detaches the tag itself, as the C++ tools do.

```bash
tag-server --attach --enable-debug
```

```bash
python host/python/tagclient/examples/tag_info.py
```

Every example takes `--server` (default `$TAG_SERVER`, then the server's
default socket), `--base BUS:DEVICE` for when it does the attaching, and
`--settle-timeout`. A failure prints the server's reason and the tag's `Ack`
code, and exits 1.

## Notes

- **`tag_dwnld.py` writes raw pages, not SQLite.** The first line is a header
  with the tag's information, configuration, status and state log; each
  further line is one page, the tag's `Ack` in protobuf JSON. Decoding stays
  in tagcore's writers. To page through the log, the example needs to know how
  many records each page holds. It mirrors what those writers return: BitTag
  counts its data entries, PresTag raw counts 252-byte blocks, and every other
  type counts one per page. A new log format with a different rule needs a
  matching change there.
- **State changes are posted, not performed.** Start, stop and erase return
  when the tag accepts the request; the examples poll for the new state as the
  C++ tools do.
