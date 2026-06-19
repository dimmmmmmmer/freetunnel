#!/usr/bin/env bash
# Generate an Ed25519 key pair for signing release SHA256SUMS.txt manifests.
# The public key must be embedded in include/core/ReleaseSigning.h;
# the private key must be added as the ED25519_SIGNING_KEY GitHub secret.
set -euo pipefail

OUT="${1:-release-signing.pem}"
PUB="${OUT%.pem}.pub.pem"

openssl genpkey -algorithm ed25519 -out "$OUT"
openssl pkey -in "$OUT" -pubout -out "$PUB"

echo "Private key (GitHub secret ED25519_SIGNING_KEY): $OUT"
echo "Public key (paste into include/core/ReleaseSigning.h): $PUB"
echo ""
cat "$PUB"
