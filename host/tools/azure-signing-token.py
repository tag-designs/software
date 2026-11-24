#!/usr/bin/env python3
"""Print an Entra ID access token for Azure Artifact Signing.

sign-ci-windows.sh uses this when the Azure CLI is not installed: it needs only
a bearer token for the Artifact Signing data plane, which jsign then presents
for each signature. The token is obtained interactively in the browser, as the
signed-in user, who must hold the Artifact Signing Certificate Profile Signer
role on the account.

Needs the ``azure-identity`` package, for example in a dedicated venv::

    python3 -m venv ~/.venvs/azure-signing
    ~/.venvs/azure-signing/bin/pip install azure-identity

Usage: azure-signing-token.py TENANT_ID

The token is written to standard output and nothing else is.
"""

import sys

#: The Artifact Signing data-plane scope.
SCOPE = "https://codesigning.azure.net/.default"


def main() -> int:
    """Sign in through the browser and print the access token.

    Returns the process exit status: 0 on success, 2 on a usage error, 1 when
    ``azure-identity`` is missing or sign-in fails.
    """
    if len(sys.argv) != 2:
        print("usage: azure-signing-token.py TENANT_ID", file=sys.stderr)
        return 2
    try:
        from azure.identity import InteractiveBrowserCredential
    except ImportError:
        print(
            "azure-identity is not installed in this Python; see the comment at "
            "the top of azure-signing-token.py",
            file=sys.stderr,
        )
        return 1
    try:
        credential = InteractiveBrowserCredential(tenant_id=sys.argv[1])
        print(credential.get_token(SCOPE).token)
    except Exception as error:  # noqa: BLE001 - report any sign-in failure plainly
        print(f"could not get a token: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
