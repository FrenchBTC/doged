// Copyright (c) 2023-2026 The Dogecoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <api/api_server.h>

#include <api/api_util.h>
#include <api/chain_handler.h>
#include <api/dashboard_handler.h>
#include <api/events_handler.h>
#include <api/mining_handler.h>
#include <api/network_handler.h>
#include <api/stratum_handler.h>
#include <config.h>
#include <httpserver.h>
#include <logging.h>
#include <node/context.h>
#include <rpc/protocol.h>

#include <functional>
#include <string>
#include <vector>

static const char *API_PREFIX = "/api/v1/";

using ApiHandler =
    std::function<bool(node::NodeContext &, HTTPRequest *,
                       const std::vector<std::string> &,
                       const api::QueryParams &)>;

struct Route {
    HTTPRequest::RequestMethod method;
    std::string prefix;
    ApiHandler handler;
};

static std::vector<Route> g_routes;
static node::NodeContext *g_node{nullptr};

static void AddRoute(HTTPRequest::RequestMethod method,
                     const std::string &prefix, ApiHandler handler) {
    g_routes.push_back({method, prefix, std::move(handler)});
}

static bool DispatchRequest(Config &, HTTPRequest *req,
                            const std::string &strURIPart) {
    if (!g_node) {
        return false;
    }

    std::string fullPath = strURIPart;
    auto qp = api::ParseQueryString(fullPath);
    auto parts = api::SplitPath(fullPath);

    auto method = req->GetRequestMethod();

    if (method == HTTPRequest::OPTIONS) {
        req->WriteHeader("Access-Control-Allow-Origin", "*");
        req->WriteHeader("Access-Control-Allow-Methods",
                         "GET, POST, PUT, DELETE, OPTIONS");
        req->WriteHeader("Access-Control-Allow-Headers",
                         "Content-Type, Authorization");
        req->WriteHeader("Access-Control-Max-Age", "86400");
        req->WriteReply(HTTP_OK);
        return true;
    }

    const Route *bestMatch = nullptr;
    size_t bestLen = 0;

    for (const auto &route : g_routes) {
        if (route.method != method) {
            continue;
        }
        auto routeParts = api::SplitPath(route.prefix);
        if (routeParts.size() > parts.size()) {
            continue;
        }
        bool match = true;
        for (size_t i = 0; i < routeParts.size(); i++) {
            if (routeParts[i] != parts[i]) {
                match = false;
                break;
            }
        }
        if (match && routeParts.size() > bestLen) {
            bestLen = routeParts.size();
            bestMatch = &route;
        }
    }

    if (bestMatch) {
        return bestMatch->handler(*g_node, req, parts, qp);
    }

    api::WriteError(req, HTTP_NOT_FOUND, "not_found",
                    "Endpoint not found: /api/v1/" + fullPath);
    return true;
}

void StartAPI(node::NodeContext &node) {
    LogPrintf("Starting REST API v1\n");
    g_node = &node;
    g_routes.clear();

    AddRoute(HTTPRequest::GET, "chain", api::HandleGetChainInfo);
    AddRoute(HTTPRequest::GET, "chain/tip", api::HandleGetChainTip);
    AddRoute(HTTPRequest::GET, "blocks", api::HandleGetBlocks);
    AddRoute(HTTPRequest::GET, "network", api::HandleGetNetworkInfo);
    AddRoute(HTTPRequest::GET, "network/peers", api::HandleGetPeers);
    AddRoute(HTTPRequest::GET, "node", api::HandleGetNodeInfo);
    AddRoute(HTTPRequest::GET, "mining", api::HandleGetMiningInfo);
    AddRoute(HTTPRequest::GET, "events", api::HandleGetEvents);
    AddRoute(HTTPRequest::GET, "stratum", api::HandleGetStratumInfo);
    AddRoute(HTTPRequest::GET, "stratum/workers",
             api::HandleGetStratumWorkers);
    AddRoute(HTTPRequest::GET, "mergemine", api::HandleGetMergeMineInfo);

    api::StartEvents();

    RegisterHTTPHandler(API_PREFIX, false, DispatchRequest);

    auto dashHandler = [](Config &, HTTPRequest *req, const std::string &) {
        return api::HandleGetDashboard(req);
    };
    RegisterHTTPHandler("/dashboard", true, dashHandler);
}

void InterruptAPI() {}

void StopAPI() {
    LogPrintf("Stopping REST API v1\n");
    api::StopEvents();
    UnregisterHTTPHandler("/dashboard", true);
    UnregisterHTTPHandler(API_PREFIX, false);
    g_routes.clear();
    g_node = nullptr;
}
