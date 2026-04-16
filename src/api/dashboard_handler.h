// Copyright (c) 2023-2026 The Dogecoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_API_DASHBOARD_HANDLER_H
#define BITCOIN_API_DASHBOARD_HANDLER_H

#include <httpserver.h>

namespace api {

bool HandleGetDashboard(HTTPRequest *req);

} // namespace api

#endif // BITCOIN_API_DASHBOARD_HANDLER_H
