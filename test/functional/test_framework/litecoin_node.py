#!/usr/bin/env python3
# Copyright (c) 2026 The Dogecoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
Lightweight helper to manage a litecoind process in functional tests.

Separate from TestNode because the doged test framework hard-codes
dogecoin.conf paths and doged-specific RPC quirks.
"""

import http.client
import json
import logging
import os
import signal
import subprocess
import sys
import time
import base64


logger = logging.getLogger("LitecoinTestNode")

# A fixed output descriptor using a well-known public key (generator point).
# Used with generatetodescriptor so no wallet is needed.
_RAW_DESCRIPTOR = (
    "pkh(0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798)"
)


def _find_litecoind():
    """Locate the litecoind binary."""
    env = os.environ.get("LITECOIND")
    if env and os.path.isfile(env):
        return env

    # Check relative to the doged repo root
    here = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.abspath(os.path.join(here, "..", "..", ".."))
    candidate = os.path.join(repo_root, "contrib", "litecoin", "src", "litecoind")
    if os.path.isfile(candidate):
        return candidate

    raise FileNotFoundError(
        "litecoind not found. Either set LITECOIND env var or build it "
        "via: contrib/litecoin/build_litecoind.sh"
    )


class LitecoinTestNode:
    """Manages a single litecoind regtest instance."""

    def __init__(self, tmpdir, rpc_port, rpc_user="ltctest", rpc_password="ltcpass"):
        self.binary = _find_litecoind()
        self.rpc_port = rpc_port
        self.rpc_user = rpc_user
        self.rpc_password = rpc_password
        self.datadir = os.path.join(tmpdir, "litecoin_node")
        self.process = None
        self._rpc_id = 0

        os.makedirs(self.datadir, exist_ok=True)

        # Write config
        conf_path = os.path.join(self.datadir, "litecoin.conf")
        with open(conf_path, "w") as f:
            f.write("regtest=1\n")
            f.write("[regtest]\n")
            f.write(f"rpcport={self.rpc_port}\n")
            f.write(f"rpcuser={self.rpc_user}\n")
            f.write(f"rpcpassword={self.rpc_password}\n")
            f.write("server=1\n")
            f.write("listen=0\n")
            f.write("listenonion=0\n")
            f.write("dnsseed=0\n")
            f.write("printtoconsole=0\n")
            f.write("fallbackfee=0.0001\n")

    def start(self, timeout=30):
        """Start litecoind and wait for RPC to become available."""
        args = [
            self.binary,
            f"-datadir={self.datadir}",
            "-regtest",
            "-daemon=0",
        ]
        logger.info(f"Starting litecoind: {' '.join(args)}")

        self.process = subprocess.Popen(
            args,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            env={**os.environ, "LIBC_FATAL_STDERR_": "1"},
        )

        # Wait for RPC
        for i in range(timeout * 4):
            if self.process.poll() is not None:
                raise RuntimeError(
                    f"litecoind exited early with code {self.process.returncode}"
                )
            try:
                info = self.rpc("getblockchaininfo")
                if info:
                    logger.info(
                        f"litecoind ready: chain={info['chain']}, "
                        f"height={info['blocks']}"
                    )
                    return
            except Exception:
                pass
            time.sleep(0.25)

        raise RuntimeError(f"litecoind RPC not ready after {timeout}s")

    def stop(self, timeout=15):
        """Stop litecoind gracefully."""
        if self.process is None:
            return

        try:
            self.rpc("stop")
        except Exception:
            pass

        try:
            self.process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            logger.warning("litecoind did not stop gracefully, sending SIGTERM")
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                logger.warning("litecoind still alive, sending SIGKILL")
                self.process.kill()
                self.process.wait()

        self.process = None

    def rpc(self, method, params=None):
        """Make a JSON-RPC call to litecoind."""
        if params is None:
            params = []
        self._rpc_id += 1

        body = json.dumps({
            "jsonrpc": "1.0",
            "id": self._rpc_id,
            "method": method,
            "params": params,
        })

        auth = base64.b64encode(
            f"{self.rpc_user}:{self.rpc_password}".encode()
        ).decode()

        conn = http.client.HTTPConnection("127.0.0.1", self.rpc_port, timeout=30)
        try:
            conn.request(
                "POST", "/",
                body=body,
                headers={
                    "Content-Type": "application/json",
                    "Authorization": f"Basic {auth}",
                },
            )
            resp = conn.getresponse()
            data = json.loads(resp.read().decode())

            if data.get("error"):
                err = data["error"]
                raise RuntimeError(
                    f"LTC RPC error ({err.get('code', '?')}): "
                    f"{err.get('message', str(err))}"
                )

            return data.get("result")
        finally:
            conn.close()

    def generate(self, nblocks):
        """Generate blocks on litecoind regtest.

        Uses generatetodescriptor with a fixed public key descriptor
        (no wallet needed).
        """
        if not hasattr(self, "_descriptor"):
            info = self.rpc("getdescriptorinfo", [_RAW_DESCRIPTOR])
            self._descriptor = info["descriptor"]
        return self.rpc("generatetodescriptor", [nblocks, self._descriptor])

    def getblockchaininfo(self):
        return self.rpc("getblockchaininfo")

    def getbestblockhash(self):
        return self.rpc("getbestblockhash")

    def getblockcount(self):
        return self.rpc("getblockcount")
