---
type: worklist
status: current
summary: Open work on the contracts host and firmware share, starting with the monitor transport's stale VC_CORERESET after a timeout.
---

# Shared Contracts Worklist

Delete an item when it is done.

## Monitor transport

- **Clear `VC_CORERESET` on the host's timeout-resume path.**
  `host/libraries/tagcore/tagmonitor.cc` has two resume paths after a failed
  monitor call. The reset path clears the flag. The timeout path is taken when
  the timeout probe has halted the target, and it resumes with
  `(demcr | MON_EN | VC_CORERESET)`, leaving the flag set. Since
  [decision 0006](../decisions/0006-monitor-u3-attachment-is-the-shared-session.md)
  a stale flag can no longer stop a U3 tag sleeping. It still leads
  `monitorResetRecoveryActive()` to read the next boot as a monitor attach. Test
  across L4 and U3 targets before changing it, because both transports read
  that bit.

## Data-log download

- **Decide how a download handles missing blocks.** The host asks for a page
  at an index and advances by the records the page held, until a page comes
  back with nothing in it. Today:
  - **End of log:** most tags return an `Ack` with no payload; BitTag returns an
    empty `bittag_data_log`.
  - **Missing block:** `NODATA` while the index is below the status's data count
    is skipped one index at a time, by `tag-dwnld`
    (`host/commandline/dwnld.cc`) and qtmonitor
    (`host/applications/qtmon/abstractdownload.cpp`) alike. Past the count it
    ends the log.
  - **The count is only a hint,** and the tools disagree on it. `tag-dwnld`
    refuses to download when it is zero, though firmware paths reset it (for
    example IMUTag's `state_run.c` on a flash error). qtmonitor stops when the
    index reaches it, so a low count truncates the download silently.

  To decide:
  - how the firmware tells a missing or unreadable block apart from the end of
    the log;
  - the unit a skip moves: a record for BitTag, a block or page elsewhere;
  - whether the count bounds the download or only reports progress;
  - whether the host records a gap in the stored log.

  Then make `tag-dwnld`, qtmonitor and the `tagclient` example
  (`host/python/tagclient/examples/tag_dwnld.py`) follow the same rule.
