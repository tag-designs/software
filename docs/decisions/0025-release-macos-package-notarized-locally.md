---
type: decision
status: accepted
summary: The released macOS DMG is notarized and stapled on the developer's Mac by sign-latest, after re-signing, with notarytool credentials held in that Mac's keychain; nothing new reaches CI.
---

# 0025. Release: the macOS package is notarized locally

Date: 2026-10-07

Extends [0024](0024-release-macos-package-built-on-ci-signed-locally.md), which
still governs who builds the DMG and where the Developer ID key lives.

## Context

Until now the released apps were Developer ID signed but not notarized. On a
download macOS blocks an unnotarized app on first launch, so every user had to
clear quarantine in a terminal, or go to System Settings -> Privacy & Security
-> Open Anyway once for each of the thirteen apps. Notarizing needs a member of
the Indiana University Apple Developer team, which the release signer now is.

## Decision

`host/tools/sign-ci-macos.sh`, which `sign-latest` runs, notarizes the image it
has just re-signed, on the same Mac:

1. every Mach-O in every bundle is signed with the hardened runtime and a
   secure timestamp (`cmake/MacosCodesignBundle.cmake`; before this change only
   the outer bundle was timestamped, which the notary service rejects);
2. the new DMG is itself signed with the Developer ID identity;
3. it is submitted with `xcrun notarytool submit --wait`, using the keychain
   profile `tag-notary` (overridable with `MACOS_NOTARY_PROFILE`); anything but
   `Accepted` fails the run and prints the notary log;
4. the ticket is stapled to the DMG and validated;
5. `host/tools/verify-macos-dmg.sh --notarized` requires the stapled ticket,
   and requires Gatekeeper to accept the image and every app as
   `Notarized Developer ID`.

The credentials are an app-specific password for a team member's Apple ID,
stored once with `xcrun notarytool store-credentials`. Like the signing key,
they stay on that Mac. `--no-notarize` signs without notarizing.

## Alternatives considered

- **Notarize in CI.** It needs the notary credentials in a public
  repository's secrets, and the image would have to be signed there first,
  which 0024 rejects. Rejected.
- **Keep shipping unnotarized.** It works, but every user pays thirteen manual
  approvals, or a terminal command, for something the release signer can now
  do once.
- **Staple each app instead of the DMG.** An app dragged out of a stapled,
  notarized image is still accepted (see Evidence), because Gatekeeper looks
  the ticket up online, and the stapled image covers an offline first open of
  the image itself. Stapling 13 bundles would mean submitting each one, or
  re-making the image after stapling. Not needed.

## Evidence

Measured on 2026-10-07 with the attested `workflow_dispatch` artifact of run
37466297592 (commit de440508), signed with `--no-upload`:

- Submission `8937bdcf-a990-4b12-a03b-1906e606e41c` came back `Accepted`,
  "Ready for distribution", with no issues in the notary log, on the first
  submission.
- `stapler validate` passed, and `spctl` accepted the image and all 13 apps as
  `source=Notarized Developer ID`.
- After a copy of the DMG was given a Safari quarantine attribute, the image
  and the `qtmonitor` and `tag-info` apps copied out of it (which inherited
  the quarantine) were each accepted as `Notarized Developer ID`.
- Copied to a Mac mini running macOS 26 that had not seen the build,
  `btviz`, `sensorviz` and `qtcalibrate` each opened.

## Consequences

- Signing now makes one timestamp request to Apple per Mach-O, several hundred
  for a full package, and submits about 200 MB to the notary service. A
  release needs the network and a few extra minutes.
- Three of the thirteen apps have been launched from a notarized image. The
  first notarized release still needs the full download-and-launch test in
  [the release procedure](../release/release-procedure.md#3-releasing-the-host-tools),
  every app, from the release page.
- `host/tools/release-macos.sh`, the fully local fallback, signs but does not
  notarize. A package from it ships with the old per-app approval.
- v3.0.1 and earlier stay unnotarized. The README keeps the approval
  instructions for them.
