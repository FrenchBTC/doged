#!/usr/bin/env bash
# Validate that doged + litecoind testnet are running correctly on the VPS.
#
# Usage:
#   ./testnet/validate-testnet.sh [VPS_HOST]

set -euo pipefail

VPS="${1:-${VPS_HOST:-127.0.0.1}}"

DOGE_RPC_PORT=44555
DOGE_RPC_USER=dogetest
DOGE_RPC_PASS=dogepass

LTC_RPC_PORT=19332
LTC_RPC_USER=ltctest
LTC_RPC_PASS=ltcpass

STRATUM_PORT=23333

PASS=0
FAIL=0
WARN=0

ok()   { echo "  ✓ $1"; PASS=$((PASS+1)); }
fail() { echo "  ✗ $1"; FAIL=$((FAIL+1)); }
warn() { echo "  ⚠ $1"; WARN=$((WARN+1)); }
header() { echo ""; echo "── $1 ──"; }

rpc_doge() {
    curl -sf --max-time 5 --user "$DOGE_RPC_USER:$DOGE_RPC_PASS" \
        --data-binary "{\"method\":\"$1\",\"params\":[$2]}" \
        "http://$VPS:$DOGE_RPC_PORT" 2>&1
}

rpc_ltc() {
    curl -sf --max-time 5 --user "$LTC_RPC_USER:$LTC_RPC_PASS" \
        --data-binary "{\"method\":\"$1\",\"params\":[$2]}" \
        "http://$VPS:$LTC_RPC_PORT" 2>&1
}

echo "╔══════════════════════════════════════════════════╗"
echo "║  TESTNET VALIDATION — $VPS             ║"
echo "╚══════════════════════════════════════════════════╝"

# -------------------------------------------------------------------
header "1. DOGE testnet node"
# -------------------------------------------------------------------
DOGE_INFO=$(rpc_doge "getblockchaininfo" "") || {
    fail "doged RPC unreachable at $VPS:$DOGE_RPC_PORT"
    DOGE_INFO=""
}

if [ -n "$DOGE_INFO" ]; then
    eval "$(echo "$DOGE_INFO" | python3 -c "
import sys, json
r = json.load(sys.stdin)['result']
print(f'DOGE_CHAIN={r[\"chain\"]}')
print(f'DOGE_BLOCKS={r[\"blocks\"]}')
print(f'DOGE_HEADERS={r.get(\"headers\", 0)}')
print(f'DOGE_PROGRESS={r[\"verificationprogress\"]:.6f}')
")"

    [ "$DOGE_CHAIN" = "test" ] && ok "DOGE chain: test" || fail "Expected chain=test, got $DOGE_CHAIN"
    echo "    Blocks: $DOGE_BLOCKS  Headers: $DOGE_HEADERS  Progress: $DOGE_PROGRESS"

    if python3 -c "exit(0 if $DOGE_PROGRESS > 0.99 else 1)" 2>/dev/null; then
        ok "DOGE IBD complete"
    else
        warn "DOGE still syncing ($DOGE_BLOCKS blocks, $DOGE_HEADERS headers)"
    fi
fi

# -------------------------------------------------------------------
header "2. LTC testnet node"
# -------------------------------------------------------------------
LTC_INFO=$(rpc_ltc "getblockchaininfo" "") || {
    fail "litecoind RPC unreachable at $VPS:$LTC_RPC_PORT"
    LTC_INFO=""
}

if [ -n "$LTC_INFO" ]; then
    eval "$(echo "$LTC_INFO" | python3 -c "
import sys, json
r = json.load(sys.stdin)['result']
print(f'LTC_CHAIN={r[\"chain\"]}')
print(f'LTC_BLOCKS={r[\"blocks\"]}')
print(f'LTC_HEADERS={r.get(\"headers\", 0)}')
print(f'LTC_PROGRESS={r[\"verificationprogress\"]:.6f}')
")"

    [ "$LTC_CHAIN" = "test" ] && ok "LTC chain: test" || fail "Expected LTC chain=test, got $LTC_CHAIN"
    echo "    Blocks: $LTC_BLOCKS  Headers: $LTC_HEADERS  Progress: $LTC_PROGRESS"

    if python3 -c "exit(0 if $LTC_PROGRESS > 0.99 else 1)" 2>/dev/null; then
        ok "LTC IBD complete"
    else
        warn "LTC still syncing ($LTC_BLOCKS blocks, $LTC_HEADERS headers)"
    fi
