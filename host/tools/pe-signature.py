#!/usr/bin/env python3
"""Inspect and repack the Authenticode signatures of a Windows package.

A helper for sign-ci-windows.sh, which signs on macOS where there is no
``Get-AuthenticodeSignature`` or ``signtool``. It reads the PE security
directory directly; it does not validate the certificate chain (that needs
Windows' trust store), so a signed package should still be checked once on
Windows -- see docs/release/windows-code-signing.md.

Commands:

``unsigned DIR``
    Print every ``.exe`` and ``.dll`` under DIR that carries no signature, one
    absolute path per line.
``check DIR``
    Check that every ``.exe`` and ``.dll`` under DIR carries a signature with
    an RFC 3161 timestamp, and print each file's signer. Exit 1 if any does
    not.
``repack ORIGINAL.zip DIR OUT.zip``
    Write OUT.zip with exactly the entries, order, timestamps and permissions
    of ORIGINAL.zip, taking each file's contents from DIR, where it was
    unpacked and signed.
"""

import os
import struct
import subprocess
import sys
import tempfile
import zipfile

#: File types that carry Authenticode signatures in our packages.
PE_SUFFIXES = (".exe", ".dll")

#: DER encoding of OID 1.3.6.1.4.1.311.3.3.1 (szOID_RFC3161_counterSign), the
#: unsigned attribute holding an RFC 3161 timestamp in an Authenticode
#: signature.
RFC3161_TIMESTAMP_OID = bytes.fromhex("060a2b060104018237030301")

#: Index of IMAGE_DIRECTORY_ENTRY_SECURITY in the optional header's data
#: directories.
SECURITY_DIRECTORY = 4


def pe_files(root):
    """Yield the absolute path of every ``.exe`` and ``.dll`` under ``root``, sorted."""
    found = []
    for directory, _, names in os.walk(root):
        for name in names:
            if name.lower().endswith(PE_SUFFIXES):
                found.append(os.path.abspath(os.path.join(directory, name)))
    yield from sorted(found)


def signature_blob(path):
    """Return the PKCS #7 signature of the PE file at ``path``, or None if unsigned.

    Raises ValueError if the file is not a well-formed PE image.
    """
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:2] != b"MZ":
        raise ValueError(f"{path}: not a PE file (no MZ header)")
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe_offset:pe_offset + 4] != b"PE\0\0":
        raise ValueError(f"{path}: not a PE file (no PE signature)")
    optional = pe_offset + 24
    magic = struct.unpack_from("<H", data, optional)[0]
    if magic == 0x10B:  # PE32
        directories = optional + 96
    elif magic == 0x20B:  # PE32+
        directories = optional + 112
    else:
        raise ValueError(f"{path}: unknown optional header magic {magic:#x}")
    address, size = struct.unpack_from("<II", data, directories + 8 * SECURITY_DIRECTORY)
    if address == 0 or size == 0:
        return None
    # WIN_CERTIFICATE: dwLength, wRevision, wCertificateType, then the blob.
    length = struct.unpack_from("<I", data, address)[0]
    return data[address + 8:address + length]


def signer_subject(blob):
    """Return the subject of the signing certificate in ``blob``, as openssl prints it.

    The signature carries the signer's certificate and the intermediates above
    it; the signer is the leaf, the one certificate that issued no other.
    Returns "?" if openssl cannot read the blob.
    """
    with tempfile.NamedTemporaryFile(suffix=".p7b") as handle:
        handle.write(blob)
        handle.flush()
        result = subprocess.run(
            ["openssl", "pkcs7", "-inform", "DER", "-in", handle.name, "-print_certs", "-noout"],
            capture_output=True, text=True, check=False)
    lines = result.stdout.splitlines()
    subjects = [line.split("=", 1)[1].strip() for line in lines if line.startswith("subject=")]
    issuers = {line.split("=", 1)[1].strip() for line in lines if line.startswith("issuer=")}
    leaves = [s for s in subjects if s not in issuers]
    return (leaves or subjects or ["?"])[0]


def command_unsigned(root):
    """Print the unsigned PE files under ``root``. Returns the exit status."""
    for path in pe_files(root):
        if signature_blob(path) is None:
            print(path)
    return 0


def command_check(root):
    """Check every PE file under ``root`` is signed and timestamped. Returns the exit status."""
    bad = 0
    for path in pe_files(root):
        relative = os.path.relpath(path, root)
        blob = signature_blob(path)
        if blob is None:
            print(f"UNSIGNED      {relative}")
            bad += 1
        elif RFC3161_TIMESTAMP_OID not in blob:
            print(f"NO TIMESTAMP  {relative}")
            bad += 1
        else:
            print(f"ok            {relative}  [{signer_subject(blob)}]")
    return 1 if bad else 0


def command_repack(original, root, out):
    """Rebuild ``original`` as ``out`` with file contents from ``root``. Returns the exit status."""
    with zipfile.ZipFile(original) as source, \
            zipfile.ZipFile(out, "w", allowZip64=True) as target:
        for info in source.infolist():
            if info.is_dir():
                target.writestr(info, b"")
                continue
            with open(os.path.join(root, info.filename), "rb") as handle:
                target.writestr(info, handle.read(), compress_type=info.compress_type)
    return 0


def main(argv):
    """Dispatch a command. Returns the process exit status."""
    commands = {
        "unsigned": (command_unsigned, 1),
        "check": (command_check, 1),
        "repack": (command_repack, 3),
    }
    if len(argv) < 2 or argv[1] not in commands or len(argv) - 2 != commands[argv[1]][1]:
        print(__doc__, file=sys.stderr)
        return 2
    function, _ = commands[argv[1]]
    try:
        return function(*argv[2:])
    except ValueError as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
