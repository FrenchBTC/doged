#!/usr/bin/env bash
# Validate that doged testnet + LTC testnet + stratum are wired correctly.
# Run this AFTER both nodes have synced and doged is serving stratum.
#
# Usage:
#   ./testnet/validate-testnet.sh [LTC_RPC_HOST]

set -euo pipefail
cd "$(dirname "$0")/.."

DOGECLI="$(pwd)/build/src/doge-cli"
DATADIR="$HOME/dogetest"

LTC_HOST="${1:-127.0.0.1}"
LTC_PORT=19332
LTC_USER=ltctest
LTC_PASS=ltcpass

PASS=0
FAIL=0
WARN=0

ok()   { echo "  ✓ $1"; PASS=$((PASS+1)); }
fail() { echo "  ✗ $1"; FAIL=$((FAIL+1)); }
warn() { echo "  ⚠ $1"; WARN=$((WARN+1)); }
header() { echo ""; echo "── $1 ──"; }

# -------------------------------------------------------------------
header "1. DOGE testnet node"
# -------------------------------------------------------------------
DOGE_INFO=$("$DOGECLI" -testnet -datadir="$DATADIR" getblockchaininfo 2>&1) || {
    fail "doged RPC unreachable — is doged running with -testnet?"
    echo "$DOGE_INFO"
    exit 1
}

DOGE_CHAIN=$(echo "$DOGE_INFO" | python3 -c "import sys,json; print(json.load(sys.stdin)['chain'])")
DOGE_HEIGHT=$(echo "$DOGE_INFO" | python3 -c "import sys,json; print(json.load(sys.stdin)['blocks'])")
DOGE_PROGRESS=$(echo "$DOGE_INFO" | python3 -c "import sys,json; print(json.load(sys.stdin)['verificationprogress'])")

[ "$DOGE_CHAIN" = "test" ] && ok "Chain: $DOGE_CHAIN" || fail "Expected chain=test, got $DOGE_CHAIN"
echo "  Height: $DOGE_HEIGHT  Progress: $DOGE_PROGRESS"

if python3 -c "exit(0 if $DOGE_PROGRESS > 0.99 else 1)" 2>/dev/null; then
    ok "IBD complete (progress > 0.99)"
else
    warn "Still syncing (progress=$DOGE_PROGRESS) — RPCs may return stale data"
fi

# -------------------------------------------------------------------
header "2. LTC testnet node"
# -------------------------------------------------------------------
LTC_INFO=$(curl -sf --user "$LTC_USER:$LTC_PASS" \
    --data-binary '{"method":"getblockchaininfo","params":[]}' \
    "http://$LTC_HOST:$LTC_PORT" 2>&1) || {
    fail "litecoind RPC unreachable at $LTC_HOST:$LTC_PORT"
    echo "  Hint: is docker-compose up? Is the port open?"
    LTC_INFO=""
}

if [ -n "$LTC_INFO" ]; then
    LTC_CHAIN=$(echo "$LTC_INFO" | python3 -c "import sys,json; print(json.load(sys.stdin)['result']['chain'])")
    LTC_HEIGHT=$(echo "$LTC_INFO" | python3 -c "import sys,json; print(json.load(sys.stdin)['result']['blocks'])")
    LTC_PROGRESS=$(echo "$LTC_INFO" | python3 -c "import sys,json; print(json.load(sys.stdin)['result']['verificationprogress'])")

    [ "$LTC_CHAIN" = "test" ] && ok "LTC chain: $LTC_CHAIN" || fail "Expected LTC chain=test, got $LTC_CHAIN"
    echo "  Height: $LTC_HEIGHT  Progress: $LTC_PROGRESS"

    if python3 -c "exit(0 if $LTC_PROGRESS > 0.99 else 1)" 2>/dev/null; then
        ok "LTC IBD complete"
    else
        warn "LTC still syncing (progress=$LTC_PROGRESS)"
    fi
fi

