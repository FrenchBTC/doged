// Copyright (c) 2023-2026 The Dogecoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <api/stratum_handler.h>

#include <httpserver.h>
#include <stratum/mergemine.h>
#include <stratum/stratum.h>
#include <stratum/stratumstats.h>
#include <univalue.h>

namespace api {

bool HandleGetStratumInfo(node::NodeContext &, HTTPRequest *req,
                          const std::vector<std::string> &,
                          const QueryParams &) {
    stratum::StratumServer *server = stratum::GetStratumServer();
    if (!server) {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("enabled", false);
        WriteSuccess(req, obj);
        return true;
    }

    stratum::StratumServerStats stats = server->GetStats();
    UniValue json = stratum::FormatStatsJson(stats);
    json.pushKV("enabled", true);

    WriteSuccess(req, json);
    return true;
}

bool HandleGetStratumWorkers(node::NodeContext &, HTTPRequest *req,
                             const std::vector<std::string> &,
                             const QueryParams &) {
    stratum::StratumServer *server = stratum::GetStratumServer();
    if (!server) {
        UniValue arr(UniValue::VARR);
        WriteSuccess(req, arr);
        return true;
    }

    stratum::StratumServerStats stats = server->GetStats();

    UniValue arr(UniValue::VARR);
    for (const auto &w : stats.workers) {
        UniValue wObj(UniValue::VOBJ);
        wObj.pushKV("name", w.workerName);
        wObj.pushKV("difficulty", w.currentDifficulty);
        wObj.pushKV("accepted", (int64_t)w.sharesAccepted);
        wObj.pushKV("rejected", (int64_t)w.sharesRejected);
        wObj.pushKV("stale", (int64_t)w.sharesStale);
        wObj.pushKV("hashrate", w.estimatedHashrate);
        wObj.pushKV("lastShareTime", (int64_t)w.lastShareTime);

        std::string stateStr;
        switch (w.state) {
            case stratum::StratumWorker::State::CONNECTED:
                stateStr = "connected";
                break;
            case stratum::StratumWorker::State::SUBSCRIBED:
                stateStr = "subscribed";
                break;
            case stratum::StratumWorker::State::AUTHORIZED:
                stateStr = "authorized";
                break;
            case stratum::StratumWorker::State::MINING:
                stateStr = "mining";
                break;
        }
        wObj.pushKV("state", stateStr);
        arr.push_back(wObj);
    }

    WriteSuccess(req, arr);
    return true;
}

bool HandleGetMergeMineInfo(node::NodeContext &, HTTPRequest *req,
                            const std::vector<std::string> &,
                            const QueryParams &) {
    UniValue obj(UniValue::VOBJ);

    auto *mm = stratum::GetMergeMineManager();
    if (!mm) {
        obj.pushKV("enabled", false);
        obj.pushKV("chains", UniValue(UniValue::VARR));
        WriteSuccess(req, obj);
        return true;
    }

    obj.pushKV("enabled", true);
    obj.pushKV("chainCount", (int64_t)mm->ChainCount());

    auto allWork = mm->GetAllWork();
    UniValue chains(UniValue::VARR);
    for (const auto &[name, work] : allWork) {
        UniValue c(UniValue::VOBJ);
        c.pushKV("name", name);
        c.pushKV("chainId", (int64_t)work.chainId);
        c.pushKV("height", (int64_t)work.height);
        c.pushKV("auxHash", work.auxHash.GetHex());
        c.pushKV("target", work.target);
        c.pushKV("fetchedAt", work.fetchedAt);
        chains.push_back(c);
    }
    obj.pushKV("chains", chains);

    WriteSuccess(req, obj);
    return true;
}

} // namespace api
