// Copyright (c) 2023-2026 The Dogecoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_API_MINING_HANDLER_H
#define BITCOIN_API_MINING_HANDLER_H

#include <api/api_util.h>
#include <httpserver.h>
#include <node/context.h>

#include <string>
#include <vector>

namespace api {

bool HandleGetMiningInfo(node::NodeContext &node, HTTPRequest *req,
                         const std::vector<std::string> &parts,
                         const QueryParams &qp);

} // namespace api

#endif // BITCOIN_API_MINING_HANDLER_H
