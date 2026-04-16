// Copyright (c) 2023-2026 The Dogecoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <api/chain_handler.h>

#include <blockindex.h>
#include <chain.h>
#include <chainparams.h>
#include <node/context.h>
#include <rpc/blockchain.h>
#include <rpc/protocol.h>
#include <sync.h>
#include <validation.h>

namespace api {

bool HandleGetChainInfo(node::NodeContext &node, HTTPRequest *req,
                        const std::vector<std::string> &,
                        const QueryParams &) {
    UniValue result(UniValue::VOBJ);
    {
        LOCK(cs_main);
        ChainstateManager &chainman = *node.chainman;
        const CBlockIndex *tip = chainman.ActiveChain().Tip();
        if (!tip) {
            WriteError(req, HTTP_SERVICE_UNAVAILABLE, "not_ready",
                       "Chain not yet loaded");
            return true;
        }

        result.pushKV("chain", Params().GetChainTypeString());
        result.pushKV("height", tip->nHeight);
        result.pushKV("best_block_hash", tip->GetBlockHash().GetHex());
        result.pushKV("difficulty", GetDifficulty(tip));
        result.pushKV("median_time", int64_t(tip->GetMedianTimePast()));
        result.pushKV("chain_work", tip->nChainWork.GetHex());
        result.pushKV("initial_block_download",
                       chainman.IsInitialBlockDownload());
    }

    WriteSuccess(req, result);
    return true;
}

bool HandleGetChainTip(node::NodeContext &node, HTTPRequest *req,
                       const std::vector<std::string> &,
                       const QueryParams &) {
    UniValue result(UniValue::VOBJ);
    {
        LOCK(cs_main);
        const CBlockIndex *tip = node.chainman->ActiveChain().Tip();
        if (!tip) {
            WriteError(req, HTTP_SERVICE_UNAVAILABLE, "not_ready",
                       "Chain not yet loaded");
            return true;
        }

        result.pushKV("hash", tip->GetBlockHash().GetHex());
        result.pushKV("height", tip->nHeight);
        result.pushKV("time", int64_t(tip->GetBlockTime()));
        result.pushKV("n_tx", int64_t(tip->nTx));
        result.pushKV("size", int64_t(tip->nSize));

        if (tip->pprev) {
            result.pushKV("previous_hash",
                           tip->pprev->GetBlockHash().GetHex());
        }
    }

    WriteSuccess(req, result);
    return true;
}

static UniValue BlockIndexToJSON(const CBlockIndex *pindex,
                                 const CBlockIndex *tip) {
    UniValue obj(UniValue::VOBJ);
    obj.pushKV("hash", pindex->GetBlockHash().GetHex());
    obj.pushKV("height", pindex->nHeight);
    obj.pushKV("time", int64_t(pindex->GetBlockTime()));
    obj.pushKV("n_tx", int64_t(pindex->nTx));
    obj.pushKV("size", int64_t(pindex->nSize));
    obj.pushKV("difficulty", GetDifficulty(pindex));
    obj.pushKV("confirmations",
               tip ? tip->nHeight - pindex->nHeight + 1 : 0);
    if (pindex->pprev) {
        obj.pushKV("previous_hash", pindex->pprev->GetBlockHash().GetHex());
    }
    return obj;
}

bool HandleGetBlocks(node::NodeContext &node, HTTPRequest *req,
                     const std::vector<std::string> &parts,
                     const QueryParams &qp) {
    if (parts.size() >= 2) {
        const std::string &id = parts[1];
        LOCK(cs_main);
        ChainstateManager &chainman = *node.chainman;
        const CBlockIndex *tip = chainman.ActiveChain().Tip();
        const CBlockIndex *pindex = nullptr;

        bool allDigits =
            !id.empty() && std::all_of(id.begin(), id.end(), ::isdigit);
        if (allDigits) {
            try {
                int height = std::stoi(id);
                if (height >= 0 &&
                    height <= chainman.ActiveChain().Height()) {
                    pindex = chainman.ActiveChain()[height];
                }
            } catch (...) {
            }
        } else {
            uint256 rawHash;
            if (ParseHashFromHex(id, rawHash)) {
                pindex = chainman.m_blockman.LookupBlockIndex(
                    BlockHash(rawHash));
            }
        }

        if (!pindex) {
            WriteError(req, HTTP_NOT_FOUND, "block_not_found",
                       "Block not found: " + id);
            return true;
        }

        UniValue block = BlockIndexToJSON(pindex, tip);
        WriteSuccess(req, block);
        return true;
    }

    int limit = qp.GetInt("limit", 10);
    int offset = qp.GetInt("offset", 0);
    limit = std::max(1, std::min(limit, 100));
    offset = std::max(0, offset);

    UniValue blocks(UniValue::VARR);
    int total;
    {
        LOCK(cs_main);
        const CBlockIndex *tip = node.chainman->ActiveChain().Tip();
        if (!tip) {
            WriteError(req, HTTP_SERVICE_UNAVAILABLE, "not_ready",
                       "Chain not yet loaded");
            return true;
        }
        total = tip->nHeight + 1;

        int startHeight = tip->nHeight - offset;
        for (int i = 0; i < limit && startHeight - i >= 0; i++) {
            const CBlockIndex *pindex =
                node.chainman->ActiveChain()[startHeight - i];
            if (pindex) {
                blocks.push_back(BlockIndexToJSON(pindex, tip));
            }
        }
    }

    WriteSuccess(req, PaginatedResponse(blocks, total, limit, offset));
    return true;
}

} // namespace api
