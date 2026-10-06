---
type: decision
status: superseded
superseded-by: docs/decisions/0024-release-macos-package-built-on-ci-signed-locally.md
summary: CI builds the macOS host package ad-hoc signed as a build check only; the shipped DMG is Developer ID signed locally by release-macos.sh, because the signing key is not put in a public repository's secrets.
---

# 0012. Release: the macOS package is signed off CI

Date: 2026-09-28

The macOS package that ships is built and Developer ID signed on a developer's
machine; the CI package is never attached to a release. Extracted verbatim from
the "Tagged Releases" section of the root README, whose release material now lives in [Releasing Tag Firmware and Host Tools](../release/release-procedure.md#release-tags-and-what-ci-builds); the procedure is in
[Releasing the host tools](../release/release-procedure.md#3-releasing-the-host-tools).

## Context, decision and alternatives

macOS CI builds are signed ad-hoc (`-DMACOS_CODE_SIGN_IDENTITY=-`) rather than
with the Developer ID identity, which is not available to the runner. Signing is
not optional here. With `MACOS_SIGN_APPS=OFF` nothing runs `codesign` on the
bundle, so the only signature present is the ad-hoc one the linker applies to
arm64 Mach-O files:

```
CodeDirectory v=20400 flags=0x20002(adhoc,linker-signed)
Sealed Resources=none
qtmonitor.app: code has no resources but signature indicates they must be present
```

That signature seals the executable but writes no
`Contents/_CodeSignature/CodeResources`, which an app bundle requires, so the
bundle fails `codesign --verify` and arm64 macOS refuses to launch it. Users see
"the application is damaged and can't be opened" rather than the usual
unidentified-developer prompt.

That is why the CI macOS package is **not attached to releases**. It is a build
check and a workflow artifact, nothing more. `release.yml` opens the release as
a *draft* with only the Windows ZIP attached; the shippable macOS DMG is built
and signed on a machine holding the Developer ID certificate by
`host/tools/release-macos.sh`, uploaded to that draft by hand, and the release
is published once it is there.

Signing in CI instead would mean storing the Developer ID certificate and its
password as repository secrets and importing them into a temporary keychain
before the package step. In a public repository that makes the private key
recoverable by anyone who can run a workflow, so it is deliberately not done.
`install_macos_codesign` in `cmake/DeployQt.cmake` already selects the hardened
runtime (`--options runtime`) and a secure timestamp for a real identity and
omits them for `-`, so the local build needs no special handling.
