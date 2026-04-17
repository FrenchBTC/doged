// Copyright (c) 2025 Tobias Ruck and Alexandre Guillioud
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_STRATUM_STRATUM_H
#define BITCOIN_STRATUM_STRATUM_H

#include <stratum/stratumaux.h>
#include <stratum/stratumconfig.h>
#include <stratum/stratumjob.h>
#include <stratum/stratumprotocol.h>
#include <stratum/stratumstats.h>
#include <stratum/stratumsubmit.h>
#include <stratum/stratumworker.h>
#include <validationinterface.h>

#include <atomic>
#include <map>
#include <memory>
#include <thread>

struct event_base;
struct evconnlistener;
struct bufferevent;

class CBlockIndex;
class CChainParams;
class Chainstate;
class ChainstateManager;
class CTxMemPool;

namespace stratum {

class StratumServer final : public CValidationInterface {
public:
    StratumServer(const StratumConfig &config, Chainstate &chainstate,
                  const CTxMemPool *mempool, const CChainParams &chainParams,
                  ChainstateManager &chainman);
    ~StratumServer();

    bool Start();
    void Interrupt();
    void Stop();

    void BroadcastJob(const StratumJob &job);
    void SendDifficulty(uint32_t sessionId, double difficulty);
    StratumServerStats GetStats() const;
    size_t GetWorkerCount() const;

    void OnExternalWorkUpdate(const std::string &chainName);

protected:
    void UpdatedBlockTip(const CBlockIndex *pindexNew,
                         const CBlockIndex *pindexFork,
                         bool fInitialDownload) override;

private:
    struct ClientSession {
        uint32_t sessionId;
        std::unique_ptr<StratumWorker> worker;
        StratumLineBuffer lineBuffer;
        struct bufferevent *bev = nullptr;
        std::set<std::string> submittedNonces;
    };

    StratumConfig m_config;
    Chainstate &m_chainstate;
    const CChainParams &m_chainParams;
    ChainstateManager &m_chainman;

    std::unique_ptr<StratumJobManager> m_jobMgr;
    std::unique_ptr<StratumAuxManager> m_auxMgr;
    ExtranonceMgr m_extranonceMgr;

    struct event_base *m_eventBase = nullptr;
    struct evconnlistener *m_listener = nullptr;
    std::thread m_eventThread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_interrupt{false};

    mutable RecursiveMutex m_cs;
    std::map<uint32_t, std::unique_ptr<ClientSession>> m_sessions
        GUARDED_BY(m_cs);
    uint32_t m_nextSessionId GUARDED_BY(m_cs) = 1;

    std::atomic<uint64_t> m_totalConnections{0};
    std::atomic<uint64_t> m_totalSharesAccepted{0};
    std::atomic<uint64_t> m_totalSharesRejected{0};
    std::atomic<uint64_t> m_totalSharesStale{0};
    std::atomic<uint64_t> m_blocksFound{0};
    int64_t m_startTime = 0;

    static void OnAccept(struct evconnlistener *listener, int fd,
                         struct sockaddr *addr, int socklen, void *ctx);
    static void OnRead(struct bufferevent *bev, void *ctx);
    static void OnEvent(struct bufferevent *bev, short events, void *ctx);

    void HandleAccept(int fd, struct sockaddr *addr);
    void HandleRead(uint32_t sessionId);
    void HandleDisconnect(uint32_t sessionId);

    void ProcessMessage(ClientSession &session, const StratumMessage &msg);
    void HandleSubscribe(ClientSession &session, const StratumRequest &req);
    void HandleAuthorize(ClientSession &session, const StratumRequest &req);
    void HandleSubmit(ClientSession &session, const StratumRequest &req);

    void SendToClient(ClientSession &session, const std::string &data);
    void SendResponse(ClientSession &session, int64_t id,
                      const UniValue &result, const UniValue &error);

    void CreateAndBroadcastJob(bool cleanJobs);
    void PeriodicMaintenance();
    void EventLoop();
};

bool InitStratumServer(const StratumConfig &config, Chainstate &chainstate,
                       const CTxMemPool *mempool,
                       const CChainParams &chainParams,
                       ChainstateManager &chainman);
void StartStratumServer();
void InterruptStratumServer();
void StopStratumServer();

StratumServer *GetStratumServer();

} // namespace stratum

#endif // BITCOIN_STRATUM_STRATUM_H
