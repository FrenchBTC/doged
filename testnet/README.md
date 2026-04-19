# Live Testnet: DOGE + LTC Merged Mining

Mine real DOGE testnet blocks with your laptop GPU, merged with LTC testnet.
Both nodes run on a VPS. The miner runs on your laptop.

## Architecture

```
    ┌─── VPS (any host, ARM64 or x86_64) ────────────────────────┐
    │                                                             │
    │  ┌──────────────┐    docker net    ┌──────────────────────┐ │
    │  │  litecoind   │◄───────────────│  doged               │ │
    │  │  LTC testnet │  RPC :19332    │  DOGE testnet        │ │
    │  │  (container) │                │  stratum :23333  ◄───┼─┼── laptop miner
    │  └──────────────┘                │  RPC :44555          │ │
    │                                  └──────────────────────┘ │
    └─────────────────────────────────────────────────────────────┘

    ┌─── Laptop ─────────────────┐
    │  doged-miner               │
    │  GPU scrypt                │
    │  → stratum+tcp://VPS:23333 │
    └────────────────────────────┘
```

## Quick Start

### 1. Deploy to VPS

```bash
# Required: VPS_HOST. Optional: VPS_USER, VPS_DIR, VPS_PASS (uses ssh keys
# by default; set VPS_PASS only if your VPS uses password auth + sshpass).
VPS_HOST=1.2.3.4 ./testnet/deploy-vps.sh
```

This rsyncs the source, builds doged inside Docker, and starts both containers.
First build takes ~10-20 min. Subsequent runs use the Docker layer cache.

### 2. Monitor IBD progress

```bash
export VPS_HOST=1.2.3.4

# LTC testnet (small chain, ~30 min)
curl -sf --user ltctest:ltcpass --data-binary \
  '{"method":"getblockchaininfo","params":[]}' \
  http://${VPS_HOST}:19332 | python3 -m json.tool | grep -E 'blocks|progress'

# DOGE testnet (larger chain, ~2-4 hours)
curl -sf --user dogetest:dogepass --data-binary \
  '{"method":"getblockchaininfo","params":[]}' \
  http://${VPS_HOST}:44555 | python3 -m json.tool | grep -E 'blocks|progress'
```

### 3. Validate setup

```bash
VPS_HOST=1.2.3.4 ./testnet/validate-testnet.sh
```

### 4. Connect your GPU miner (from laptop)

```bash
./testnet/start-miner.sh --gpu 0 1.2.3.4
```

## What to expect

| Event | Where |
|-------|-------|
| Stratum subscribe + authorize | miner stdout |
| mining.notify jobs flowing | miner stdout (after both chains finish IBD) |
| Share accepted | miner stdout |
| DOGE block found | `docker logs doge-testnet`: `BLOCK FOUND` |
| LTC AuxPoW accepted | `docker logs doge-testnet`: `LTC block accepted!` |

DOGE testnet difficulty is ~0.001 — an RTX 4080 at ~1.5 MH/s should find
a block every few minutes. LTC testnet is also low difficulty, so merge-mined
LTC blocks should appear too.

## VPS management

```bash
# SSH into VPS (use your normal ssh / key-based auth)
ssh root@${VPS_HOST}

# View logs
docker logs -f doge-testnet --tail 100
docker logs -f ltc-testnet --tail 100

# Filter for mining events
docker logs doge-testnet 2>&1 | grep -E 'Stratum|MergeMine|BLOCK'

# Restart
cd /opt/doged/testnet
docker compose -f docker-compose.testnet.yml restart

# Stop
docker compose -f docker-compose.testnet.yml down

# Rebuild (after code changes)
docker compose -f docker-compose.testnet.yml build doged
docker compose -f docker-compose.testnet.yml up -d
```

## Credentials

The `dogetest:dogepass` and `ltctest:ltcpass` RPC credentials in
`docker-compose.testnet.yml` are intended for the isolated Docker network
only and are not exposed publicly (RPC ports are only bound for local
debug). If you expose these RPC ports beyond the VPS, change them.

## Ports

| Port | Service | Access |
|------|---------|--------|
| 23333 | Stratum v1 | Laptop miner → VPS |
| 44555 | DOGE RPC | Debug (curl from laptop) |
| 44556 | DOGE P2P | Testnet peers |
| 19332 | LTC RPC | Internal (doged → litecoind) + debug |
| 19335 | LTC P2P | Testnet peers |