# -------------------------------------------------------------------
header "3. Merged mining RPCs"
# -------------------------------------------------------------------
MMINFO=$("$DOGECLI" -testnet -datadir="$DATADIR" getmergemineinfo 2>&1) || {
    fail "getmergemineinfo failed"
    echo "$MMINFO"
    MMINFO=""
}

if [ -n "$MMINFO" ]; then
    MM_ENABLED=$(echo "$MMINFO" | python3 -c "import sys,json; print(json.load(sys.stdin)['enabled'])")
    MM_CHAINS=$(echo "$MMINFO" | python3 -c "import sys,json; print(json.load(sys.stdin)['chains'])")

    [ "$MM_ENABLED" = "True" ] && ok "Merge mining enabled" || fail "Merge mining not enabled"
    [ "$MM_CHAINS" -ge 1 ] 2>/dev/null && ok "External chains: $MM_CHAINS" || warn "No external chains registered yet"

    MM_WORK=$(echo "$MMINFO" | python3 -c "
import sys, json
d = json.load(sys.stdin)
for w in d.get('work', []):
    print(f\"  {w['chain']}: height={w['height']} auxhash={w['auxhash'][:24]}...\")
" 2>/dev/null || true)
    [ -n "$MM_WORK" ] && echo "$MM_WORK"
fi

# -------------------------------------------------------------------
header "4. createauxblock"
# -------------------------------------------------------------------
COINBASE_ADDR=""
if [ -f "$DATADIR/coinbase_address.txt" ]; then
    COINBASE_ADDR=$(cat "$DATADIR/coinbase_address.txt")
fi

if [ -n "$COINBASE_ADDR" ]; then
    AUXBLOCK=$("$DOGECLI" -testnet -datadir="$DATADIR" createauxblock "$COINBASE_ADDR" 2>&1) || {
        fail "createauxblock failed"
        echo "$AUXBLOCK"
        AUXBLOCK=""
    }

    if [ -n "$AUXBLOCK" ]; then
        CHAIN_ID=$(echo "$AUXBLOCK" | python3 -c "import sys,json; print(json.load(sys.stdin)['chainid'])")
        AUX_HEIGHT=$(echo "$AUXBLOCK" | python3 -c "import sys,json; print(json.load(sys.stdin)['height'])")
        AUX_HASH=$(echo "$AUXBLOCK" | python3 -c "import sys,json; print(json.load(sys.stdin)['hash'][:24])")

        [ "$CHAIN_ID" = "98" ] && ok "Chain ID = 0x62 (98 decimal) ✓" || fail "Unexpected chainid: $CHAIN_ID"
        ok "AuxBlock: height=$AUX_HEIGHT hash=$AUX_HASH..."
    fi
else
    warn "No coinbase address — skipping createauxblock test"
fi

# -------------------------------------------------------------------
header "5. Stratum port check"
# -------------------------------------------------------------------
if timeout 2 bash -c "echo | nc -w1 127.0.0.1 23333" 2>/dev/null; then
    ok "Stratum port 23333 accepting connections"
else
    fail "Stratum port 23333 not responding"
fi

# -------------------------------------------------------------------
header "6. Stratum subscribe smoke test"
# -------------------------------------------------------------------
SUBSCRIBE_RESP=$(echo '{"id":1,"method":"mining.subscribe","params":["doged-test/0.1"]}' | \
    timeout 3 nc -w2 127.0.0.1 23333 2>/dev/null | head -1) || true

if [ -n "$SUBSCRIBE_RESP" ]; then
    ok "Stratum subscribe response received"
    echo "  ${SUBSCRIBE_RESP:0:120}..."
else
    warn "No stratum subscribe response (might need a brief wait after startup)"
fi

# -------------------------------------------------------------------
echo ""
echo "════════════════════════════════════════════"
echo "  Results: $PASS passed, $FAIL failed, $WARN warnings"
echo "════════════════════════════════════════════"
[ "$FAIL" -eq 0 ] && echo "  🟢 All critical checks passed!" || echo "  🔴 $FAIL check(s) failed"
echo ""
exit "$FAIL"
