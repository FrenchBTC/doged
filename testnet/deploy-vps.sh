#!/usr/bin/env bash
# Deploy doged + litecoind testnet to a VPS via docker-compose.
#
# Usage:
#   VPS_HOST=1.2.3.4 ./testnet/deploy-vps.sh
#
# Required environment variables:
#   VPS_HOST  - VPS IP / hostname (e.g. 1.2.3.4)
# Optional environment variables:
#   VPS_USER  - SSH user (default: root)
#   VPS_DIR   - Remote directory (default: /opt/doged)
#   VPS_PASS  - SSH password. If unset, key-based auth is used. If set,
#               sshpass must be installed locally.
#
# This script:
#   1. rsync's the source tree to the VPS (excluding build/, .git objects)
#   2. Runs docker compose build + up on the VPS
#
# Prerequisites: docker + docker-compose on VPS. sshpass locally only if
# VPS_PASS is set (otherwise plain ssh + your SSH key is used).

set -euo pipefail
cd "$(dirname "$0")/.."

: "${VPS_HOST:?VPS_HOST env var required (e.g. VPS_HOST=1.2.3.4 $0)}"
VPS_USER="${VPS_USER:-root}"
VPS_DIR="${VPS_DIR:-/opt/doged}"

if [ -n "${VPS_PASS:-}" ]; then
    if ! command -v sshpass >/dev/null 2>&1; then
        echo "ERROR: VPS_PASS is set but sshpass is not installed." >&2
        exit 1
    fi
    SSH="sshpass -e ssh -o StrictHostKeyChecking=no $VPS_USER@$VPS_HOST"
    RSYNC_RSH="sshpass -e ssh -o StrictHostKeyChecking=no"
    export SSHPASS="$VPS_PASS"
else
    SSH="ssh -o StrictHostKeyChecking=no $VPS_USER@$VPS_HOST"
    RSYNC_RSH="ssh -o StrictHostKeyChecking=no"
fi

RSYNC="rsync -az --delete \
  --exclude=build/ \
  --exclude=.git/ \
  --exclude='*.o' \
  --exclude='*.a' \
  --exclude=contrib/litecoin/src/litecoind \
  --exclude=contrib/litecoin/src/litecoin-cli \
  -e \"$RSYNC_RSH\""

echo "================================================================"
echo "  DEPLOYING TESTNET TO VPS: ${VPS_USER}@${VPS_HOST}:${VPS_DIR}"
echo "================================================================"
echo ""

# Step 1: sync source
echo "── Step 1: rsync source tree to $VPS_HOST:$VPS_DIR ──"
$SSH "mkdir -p $VPS_DIR"
eval $RSYNC ./ "$VPS_USER@$VPS_HOST:$VPS_DIR/"
echo "  Done."

# Step 2: build + start
echo ""
echo "── Step 2: docker compose build + up ──"
echo "  This builds doged from source on the VPS (ARM64)."
echo "  First build takes ~10-20 min. Subsequent builds use cache."
echo ""

$SSH "cd $VPS_DIR/testnet && docker compose -f docker-compose.testnet.yml build --progress=plain doged 2>&1" | tail -30

echo ""
echo "── Step 3: starting services ──"
$SSH "cd $VPS_DIR/testnet && docker compose -f docker-compose.testnet.yml up -d"

echo ""
echo "── Step 4: checking status ──"
sleep 5
$SSH "docker ps --format 'table {{.Names}}\t{{.Status}}\t{{.Ports}}'"

echo ""
echo "================================================================"
echo "  DEPLOYMENT COMPLETE"
echo "  Stratum:  ${VPS_HOST}:23333"
echo "  DOGE RPC: ${VPS_HOST}:44555"
echo "  LTC  RPC: ${VPS_HOST}:19332"
echo ""
echo "  Connect miner from laptop:"
echo "    doged-miner -o stratum+tcp://${VPS_HOST}:23333 \\"
echo "      -u test.worker -p x --gpu 0"
echo "================================================================"
