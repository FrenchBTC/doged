#!/usr/bin/env bash
# Start doged on DOGE testnet with stratum + merged mining against LTC testnet.
#
# Usage:
#   ./testnet/start-doged.sh [LTC_RPC_HOST]
#
# The LTC_RPC_HOST argument defaults to 127.0.0.1 (local docker or same box).
# Change it to your VPS IP if litecoind runs remotely.

set -euo pipefail
cd "$(dirname "$0")/.."

DOGED="$(pwd)/build/src/doged"
DOGECLI="$(pwd)/build/src/doge-cli"
DATADIR="$HOME/dogetest"

LTC_HOST="${1:-127.0.0.1}"
LTC_PORT=19332
LTC_USER=ltctest
LTC_PASS=ltcpass
LTC_CHAINID=2
LTC_POLL_MS=5000

STRATUM_PORT=23333
STRATUM_BIND="0.0.0.0"

if [ ! -x "$DOGED" ]; then
    echo "ERROR: doged not found at $DOGED — run 'ninja doged' in build/ first"
    exit 1
fi

mkdir -p "$DATADIR"

# Generate a testnet address on first run
if [ ! -f "$DATADIR/coinbase_address.txt" ]; then
    echo "First run — starting doged briefly to generate a coinbase address..."
    "$DOGED" -testnet -datadir="$DATADIR" -daemon
    sleep 3
    ADDR=$("$DOGECLI" -testnet -datadir="$DATADIR" getnewaddress "stratum-coinbase" 2>/dev/null || echo "")
    if [ -z "$ADDR" ]; then
        ADDR=$("$DOGECLI" -testnet -datadir="$DATADIR" -rpcwait getnewaddress "stratum-coinbase" 2>/dev/null || echo "")
    fi
    "$DOGECLI" -testnet -datadir="$DATADIR" stop 2>/dev/null || true
    sleep 2
    if [ -z "$ADDR" ]; then
        echo "WARNING: Could not auto-generate address. Using OP_TRUE coinbase."
        ADDR=""
    else
        echo "$ADDR" > "$DATADIR/coinbase_address.txt"
        echo "Coinbase address: $ADDR"
    fi
fi

COINBASE_ADDR=""
if [ -f "$DATADIR/coinbase_address.txt" ]; then
    COINBASE_ADDR=$(cat "$DATADIR/coinbase_address.txt")
fi

COINBASE_ARG=""
if [ -n "$COINBASE_ADDR" ]; then
    COINBASE_ARG="-stratumcoinbase=$COINBASE_ADDR"
fi

MERGEMINE_ARG="-mergemine=LTC:${LTC_HOST}:${LTC_PORT}:${LTC_USER}:${LTC_PASS}:${LTC_CHAINID}:${LTC_POLL_MS}"

echo "============================================================"
echo "  DOGED TESTNET — Stratum + Merged Mining"
echo "============================================================"
echo "  Datadir:      $DATADIR"
echo "  Stratum:      ${STRATUM_BIND}:${STRATUM_PORT}"
echo "  Coinbase:     ${COINBASE_ADDR:-OP_TRUE}"
echo "  LTC merge:    ${LTC_HOST}:${LTC_PORT} (chainid=${LTC_CHAINID})"
echo "============================================================"
echo ""

exec "$DOGED" \
    -testnet \
    -datadir="$DATADIR" \
    -stratum \
    -stratumport="$STRATUM_PORT" \
    -stratumbind="$STRATUM_BIND" \
    $COINBASE_ARG \
    "$MERGEMINE_ARG" \
    -coinbasetag="/doged-test/" \
    -server=1 \
    -printtoconsole=1 \
    -debug=stratum \
    -debug=mergemine \
    -debug=rpc \
    -txindex=1
