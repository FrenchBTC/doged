#!/usr/bin/env bash
# Compare best block hash, height, and chain work between doged-a and
# doged-b on the two-node convergence harness. Both nodes should agree
# once they have peered + synced.
#
# Usage:
#   ./testnet/validate-fork.sh                 # localhost (docker-compose)
#   HOST=1.2.3.4 ./testnet/validate-fork.sh    # remote VPS
#
# Exits 0 if the two nodes agree on tip, non-zero otherwise.

set -euo pipefail

HOST="${HOST:-127.0.0.1}"
RPC_USER="${RPC_USER:-dogetest}"
RPC_PASS="${RPC_PASS:-dogepass}"
PORT_A="${PORT_A:-44555}"
PORT_B="${PORT_B:-44557}"

call() {
    local port="$1" method="$2"
    curl -sf --user "${RPC_USER}:${RPC_PASS}" --data-binary \
        "{\"jsonrpc\":\"1.0\",\"id\":\"validate-fork\",\"method\":\"${method}\",\"params\":[]}" \
        -H 'content-type: text/plain;' "http://${HOST}:${port}/" \
        | python3 -c 'import sys,json; r=json.load(sys.stdin); print(json.dumps(r["result"]))'
}

extract() { python3 -c "import sys,json; d=json.loads(sys.stdin.read()); print(d['$1'])"; }

INFO_A="$(call "$PORT_A" getblockchaininfo 2>/dev/null || echo '{}')"
INFO_B="$(call "$PORT_B" getblockchaininfo 2>/dev/null || echo '{}')"

if [ "$INFO_A" = '{}' ]; then echo "ERROR: doged-a unreachable on ${HOST}:${PORT_A}"; exit 2; fi
if [ "$INFO_B" = '{}' ]; then echo "ERROR: doged-b unreachable on ${HOST}:${PORT_B}"; exit 2; fi

A_HEIGHT="$(echo "$INFO_A" | extract blocks)"
B_HEIGHT="$(echo "$INFO_B" | extract blocks)"
A_HASH="$(echo "$INFO_A"   | extract bestblockhash)"
B_HASH="$(echo "$INFO_B"   | extract bestblockhash)"
A_WORK="$(echo "$INFO_A"   | extract chainwork)"
B_WORK="$(echo "$INFO_B"   | extract chainwork)"
A_PEER="$(call "$PORT_A" getconnectioncount)"
B_PEER="$(call "$PORT_B" getconnectioncount)"

printf "%-10s %-10s %-66s %-66s %-8s\n" node height bestblockhash chainwork peers
printf "%-10s %-10s %-66s %-66s %-8s\n" doged-a "$A_HEIGHT" "$A_HASH" "$A_WORK" "$A_PEER"
printf "%-10s %-10s %-66s %-66s %-8s\n" doged-b "$B_HEIGHT" "$B_HASH" "$B_WORK" "$B_PEER"

if [ "$A_HASH" = "$B_HASH" ] && [ "$A_HEIGHT" = "$B_HEIGHT" ]; then
    echo "OK  — both nodes agree on tip ${A_HEIGHT} ${A_HASH}"
    exit 0
fi

echo "DIVERGED — nodes do not agree on the tip"
exit 1
