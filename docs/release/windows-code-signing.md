---
type: procedure
status: current
summary: How the Windows host tools package is Authenticode-signed with Azure Artifact Signing -- locally from a Mac with sign-ci-windows.sh (the current route), or in CI by the sign-windows job once a CI identity holds the signing role -- with the one-time setup, how to check a signed ZIP, and what goes wrong.
---

# Signing the Windows Package

The Windows ZIP of the host tools is Authenticode-signed with Azure Artifact
Signing (formerly Trusted Signing). Microsoft holds the key; each signing uses a
short-lived (72-hour) certificate issued to the validated identity, and every
signature is timestamped, so it stays valid after that certificate expires.
Signed executables show a verified publisher instead of "Unknown publisher" in
SmartScreen and are not blocked by Smart App Control for lacking a signature.

The publisher Windows shows is the certificate's common name, which comes
from the account's identity validation: **indiana.onmicrosoft.com**
(organization indiana.onmicrosoft.com, OU Computer Science, Bloomington). A
different name would need a new identity validation and a certificate profile
made from it.

There are two routes to the same result:

- **Locally** (the current route): `release.yml` attaches the unsigned ZIP to
  the draft release, and `host/tools/sign-ci-windows.sh` -- the counterpart of
  the macOS `sign-ci-macos.sh` -- downloads it, signs it as the signed-in Azure
  user and replaces it on the draft.
- **In CI**: the `sign-windows` job in
  [`release.yml`](../../.github/workflows/release.yml) signs it before the
  draft is opened. It needs a CI identity holding the signing role, which at IU
  only a subscription administrator can grant, so it is off until
  `WINDOWS_SIGNING_IN_CI` is set.

Both sign the same files, the same way:

- every `.exe` and `.dll` in the ZIP that is **not already signed** -- our
  executables, `tagcore_c.dll` and any unsigned third-party DLL we ship. The
  Microsoft runtime DLLs that `InstallRequiredSystemLibraries` copies in keep
  Microsoft's own signature;
- SHA-256, timestamped by `http://timestamp.acs.microsoft.com`;
- then every `.exe` and `.dll` must carry a signature and a timestamp, and the
  ZIP keeps its name and layout.

## Configuration

Both routes read the same repository variables (Settings > Secrets and
variables > Actions > Variables). None is secret.

| Variable | Value | Where to find it |
| --- | --- | --- |
| `ARTIFACT_SIGNING_ENDPOINT` | the account's regional endpoint, e.g. `https://eus.codesigning.azure.net/` | signing account > Overview > *Account URI* |
| `ARTIFACT_SIGNING_ACCOUNT` | the account name | signing account > Overview |
| `ARTIFACT_SIGNING_PROFILE` | the certificate profile name | signing account > Objects > Certificate profiles |
| `AZURE_TENANT_ID` | the Entra ID tenant | Microsoft Entra ID > Overview |
| `AZURE_SUBSCRIPTION_ID` | CI only: the subscription holding the account | signing account > Overview |
| `AZURE_CLIENT_ID` | CI only: the CI identity's client ID | managed identity (or app registration) > Overview |
| `WINDOWS_SIGNING_IN_CI` | CI only: `true` switches the `sign-windows` job on | -- |

The local script takes each value from the environment if set, otherwise from
the repository variable, with `gh variable get`.

## Signing locally

### Once

1. **The role.** Your own Azure account needs **Artifact Signing Certificate
   Profile Signer** on the signing account (signing account > Access control
   (IAM) > View my access). Owner or Contributor alone cannot sign.
2. **jsign**, which signs Windows files from macOS: `brew install jsign`.
3. **A way to sign in to Azure**, either the Azure CLI (`az login --tenant
   <tenant>`) or, much smaller, `azure-identity` in a venv, which signs in
   through the browser:

   ```sh
   python3 -m venv ~/.venvs/azure-signing
   ~/.venvs/azure-signing/bin/pip install azure-identity
   ```

4. **The variables** above, at least the first four.

### Each release

After the tag's `release.yml` run has finished and opened the draft:

```sh
cmake --build <build-dir> --target sign-latest-windows
# or, for a particular tag:
host/tools/sign-ci-windows.sh vX.Y[.Z]
```

The script finds the tag's run and checks the tag still names the commit it
built, downloads `host-tools-windows-x64`, checks its build provenance
attestation, signs the unsigned files with jsign, checks every signature and
timestamp (printing each signer), repacks the ZIP and uploads it to the draft
with `--clobber`, replacing the unsigned one. It never changes a published
release. `--run ID --no-upload` signs a manual run's ZIP for testing.

