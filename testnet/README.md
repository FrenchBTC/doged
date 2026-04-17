# Live Testnet: DOGE + LTC Merged Mining

Mine real DOGE testnet blocks with GPU Scrypt, merge-mined with LTC testnet.

## Architecture

```
┌──────────────────┐    RPC (19332)    ┌──────────────────┐
│  litecoind       │◄─────────────────│  doged           │
│  (LTC testnet)   │   poll every 5s  │  (DOGE testnet)  │
│  docker-compose  │                  │  + stratum:23333 │
└──────────────────┘                  └────────┬─────────┘
                                               │ stratum v1
                                      ┌────────▼─────────┐
                                      │  doged-miner     │
                                      │  GPU scrypt      │
                                      │  RTX 4080        │
                                      └──────────────────┘
```

## Step 1 — LTC testnet node (on VPS or local)

```bash
cd testnet/
docker compose -f docker-compose.ltc.yml up -d
docker logs -f ltc-testnet   # watch IBD progress
```

Check sync progress:

```bash
curl -sf --user ltctest:ltcpass --data-binary \
  '{"method":"getblockchaininfo","params":[]}' \
  http://127.0.0.1:19332 | python3 -m json.tool | grep -E 'blocks|progress'
```

LTC testnet IBD takes ~30-60 minutes (chain is small).

If running on a **remote VPS**, open port 19332 and use that IP in step 2.

## Step 2 — DOGE testnet node

```bash
# From the doged repo root:
./testnet/start-doged.sh [LTC_RPC_HOST]

# Examples:
./testnet/start-doged.sh                # LTC on localhost
./testnet/start-doged.sh 10.0.0.5       # LTC on VPS at 10.0.0.5
```

On first run it auto-generates a coinbase address (saved to `~/dogetest/coinbase_address.txt`).

DOGE testnet IBD takes a few hours. Watch for:
```
MergeMine: 1 chain(s) — LTC
Stratum: server started on 0.0.0.0:23333
```

## Step 3 — Validate the setup

After both nodes are synced, run:

```bash
./testnet/validate-testnet.sh [LTC_RPC_HOST]
```

This checks:
- Both chains are on `test` network
- IBD progress > 99%
- `getmergemineinfo` shows LTC registered
- `createauxblock` returns chainid=0x62
- Stratum port accepts connections + responds to subscribe

## Step 4 — Connect the GPU miner

```bash
./testnet/start-miner.sh --gpu 0      # GPU mining (RTX 4080)
./testnet/start-miner.sh --cpu 4       # CPU only, 4 threads
```

Watch doged's console for:
```
Stratum: new connection #1 from 127.0.0.1
Stratum: worker test.worker authorized
Stratum: share accepted (diff=0.50)
```

## What to expect

| Event | Where to see it |
|-------|----------------|
| Stratum subscribe | doged console: `BCLog::STRATUM` |
| mining.notify jobs | miner stdout |
| Share accepted | doged console + miner |
| DOGE block found | doged: `BLOCK FOUND by worker test.worker at height N` |
| LTC AuxPoW submit | doged: `LTC block accepted!` |

**Difficulty reality check**: DOGE testnet difficulty is ~0.001 — a single RTX 4080
doing ~1.5 MH/s Scrypt should find a block every few minutes. LTC testnet difficulty
is also very low, so merge-mined LTC blocks are likely too.

## Monitoring

Tail the doged debug log for all stratum/mergemine activity:

```bash
tail -f ~/dogetest/testnet3/debug.log | grep -E 'Stratum|MergeMine|BLOCK'
```

## Cleanup

```bash
# Stop miner: Ctrl+C
# Stop doged:
build/src/doge-cli -testnet -datadir=$HOME/dogetest stop
# Stop litecoind:
cd testnet/ && docker compose -f docker-compose.ltc.yml down
# Remove data (if needed):
# rm -rf ~/dogetest ~/ltctest
# docker volume rm testnet_ltc-testnet-data
```
