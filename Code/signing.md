# Firmware signing

The web update feature lets a device download a firmware image over plain HTTP
and flash itself. Plain HTTP is safe here because the image is **signed**: the
signature, not the transport, is the trust anchor. A device only installs an
image whose signature verifies against the public key compiled into its
firmware, so a man-in-the-middle or a rogue server cannot push arbitrary code.

## How it works

**Algorithm:** RSA-2048, SHA-256, PKCS#1 v1.5 (plain `openssl dgst`).

**Trailer format** (appended to `firmware.bin` by `sign.py`):

```
<firmware bytes> || <signature, 256 bytes> || uint32_le(len(signature))
```

This is byte-for-byte the format the ESP8266 Arduino core uses, so one signed
file is accepted by both platforms:

- **ESP8266** - the core's `Update` verifier checks the trailer automatically
  inside `Update.end()` once `installSignature()` is set up (no app code does
  the crypto). See `installUpdateSignature()` in `src/net/web_update.cpp`.
- **ESP32** - the Arduino `Update` lib has no such hook, so we hash the payload
  with mbedTLS and `mbedtls_pk_verify()` the trailer ourselves before flashing.
  Same key, same file. See `verifyRsa()` in `src/net/web_update.cpp`.

**Keys:**

- `private.key` - signs builds. **Secret. Never commit.** Gitignored.
- `public.key` - embedded in `src/update_pubkey.h`, compiled into the firmware.
  A public key is not a secret; it is fine to commit the header.

**Enabling it:** signing is driven by the `-DUPDATE_SIGNING` build flag in
`platformio.ini`. That one flag both compiles the
verifier into the firmware **and** makes the post-build hook
(`sign_firmware.py`) sign `firmware.bin`. Envs without the flag build unsigned
(handy for development), so the two halves can never drift apart. A build with
the flag set but no `private.key` present fails loudly rather than shipping
unsigned.

## Generating a keypair with a passphrase

A passphrase encrypts `private.key` on disk, so a leaked file is not directly
usable without the passphrase.

```sh
# 1. private key, AES-256 encrypted - openssl prompts for a passphrase
openssl genrsa -aes256 -out private.key 2048

# 2. public key (prompts for the passphrase to read the private key)
openssl rsa -in private.key -pubout -out public.key
```

`public.key` is safe to share.

### Put the public key into the firmware

Paste the contents of `public.key` into `src/update_pubkey.h`, preserving the
`\n` line breaks, replacing the committed dev key. Example:

```c
static const char UPDATE_PUBKEY_PEM[] =
    "-----BEGIN PUBLIC KEY-----\n"
    "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8A...\n"
    "...\n"
    "-----END PUBLIC KEY-----\n";
```

The public key in the firmware and the private key used to sign **must be a
matching pair**, or every update is rejected.

## Signing a build

With `-DUPDATE_SIGNING` set for the env, just build - the post-build hook signs
automatically:

```sh
pio run -e wifiwhirl_2_1_4
```

Because the key is passphrase-protected, the hook needs the passphrase:

- **Interactive (local build):** openssl prompts on the terminal during the
  build. Nothing else to do.
- **Unattended (CI / scripts):** set `SIGN_PASSIN` to any openssl pass source so
  it never prompts:

  ```sh
  SIGN_PASSIN=env:KEYPASS KEYPASS='your passphrase' pio run -e wifiwhirl_2_1_4
  # or: SIGN_PASSIN=file:/secure/pass.txt
  # or: SIGN_PASSIN=pass:literalsecret   (visible in process args - avoid)
  ```

You can also sign a binary by hand:

```sh
python3 sign.py --key private.key --bin firmware.bin            # prompts
python3 sign.py --key private.key --bin firmware.bin --passin env:KEYPASS
```

## Verifying / testing

`sign.py --selftest` signs a throwaway blob and checks the trailer parses and
the signature verifies - run it if you change the format.

To verify a signed binary by hand against your public key:

```sh
# split off the trailer, then verify the payload
python3 - <<'PY'
import struct
d = open("firmware.bin","rb").read()
n = struct.unpack("<L", d[-4:])[0]
open("payload.bin","wb").write(d[:-4-n]); open("sig.bin","wb").write(d[-4-n:-4])
PY
openssl dgst -sha256 -verify public.key -signature sig.bin payload.bin   # -> Verified OK
```
