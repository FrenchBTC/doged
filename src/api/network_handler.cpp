// Copyright (c) 2023-2026 The Dogecoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <api/network_handler.h>

#include <common/args.h>
#include <config.h>
#include <net.h>
#include <net_processing.h>
#include <node/context.h>
#include <rpc/protocol.h>
#include <validation.h>
#include <version.h>
#include <util/time.h>

namespace api {

bool HandleGetNetworkInfo(node::NodeContext &node, HTTPRequest *req,
                          const std::vector<std::string> &,
                          const QueryParams &) {
    UniValue result(UniValue::VOBJ);
    result.pushKV("protocol_version", PROTOCOL_VERSION);
    result.pushKV("subversion", userAgent(GetConfig()));

    if (node.connman) {
        result.pushKV("connections",
                       int(node.connman->GetNodeCount(
                           ConnectionDirection::Both)));
        result.pushKV("connections_in",
                       int(node.connman->GetNodeCount(
                           ConnectionDirection::In)));
        result.pushKV("connections_out",
                       int(node.connman->GetNodeCount(
                           ConnectionDirection::Out)));
    }

    result.pushKV("network_active",
                   node.connman ? node.connman->GetNetworkActive() : false);

    WriteSuccess(req, result);
    return true;
}

bool HandleGetPeers(node::NodeContext &node, HTTPRequest *req,
                    const std::vector<std::string> &,
                    const QueryParams &qp) {
    if (!node.connman) {
        WriteError(req, HTTP_SERVICE_UNAVAILABLE, "no_network",
                   "Network not available");
        return true;
    }

    std::vector<CNodeStats> vstats;
    node.connman->GetNodeStats(vstats);

    UniValue peers(UniValue::VARR);
    for (const auto &stats : vstats) {
        UniValue peer(UniValue::VOBJ);
        peer.pushKV("id", stats.nodeid);
        peer.pushKV("addr", stats.m_addr_name);
        peer.pushKV("subver", stats.cleanSubVer);
        peer.pushKV("inbound", stats.fInbound);
        peer.pushKV("startingheight", stats.m_starting_height);
        peer.pushKV("ping_ms",
                     stats.m_last_ping_time.count() > 0
                         ? double(stats.m_last_ping_time.count()) / 1000.0
                         : -1.0);
        peer.pushKV("bytes_sent", int64_t(stats.nSendBytes));
        peer.pushKV("bytes_recv", int64_t(stats.nRecvBytes));
        peers.push_back(peer);
    }

    WriteSuccess(req, peers);
    return true;
}

bool HandleGetNodeInfo(node::NodeContext &node, HTTPRequest *req,
                       const std::vector<std::string> &,
                       const QueryParams &) {
    UniValue result(UniValue::VOBJ);
    result.pushKV("version", CLIENT_VERSION);
    result.pushKV("subversion", userAgent(GetConfig()));
    result.pushKV("protocol_version", PROTOCOL_VERSION);
    result.pushKV("uptime", GetTime() - GetStartupTime());

    {
        LOCK(cs_main);
        ChainstateManager &chainman = *node.chainman;
        result.pushKV("initial_block_download",
                       chainman.IsInitialBlockDownload());

        const CBlockIndex *tip = chainman.ActiveChain().Tip();
        if (tip) {
            result.pushKV("chain_height", tip->nHeight);
            result.pushKV("best_block_hash", tip->GetBlockHash().GetHex());
        }
    }

    result.pushKV("datadir", fs::PathToString(gArgs.GetDataDirNet()));

    WriteSuccess(req, result);
    return true;
}

} // namespace api
