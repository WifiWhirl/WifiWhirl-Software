#pragma once

// RSA-2048 public key used to verify web-pulled firmware images. Only compiled
// in when the -DUPDATE_SIGNING build flag is set (see platformio.ini). A public key is
// not a secret; the matching private.key stays offline / in CI and is what
// sign.py uses to sign firmware.bin.
//
// To use your own keypair (do this for production):
//   openssl genrsa -out private.key 2048
//   openssl rsa -in private.key -pubout -out public.key
//   then paste public.key's PEM below (keep the \n line breaks).
//
// The committed key below is a throwaway dev key so signed builds work out of
// the box; replace it before shipping.

#ifdef UPDATE_SIGNING
static const char UPDATE_PUBKEY_PEM[] =
    "-----BEGIN PUBLIC KEY-----\n"
    "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAjr2lOSuVj5JQnFx27hQn\n"
    "u3tKszZEQbSS/3VUiRJ8r7/QRRenDDymDN1GLLdcBu2ijeWWqboy9SI7z8Y4JQbN\n"
    "2/52y+Ga4deRqUEGZoPmjiOz8fHmBhErieNyVi7JWfqw/3vfyrnn5+dQlj+KdkXB\n"
    "7Mch/diAal9pRmI8nYiW4FDSCQqdDb7gklC5j6q40yO56jMO6JShYbbgCat0uL8u\n"
    "J6/vgK4SjbKF7pUywgrCD2aRL1npfnnoIeblJ89H5ipFKaIgCmpPH7xToBocY9el\n"
    "Q3mUftmrOfUWMkK/FbB+c4dSbIL+m4L5u59paBXRn5osQlmh/1DwreAn9/987o4f\n"
    "VQIDAQAB\n"
    "-----END PUBLIC KEY-----\n";
#endif
