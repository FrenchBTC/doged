#!/usr/bin/env bash
# Connect doged-miner to the local doged stratum server.
#
# Usage:
#   ./testnet/start-miner.sh [--gpu N | --cpu N]
#
# Defaults to GPU 0. Pass --cpu 4 for CPU-only with 4 threads.

set -euo pipefail
cd "$(dirname "$0")/.."

MINER="$(pwd)/build/tools/miner/doged-miner"
STRATUM_URL="stratum+tcp://127.0.0.1:23333"
WORKER="test.worker"
PASSWORD="x"

if [ ! -x "$MINER" ]; then
    echo "ERROR: doged-miner not found — run 'ninja doged-miner' with -DBUILD_MINER=ON"
    exit 1
fi

MODE_ARGS="--gpu 0"
for arg in "$@"; do
    case "$arg" in
        --gpu|--cpu) MODE_ARGS="$arg" ;;
        [0-9]*) MODE_ARGS="$MODE_ARGS $arg" ;;
    esac
done

echo "============================================================"
echo "  DOGED-MINER — Scrypt GPU/CPU Stratum Miner"
echo "============================================================"
echo "  Server:   $STRATUM_URL"
echo "  Worker:   $WORKER"
echo "  Mode:     $MODE_ARGS"
echo "============================================================"
echo ""

exec "$MINER" \
    -o "$STRATUM_URL" \
    -u "$WORKER" \
    -p "$PASSWORD" \
    $MODE_ARGS