The macOS DMG is signed in the same way by `sign-latest`; see
[release-procedure.md](release-procedure.md). Do both before publishing.

## Signing in CI

The job authenticates with OpenID Connect: the run's GitHub token is exchanged
for an Entra ID token through a federated credential, so there is no client
secret to store or rotate.

1. **An identity for the job** -- either of:
   - a **user-assigned managed identity** (search the portal for *Managed
     Identities* > Create, in the signing account's resource group), which
     needs only rights in the subscription;
   - or an **app registration** (Microsoft Entra ID > Manage > App
     registrations > New registration), hidden or greyed out when the tenant
     restricts who may register applications.

   Its client ID is `AZURE_CLIENT_ID`.
2. **Federated credential.** On the managed identity: Settings > Federated
   credentials > Add credential (on an app registration: Certificates &
   secrets > Federated credentials), scenario *GitHub Actions deploying Azure
   resources*, organization `tag-designs`, repository `software`, entity
   **Environment**, name `windows-signing`:

   | Field | Value |
   | --- | --- |
   | Issuer | `https://token.actions.githubusercontent.com` |
   | Subject identifier | `repo:tag-designs/software:environment:windows-signing` |
   | Audience | `api://AzureADTokenExchange` |

   The repository uses GitHub's default, name-based subject. If the portal
   builds one with numeric IDs instead, enter the subject by hand under *Other
   issuer*.
3. **Role.** Signing account > Access control (IAM) > Add role assignment >
   **Artifact Signing Certificate Profile Signer**, *Assign access to*
   **Managed identity** (or *User, group, or service principal* for an app
   registration), and select the identity. This needs Owner, User Access
   Administrator or Role Based Access Control Administrator on the account or
   above; at IU, ask the subscription's administrator.
4. **Environment.** GitHub Settings > Environments > New environment
   `windows-signing`; deployment branches and tags: *Selected*, tag rule `v*`
   and branch `main`.
5. **Variables**: `AZURE_SUBSCRIPTION_ID`, `AZURE_CLIENT_ID`, then
   `WINDOWS_SIGNING_IN_CI` = `true` at **repository** level (the job's `if:`
   is evaluated before it enters the environment).
6. **Test** with a manual run on `main` (Actions > Release host tools > Run
   workflow, `legacy` unchecked); it signs but never publishes. The *Verify
   every signature* step lists each file and its signer.

Once a tag's draft carries a ZIP signed in CI, `sign-ci-windows.sh` stops with
"no unsigned .exe or .dll", which is the signal that it is not needed.

## Checking a signed ZIP on Windows

Neither route checks the certificate chain on macOS -- that needs Windows'
trust store -- so check the first signed package on a Windows machine:

```powershell
Get-AuthenticodeSignature .\tag_tools\lib\tagmonitor.exe | Format-List Status, SignerCertificate, TimeStamperCertificate
```

`Status` should be `Valid` and both certificates present. The Properties >
Digital Signatures tab in Explorer shows the same.

## When it fails

| Symptom | Likely cause |
| --- | --- |
| jsign reports `403 Forbidden` | the signed-in user (or CI identity) lacks Certificate Profile Signer on the account, or the endpoint is for another region |
| jsign reports `401` | the token is for the wrong tenant, or has expired; sign in again |
| the script reports no provenance attestation | the run predates Windows attestation; `--no-attestation` signs it without that assurance |
| the script reports no unsigned files | the ZIP is already signed, by CI or an earlier run |
| *Log in to Azure* fails with `AADSTS70021` | CI only: the federated credential's subject does not match the job's environment |
| the CI job fails before any step, citing the environment | the ref is not allowed by the environment's deployment rules |
| a file is reported with no timestamp | the timestamp server was unreachable; run again |
| an existing signature does not verify (CI) | a shipped DLL is signed but corrupt or untrusted; find where it comes from rather than re-signing it |

Microsoft's own list:
[common error codes and mitigations](https://learn.microsoft.com/azure/artifact-signing/faq#common-error-codes-and-mitigations).

## What it does not cover

- **macOS** is signed and notarized locally with the Developer ID key; see
  [release-procedure.md](release-procedure.md).
- **Linux** is not signed; its tarball carries a build provenance attestation.
- **Local Windows builds** are not signed.
