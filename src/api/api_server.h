// Copyright (c) 2023-2026 The Dogecoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_API_SERVER_H
#define BITCOIN_API_SERVER_H

namespace node {
struct NodeContext;
}

void StartAPI(node::NodeContext &node);
void InterruptAPI();
void StopAPI();

#endif // BITCOIN_API_SERVER_H
