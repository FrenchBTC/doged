// Copyright (c) 2023-2026 The Dogecoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <api/mining_handler.h>

#include <chain.h>
#include <chainparams.h>
#include <node/context.h>
#include <rpc/blockchain.h>
#include <rpc/protocol.h>
#include <sync.h>
#include <txmempool.h>
#include <validation.h>

namespace api {

bool HandleGetMiningInfo(node::NodeContext &node, HTTPRequest *req,
                         const std::vector<std::string> &,
                         const QueryParams &) {
    UniValue result(UniValue::VOBJ);
    {
        LOCK(cs_main);
        const CBlockIndex *tip = node.chainman->ActiveChain().Tip();
        if (!tip) {
            WriteError(req, HTTP_SERVICE_UNAVAILABLE, "not_ready",
                       "Chain not loaded");
            return true;
        }

        result.pushKV("height", tip->nHeight);
        result.pushKV("difficulty", GetDifficulty(tip));
        result.pushKV("bits", strprintf("%08x", tip->nBits));
        result.pushKV("chain_work", tip->nChainWork.GetHex());
    }

    if (node.mempool) {
        LOCK(node.mempool->cs);
        result.pushKV("mempool_size", int64_t(node.mempool->size()));
        result.pushKV("mempool_bytes",
                       int64_t(node.mempool->GetTotalTxSize()));
    }

    result.pushKV("chain", Params().GetChainTypeString());

    WriteSuccess(req, result);
    return true;
}

} // namespace api
