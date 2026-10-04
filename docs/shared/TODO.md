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
