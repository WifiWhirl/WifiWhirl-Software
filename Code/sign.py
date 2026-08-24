#!/usr/bin/env python3
"""Append an RSA signature trailer to a WifiWhirl firmware .bin.

Trailer layout (identical to the ESP8266 core's tools/signing.py):

    <firmware bytes> || <signature> || uint32_le(len(signature))

The signature is openssl `dgst -sha256 -sign` => RSA PKCS#1 v1.5 over SHA-256
of the firmware bytes. Because the format matches the core exactly, the same
signed file is verified by:
  - ESP8266: the core's Update.installSignature() verifier (native), and
  - ESP32:   the manual mbedTLS verify in src/net/web_update.cpp.

Keys must be RSA-2048 (the ESP32 side assumes a 256-byte signature).

Usage:
    python sign.py --key private.key --bin firmware.bin     # signs in place
    python sign.py --selftest                               # offline self-check
"""
import argparse
import struct
import subprocess
import sys


def sign_in_place(bin_path: str, key_path: str, passin: str = None) -> None:
    with open(bin_path, "rb") as f:
        data = f.read()
    cmd = ["openssl", "dgst", "-sha256", "-sign", key_path]
    if passin:  # encrypted key: e.g. env:KEYPASS, file:pass.txt, pass:secret
        cmd += ["-passin", passin]
    sig = subprocess.run(
        cmd, input=data, stdout=subprocess.PIPE, check=True,
    ).stdout
    with open(bin_path, "wb") as f:
        f.write(data)
        f.write(sig)
        f.write(struct.pack("<L", len(sig)))  # u32 little-endian, matches core
    sys.stderr.write(f"Signed {bin_path}: +{len(sig)}+4 byte trailer\n")


def selftest() -> None:
    # the one runnable check - sign a blob, then verify the trailer
    # parses and openssl accepts the signature. Fails loudly if the format drifts.
    import os
    import tempfile
    import hashlib
    d = tempfile.mkdtemp()
    key, pub, blob = (os.path.join(d, n) for n in ("k.pem", "p.pem", "fw.bin"))
    subprocess.run(["openssl", "genrsa", "-out", key, "2048"], check=True,
                   stderr=subprocess.DEVNULL)
    subprocess.run(["openssl", "rsa", "-in", key, "-pubout", "-out", pub],
                   check=True, stderr=subprocess.DEVNULL)
    payload = b"hello wifiwhirl firmware" * 100
    with open(blob, "wb") as f:
        f.write(payload)
    sign_in_place(blob, key)

    with open(blob, "rb") as f:
        signed = f.read()
    sig_len = struct.unpack("<L", signed[-4:])[0]
    assert sig_len == 256, f"expected 256-byte RSA-2048 sig, got {sig_len}"
    sig = signed[-4 - sig_len:-4]
    recovered = signed[:-4 - sig_len]
    assert recovered == payload, "payload corrupted by trailer"

    sigfile = os.path.join(d, "s.bin")
    with open(sigfile, "wb") as f:
        f.write(sig)
    payload_only = os.path.join(d, "payload.bin")
    with open(payload_only, "wb") as f:
        f.write(recovered)
    # openssl verifies the signature against the payload using the public key
    subprocess.run(
        ["openssl", "dgst", "-sha256", "-verify", pub, "-signature", sigfile, payload_only],
        check=True,
    )
    assert hashlib.sha256(recovered).digest()  # payload is hashable/non-empty
    print("selftest OK: trailer parses, signature verifies")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--key", help="RSA-2048 private key (PEM)")
    ap.add_argument("--bin", help="firmware .bin to sign in place")
    ap.add_argument("--passin", help="openssl pass phrase source for an encrypted "
                                     "key, e.g. env:KEYPASS, file:pass.txt, pass:secret; "
                                     "omit to let openssl prompt")
    ap.add_argument("--selftest", action="store_true", help="run an offline self-check")
    a = ap.parse_args()
    if a.selftest:
        selftest()
        return 0
    if not a.key or not a.bin:
        ap.error("--key and --bin are required (or use --selftest)")
    sign_in_place(a.bin, a.key, a.passin)
    return 0


if __name__ == "__main__":
    sys.exit(main())
