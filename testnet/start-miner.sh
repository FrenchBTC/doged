#!/usr/bin/env bash
# Connect doged-miner (on laptop) to the VPS stratum server.
#
# Usage:
#   ./testnet/start-miner.sh [--gpu N | --cpu N] [VPS_HOST]
#
# Defaults to GPU 0. VPS_HOST defaults to 127.0.0.1 — override via positional
# arg or the VPS_HOST env var.

set -euo pipefail
cd "$(dirname "$0")/.."

MINER="$(pwd)/build/tools/miner/doged-miner"
WORKER="test.worker"
PASSWORD="x"

if [ ! -x "$MINER" ]; then
    echo "ERROR: doged-miner not found — run 'ninja doged-miner' with -DBUILD_MINER=ON"
    exit 1
fi

# Parse args
VPS_HOST="${VPS_HOST:-127.0.0.1}"
MODE_ARGS="--gpu 0"
while [ $# -gt 0 ]; do
    case "$1" in
        --gpu) MODE_ARGS="--gpu ${2:-0}"; shift 2 || shift ;;
        --cpu) MODE_ARGS="--cpu ${2:-4}"; shift 2 || shift ;;
        *)     VPS_HOST="$1"; shift ;;
    esac
done

STRATUM_URL="stratum+tcp://${VPS_HOST}:23333"

echo "╔══════════════════════════════════════════════════╗"
echo "║  DOGED-MINER — Laptop → VPS Stratum             ║"
echo "╠══════════════════════════════════════════════════╣"
echo "║  Server:   $STRATUM_URL"
echo "║  Worker:   $WORKER"
echo "║  Mode:     $MODE_ARGS"
echo "╚══════════════════════════════════════════════════╝"
echo ""

exec "$MINER" \
    -o "$STRATUM_URL" \
    -u "$WORKER" \
    -p "$PASSWORD" \
    $MODE_ARGS