fi

# -------------------------------------------------------------------
header "3. Merged mining RPCs"
# -------------------------------------------------------------------
MMINFO=$(rpc_doge "getmergemineinfo" "") || {
    fail "getmergemineinfo failed (doged may still be in IBD)"
    MMINFO=""
}

if [ -n "$MMINFO" ]; then
    eval "$(echo "$MMINFO" | python3 -c "
import sys, json
r = json.load(sys.stdin)['result']
print(f'MM_ENABLED={\"yes\" if r[\"enabled\"] else \"no\"}')
print(f'MM_CHAINS={r[\"chains\"]}')
")"

    [ "$MM_ENABLED" = "yes" ] && ok "Merge mining enabled" || fail "Merge mining not enabled"
    [ "$MM_CHAINS" -ge 1 ] 2>/dev/null && ok "External chains: $MM_CHAINS" || warn "No external chains registered yet"

    echo "$MMINFO" | python3 -c "
import sys, json
d = json.load(sys.stdin)['result']
for w in d.get('work', []):
    print(f\"    {w['chain']}: height={w.get('height','?')} auxhash={w.get('auxhash','?')[:24]}...\")
" 2>/dev/null || true
fi

# -------------------------------------------------------------------
header "4. createauxblock"
# -------------------------------------------------------------------
# Get a fresh address from the wallet, or use a hardcoded testnet fallback
ADDR_RESP=$(rpc_doge "getnewaddress" '""') 2>/dev/null || true
COINBASE_ADDR=$(echo "$ADDR_RESP" | python3 -c "import sys,json; print(json.load(sys.stdin).get('result',''))" 2>/dev/null) || true
if [ -z "$COINBASE_ADDR" ]; then
    COINBASE_ADDR="noBLKmyGEr3YNXFQ1sTSiQJWMHp6R4pj4"   # known DOGE testnet burn addr
fi

AUXBLOCK=$(rpc_doge "createauxblock" "\"$COINBASE_ADDR\"") || {
    warn "createauxblock failed (node may still be in IBD)"
    AUXBLOCK=""
}

if [ -n "$AUXBLOCK" ]; then
    HAS_CHAIN_ID=$(echo "$AUXBLOCK" | python3 -c "
import sys,json
r = json.load(sys.stdin).get('result', {})
if r and 'chainid' in r:
    print(r['chainid'])
else:
    print('')
" 2>/dev/null)
    if [ "$HAS_CHAIN_ID" = "98" ]; then
        ok "createauxblock: chainid = 0x62 (98)"
    elif [ -n "$HAS_CHAIN_ID" ]; then
        fail "Unexpected chainid: $HAS_CHAIN_ID"
    else
        warn "createauxblock response didn't contain chainid"
    fi
fi

# -------------------------------------------------------------------
header "5. Stratum port"
# -------------------------------------------------------------------
if timeout 3 bash -c "echo | nc -w2 $VPS $STRATUM_PORT" 2>/dev/null; then
    ok "Stratum port $STRATUM_PORT accepting connections"
else
    fail "Stratum port $STRATUM_PORT not responding at $VPS"
fi

# -------------------------------------------------------------------
header "6. Stratum subscribe smoke test"
# -------------------------------------------------------------------
SUBSCRIBE_RESP=$(echo '{"id":1,"method":"mining.subscribe","params":["doged-test/0.1"]}' | \
    timeout 3 nc -w2 "$VPS" "$STRATUM_PORT" 2>/dev/null | head -1) || true

if [ -n "$SUBSCRIBE_RESP" ]; then
    ok "Stratum subscribe response received"
    echo "    ${SUBSCRIBE_RESP:0:100}..."
else
    warn "No stratum subscribe response (may need IBD to finish)"
fi

# -------------------------------------------------------------------
echo ""
echo "════════════════════════════════════════════"
echo "  Results: $PASS passed, $FAIL failed, $WARN warnings"
echo "════════════════════════════════════════════"
[ "$FAIL" -eq 0 ] && echo "  All critical checks passed" || echo "  $FAIL check(s) failed"
echo ""
exit "$FAIL"
