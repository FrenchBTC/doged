// Copyright (c) 2025 Tobias Ruck and Alexandre Guillioud
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <stratum/stratumaux.h>

#include <arith_uint256.h>
#include <chainparams.h>
#include <config.h>
#include <consensus/merkle.h>
#include <script/script.h>
#include <hash.h>
#include <logging.h>
#include <node/miner.h>
#include <pow/pow.h>
#include <primitives/auxpow.h>
#include <streams.h>
#include <util/strencodings.h>
#include <util/translation.h>
#include <validation.h>

#include <set>

namespace stratum {

namespace {
/**
 * Serialize the standard FABE6D6D + root_BE + treeSize_LE + nonce_LE payload
 * that goes into the parent coinbase scriptSig.
 */
void SerializeCommitmentPayload(std::vector<uint8_t> &out,
                                const uint256 &root, uint32_t treeSize,
                                uint32_t nonce) {
    out.clear();
    out.insert(out.end(), MERGE_MINE_PREFIX.begin(), MERGE_MINE_PREFIX.end());

    uint256 rootBE = root;
    std::reverse(rootBE.begin(), rootBE.end());
    out.insert(out.end(), rootBE.begin(), rootBE.end());

    out.push_back(treeSize & 0xff);
    out.push_back((treeSize >> 8) & 0xff);
    out.push_back((treeSize >> 16) & 0xff);
    out.push_back((treeSize >> 24) & 0xff);

    out.push_back(nonce & 0xff);
    out.push_back((nonce >> 8) & 0xff);
    out.push_back((nonce >> 16) & 0xff);
    out.push_back((nonce >> 24) & 0xff);
}

/**
 * Walk a balanced merkle tree (leaves padded to a power of two) and
 * extract the inclusion branch for `index`. Also returns the root.
 */
void ExtractBranch(const std::vector<uint256> &leaves, uint32_t index,
                   std::vector<uint256> &branchOut, uint256 &rootOut) {
    branchOut.clear();
    std::vector<uint256> level = leaves;
    size_t idx = index;
    while (level.size() > 1) {
        size_t siblingIdx = idx ^ 1;
        if (siblingIdx < level.size()) {
            branchOut.push_back(level[siblingIdx]);
        } else {
            branchOut.push_back(level[idx]);
        }
        std::vector<uint256> nextLevel;
        for (size_t i = 0; i < level.size(); i += 2) {
            const uint256 &left = level[i];
            const uint256 &right =
                (i + 1 < level.size()) ? level[i + 1] : left;
            nextLevel.push_back(Hash(Span(left), Span(right)));
        }
        level = nextLevel;
        idx /= 2;
    }
    rootOut = level[0];
}
} // namespace

static std::unique_ptr<StratumAuxManager> g_globalAuxMgr;

void InitGlobalAuxManager(Chainstate &chainstate, const CTxMemPool *mempool,
                          const CChainParams &params) {
    g_globalAuxMgr = std::make_unique<StratumAuxManager>(
        chainstate, mempool, params, CScript() << OP_TRUE);
}

StratumAuxManager *GetGlobalAuxManager() {
    return g_globalAuxMgr.get();
}

void StopGlobalAuxManager() {
    g_globalAuxMgr.reset();
}

StratumAuxManager::StratumAuxManager(Chainstate &chainstate,
                                     const CTxMemPool *mempool,
                                     const CChainParams &params,
                                     const CScript &coinbaseScript)
    : m_chainstate(chainstate), m_mempool(mempool), m_params(params),
      m_coinbaseScript(coinbaseScript) {}

void StratumAuxManager::SetCoinbaseScript(const CScript &script) {
    LOCK(m_mutex);
    m_coinbaseScript = script;
}

util::Result<AuxWorkTemplate> StratumAuxManager::CreateAuxWork() {
    LOCK(m_mutex);
    LOCK(cs_main);

    const CBlockIndex *pindexPrev = m_chainstate.m_chain.Tip();
    if (!pindexPrev) {
        return {{_("No chain tip available")}};
    }

    node::BlockAssembler assembler(::GetConfig(), m_chainstate, m_mempool);
    auto blockTemplate = assembler.CreateNewBlock(m_coinbaseScript);
    if (!blockTemplate) {
        return {{_("Failed to create block template")}};
    }

    CBlock &block = blockTemplate->block;

    // CreateNewBlock doesn't compute hashMerkleRoot; compute it now
    block.hashMerkleRoot = BlockMerkleRoot(block);

    // Build the StratumJob underlying this aux work
    StratumJob job;
    job.jobId = 0; // aux work uses hash as key, not jobId
    job.block = std::make_shared<CBlock>(block);
    job.nBitsRaw = block.nBits;
    job.nVersionRaw = block.nVersion;
    job.height = pindexPrev->nHeight + 1;

    // The aux block hash must be computed with the AuxPoW version bit set,
    // since SubmitAuxBlock sets this bit before ProcessNewBlock, and
    // validation uses block.GetHash() (with the bit set) for CheckAuxBlockHash.
    CBlockHeader hdr;
    hdr.nVersion = VersionWithAuxPow(block.nVersion, true);
    hdr.hashPrevBlock = block.hashPrevBlock;
    hdr.hashMerkleRoot = block.hashMerkleRoot;
    hdr.nTime = block.nTime;
    hdr.nBits = block.nBits;
    hdr.nNonce = block.nNonce;
    uint256 auxBlockHash = hdr.GetHash();

    // Compute network target from nBits
    arith_uint256 target;
    NBitsToTarget(m_params.GetConsensus(), block.nBits, target);

    Amount coinbaseValue = Amount::zero();
    if (!block.vtx.empty() && !block.vtx[0]->vout.empty()) {
        for (const auto &out : block.vtx[0]->vout) {
            coinbaseValue += out.nValue;
        }
    }

    AuxWorkTemplate work;
    work.auxBlockHash = auxBlockHash;
    work.nChainId = AUXPOW_CHAIN_ID;
    work.prevBlockHash = block.hashPrevBlock;
    work.coinbaseValue = coinbaseValue;
    work.nBits = block.nBits;
    work.height = job.height;
    work.target = target;
    work.underlyingJob = std::move(job);

    m_pendingWork[auxBlockHash] = work;
    m_workInsertOrder.push_back(auxBlockHash);

    // FIFO eviction: remove oldest entries first
    while (m_pendingWork.size() > 32) {
        uint256 oldest = m_workInsertOrder.front();
        m_workInsertOrder.pop_front();
        m_pendingWork.erase(oldest);
    }

    return work;
}

MergeMineCommitment StratumAuxManager::BuildCommitment(
    const uint256 &auxBlockHash,
    const std::vector<uint256> &otherAuxHashes) const {

    // Backward-compatible single-chain path: only DOGE is placed at the
    // CalcExpectedMerkleTreeIndex slot. Other leaves are filled into
    // remaining slots without per-chain validation. Used when the caller
    // doesn't know the other chains' chain IDs.
    if (otherAuxHashes.empty()) {
        MergeMineCommitment commitment;
        commitment.nTreeSize = 1;
        commitment.nMergeMineNonce = 0;
        commitment.nChainIndex = 0;
        commitment.chainMerkleRoot = auxBlockHash;
        commitment.perChain[AUXPOW_CHAIN_ID] = {0, {}};
        SerializeCommitmentPayload(commitment.coinbasePayload,
                                   commitment.chainMerkleRoot,
                                   commitment.nTreeSize,
                                   commitment.nMergeMineNonce);
        return commitment;
    }

    // Multi-chain without explicit IDs: fall back to placing DOGE only.
    // Callers wanting strict per-chain placement should use
    // BuildMultiChainCommitment.
    std::vector<uint256> leaves;
    leaves.push_back(auxBlockHash);
    for (const auto &h : otherAuxHashes) {
        leaves.push_back(h);
    }

    uint32_t treeSize = 1;
    uint32_t merkleHeight = 0;
    while (treeSize < leaves.size()) {
        treeSize <<= 1;
        merkleHeight++;
    }
    while (leaves.size() < treeSize) {
        leaves.push_back(uint256());
    }

    uint32_t dogeIndex = 0;
    uint32_t nonce = 0;
    for (; nonce < 0xFFFFFFFF; nonce++) {
        uint32_t idx =
            CalcExpectedMerkleTreeIndex(nonce, AUXPOW_CHAIN_ID, merkleHeight);
        if (idx < treeSize) {
            dogeIndex = idx;
            break;
        }
    }
    if (dogeIndex != 0) {
        std::swap(leaves[0], leaves[dogeIndex]);
    }

    MergeMineCommitment commitment;
    commitment.nTreeSize = treeSize;
    commitment.nMergeMineNonce = nonce;
    commitment.nChainIndex = dogeIndex;

    ExtractBranch(leaves, dogeIndex, commitment.chainMerkleBranch,
                  commitment.chainMerkleRoot);
    commitment.perChain[AUXPOW_CHAIN_ID] = {dogeIndex,
                                            commitment.chainMerkleBranch};

    SerializeCommitmentPayload(commitment.coinbasePayload,
                               commitment.chainMerkleRoot,
                               commitment.nTreeSize,
                               commitment.nMergeMineNonce);
    return commitment;
}

MergeMineCommitment StratumAuxManager::BuildMultiChainCommitment(
    const uint256 &dogeAuxHash,
    const std::vector<std::pair<uint32_t, uint256>> &otherChains) const {

    // Aggregate all chains into a single (chainId -> hash) table. DOGE
    // counts as a participant under its own AuxPoW chain ID.
    std::vector<std::pair<uint32_t, uint256>> allChains;
    allChains.emplace_back(AUXPOW_CHAIN_ID, dogeAuxHash);
    for (const auto &c : otherChains) {
        if (c.first == AUXPOW_CHAIN_ID) {
            // Skip duplicates of our own chain ID; the caller probably made
            // a mistake and we'd otherwise place two leaves at the same
            // expected slot.
            continue;
        }
        allChains.push_back(c);
    }

    MergeMineCommitment commitment;

    if (allChains.size() == 1) {
        commitment.nTreeSize = 1;
        commitment.nMergeMineNonce = 0;
        commitment.nChainIndex = 0;
        commitment.chainMerkleRoot = dogeAuxHash;
        commitment.perChain[AUXPOW_CHAIN_ID] = {0, {}};
        SerializeCommitmentPayload(commitment.coinbasePayload,
                                   commitment.chainMerkleRoot,
                                   commitment.nTreeSize,
                                   commitment.nMergeMineNonce);
        return commitment;
    }

    // Smallest power of two >= chain count.
    uint32_t treeSize = 1;
    uint32_t merkleHeight = 0;
    while (treeSize < allChains.size()) {
        treeSize <<= 1;
        merkleHeight++;
    }

    // Search for a nonce that places every chain at a unique slot. The
    // address space is 2^32 and collisions are rare for small N, so this
    // typically terminates within a handful of iterations.
    uint32_t nonce = 0;
    std::map<uint32_t, uint32_t> slots; // chainId -> slot
    for (; nonce < 0xFFFFFFFF; ++nonce) {
        slots.clear();
        std::set<uint32_t> used;
        bool ok = true;
        for (const auto &[chainId, _hash] : allChains) {
            uint32_t slot =
                CalcExpectedMerkleTreeIndex(nonce, chainId, merkleHeight);
            if (used.count(slot)) {
                ok = false;
                break;
            }
            used.insert(slot);
            slots[chainId] = slot;
        }
        if (ok) {
            break;
        }
    }

    std::vector<uint256> leaves(treeSize);
    for (const auto &[chainId, hash] : allChains) {
        leaves[slots[chainId]] = hash;
    }

    commitment.nTreeSize = treeSize;
    commitment.nMergeMineNonce = nonce;

    // Extract the branch for each participating chain. The first iteration
    // also gives us the canonical root.
    bool rootSet = false;
    for (const auto &[chainId, _hash] : allChains) {
        uint32_t slot = slots[chainId];
        ChainMerklePath path;
        path.nChainIndex = slot;
        uint256 root;
        ExtractBranch(leaves, slot, path.chainMerkleBranch, root);
        if (!rootSet) {
            commitment.chainMerkleRoot = root;
            rootSet = true;
        }
        commitment.perChain[chainId] = std::move(path);
    }

    // Legacy fields refer to the DOGE leaf for backward compatibility.
    auto dogeIt = commitment.perChain.find(AUXPOW_CHAIN_ID);
    if (dogeIt != commitment.perChain.end()) {
        commitment.nChainIndex = dogeIt->second.nChainIndex;
        commitment.chainMerkleBranch = dogeIt->second.chainMerkleBranch;
    }

    SerializeCommitmentPayload(commitment.coinbasePayload,
                               commitment.chainMerkleRoot,
                               commitment.nTreeSize,
                               commitment.nMergeMineNonce);

    LogPrint(BCLog::MERGEMINE,
             "BuildMultiChainCommitment: %zu chain(s), treeSize=%u, "
             "nonce=%u, dogeIndex=%u\n",
             allChains.size(), treeSize, nonce, commitment.nChainIndex);

    return commitment;
}

util::Result<std::shared_ptr<CAuxPow>> StratumAuxManager::AssembleAuxPow(
    const AuxWorkTemplate &work,
    const AuxPowSubmission &submission) const {

    auto auxpow = std::make_shared<CAuxPow>();

    // Coinbase must be at index 0
    if (submission.coinbaseMerkleIndex != 0) {
        return {{_("AuxPow coinbase must be at merkle index 0")}};
    }

    // Parent chain ID must not be Dogecoin's
    if (VersionChainId(submission.parentHeader.nVersion) == AUXPOW_CHAIN_ID) {
        return {{_("AuxPow parent has our chain ID")}};
    }

    auxpow->coinbaseTx = submission.parentCoinbaseTx;
    auxpow->hashBlock = submission.parentBlockHash;
    auxpow->vMerkleBranch = submission.coinbaseMerkleBranch;
    auxpow->nIndex = submission.coinbaseMerkleIndex;

    // Build commitment to find chain merkle data
    MergeMineCommitment commitment = BuildCommitment(work.auxBlockHash);
    auxpow->vChainMerkleBranch = commitment.chainMerkleBranch;
    auxpow->nChainIndex = commitment.nChainIndex;

    auxpow->parentBlock = submission.parentHeader;

    // Validate the assembled AuxPow
    const Consensus::Params &consensus = m_params.GetConsensus();
    util::Result<std::monostate> checkResult =
        auxpow->CheckAuxBlockHash(work.auxBlockHash,
                                   VersionChainId(work.underlyingJob.nVersionRaw),
                                   consensus);
    if (!checkResult) {
        return {{util::ErrorString(checkResult)}};
    }

    return auxpow;
}

bool StratumAuxManager::ValidateParentPow(
    const CBaseBlockHeader &parentHeader, uint32_t dogeNBits,
    const Consensus::Params &params) const {
    // PoW is Scrypt on the parent header, compared to Doge's nBits target
    BlockHash powHash = parentHeader.GetPowHash();
    return CheckProofOfWork(powHash, dogeNBits, params);
}

bool StratumAuxManager::SubmitAuxBlock(const AuxWorkTemplate &work,
                                        std::shared_ptr<CAuxPow> auxpow,
                                        ChainstateManager &chainman) {
    auto block = std::make_shared<CBlock>(*work.underlyingJob.block);

    // Set the AuxPoW version flag
    block->nVersion =
        VersionWithAuxPow(work.underlyingJob.nVersionRaw, true);
    block->auxpow = std::move(auxpow);

    // CreateNewBlock doesn't compute hashMerkleRoot; do it now.
    block->hashMerkleRoot = BlockMerkleRoot(*block);

    bool newBlock = false;
    return chainman.ProcessNewBlock(block, /*force_processing=*/true,
                                    /*min_pow_checked=*/true, &newBlock);
}

std::optional<AuxWorkTemplate>
StratumAuxManager::GetWork(const uint256 &auxBlockHash) const {
    LOCK(m_mutex);
    auto it = m_pendingWork.find(auxBlockHash);
    if (it == m_pendingWork.end()) {
        return std::nullopt;
    }
    return it->second;
}

void StratumAuxManager::RemoveWork(const uint256 &auxBlockHash) {
    LOCK(m_mutex);
    m_pendingWork.erase(auxBlockHash);
}

void StratumAuxManager::PruneWork(size_t keepCount) {
    LOCK(m_mutex);
    while (m_pendingWork.size() > keepCount && !m_workInsertOrder.empty()) {
        uint256 oldest = m_workInsertOrder.front();
        m_workInsertOrder.pop_front();
        m_pendingWork.erase(oldest);
    }
}

} // namespace stratum
