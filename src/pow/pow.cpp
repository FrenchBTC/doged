// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2020 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow/pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <chainparams.h>
#include <common/system.h>
#include <consensus/activation.h>
#include <consensus/params.h>
#include <primitives/blockhash.h>

// Multiplier applied to nPowTargetSpacing for the min-difficulty time gate.
// Pre-fix: 2x spacing (120s on testnet). Post-fix: 10x spacing (600s on
// testnet) - makes the spam attack 5x slower per burst.
static constexpr int64_t LEGACY_MIN_DIFF_SPACING_MULT = 2;
static constexpr int64_t TESTNET_DAA_FIX_MIN_DIFF_SPACING_MULT = 10;

static int64_t MinDiffSpacingMultiplier(const Consensus::Params &params,
                                        const CBlockIndex *pindexPrev) {
    return IsTestnetDaaFixEnabled(params, pindexPrev)
               ? TESTNET_DAA_FIX_MIN_DIFF_SPACING_MULT
               : LEGACY_MIN_DIFF_SPACING_MULT;
}

// Cap the min-difficulty target so it cannot drop below 1/4 of the previous
// block's difficulty. Returns the compact representation of the capped target,
// still bounded by powLimit. Only used when the testnet DAA spam fix is active.
static uint32_t CappedMinDifficulty(const CBlockIndex *pindexPrev,
                                    const Consensus::Params &params) {
    arith_uint256 capped;
    capped.SetCompact(pindexPrev->nBits);
    // Multiplying the target by 4 == dividing the difficulty by 4.
    capped *= 4;
    const arith_uint256 powLimit = UintToArith256(params.powLimit);
    if (capped > powLimit) {
        capped = powLimit;
    }
    return capped.GetCompact();
}

// Dogecoin: Normally minimum difficulty blocks can only occur in between
// retarget blocks. However, once we introduce Digishield every block is
// a retarget, so we need to handle minimum difficulty on all blocks.
bool AllowDigishieldMinDifficultyForBlock(
    const CBlockIndex *pindexLast, const CBlockHeader *pblock,
    const Consensus::Params &params, const Consensus::DaaParams &daaParams) {
    // check if the chain allows minimum difficulty blocks
    if (!daaParams.fPowAllowMinDifficultyBlocks) {
        return false;
    }

    // Allow for a minimum block time if the elapsed time exceeds the
    // configured spacing multiplier.
    const int64_t mult = MinDiffSpacingMultiplier(params, pindexLast);
    return (pblock->GetBlockTime() >
            pindexLast->GetBlockTime() + params.nPowTargetSpacing * mult);
}

uint32_t GetNextWorkRequired(const CBlockIndex *pindexPrev,
                             const CBlockHeader *pblock,
                             const CChainParams &chainParams) {
    // GetNextWorkRequired should never be called on the genesis block
    assert(pindexPrev != nullptr);

    const Consensus::Params &params = chainParams.GetConsensus();

    // Special rule for regtest: we never retarget.
    if (params.fPowNoRetargeting) {
        return pindexPrev->nBits;
    }

    unsigned int nProofOfWorkLimit =
        UintToArith256(params.powLimit).GetCompact();

    const int32_t nHeight = pindexPrev->nHeight;
    const Consensus::DaaParams daaParams =
        params.DaaParamsAtHeight(nHeight + 1);

    const bool fTestnetDaaFix = IsTestnetDaaFixEnabled(params, pindexPrev);

    // Dogecoin: Special rules for minimum difficulty blocks with Digishield
    if (nHeight >= params.digishieldMinDiffHeight &&
        AllowDigishieldMinDifficultyForBlock(pindexPrev, pblock, params,
                                             daaParams)) {
        // Special difficulty rule for testnet:
        // If the new block's timestamp is more than the configured spacing
        // multiplier (2x pre-fix, 10x post-fix) then allow mining of a
        // min-difficulty block. Post-fix, the drop is capped at 1/4 of the
        // previous block's difficulty rather than the absolute powLimit.
        return fTestnetDaaFix ? CappedMinDifficulty(pindexPrev, params)
                              : nProofOfWorkLimit;
    }

    // Only change once per difficulty adjustment interval
    const int64_t defaultInterval =
        params.DifficultyAdjustmentInterval(daaParams);
    const int64_t difficultyAdjustmentInterval =
        daaParams.fDigishieldDifficultyCalculation ? 1 : defaultInterval;
    if ((nHeight + 1) % difficultyAdjustmentInterval != 0) {
        if (daaParams.fPowAllowMinDifficultyBlocks) {
            const int64_t mult = MinDiffSpacingMultiplier(params, pindexPrev);
            // Special difficulty rule for testnet:
            // If the new block's timestamp is more than the configured
            // spacing multiplier then allow mining of a min-difficulty
            // block. Post-fix, the drop is capped at 1/4 of the previous
            // block's difficulty.
            if (pblock->GetBlockTime() >
                pindexPrev->GetBlockTime() + params.nPowTargetSpacing * mult) {
                return fTestnetDaaFix ? CappedMinDifficulty(pindexPrev, params)
                                      : nProofOfWorkLimit;
            } else {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex *pindex = pindexPrev;
                while (pindex->pprev &&
                       pindex->nHeight % defaultInterval != 0 &&
                       pindex->nBits == nProofOfWorkLimit) {
                    pindex = pindex->pprev;
                }
                return pindex->nBits;
            }
        }
        return pindexPrev->nBits;
    }

    // Litecoin: This fixes an issue where a 51% attack can change difficulty at
    // will. Go back the full period unless it's the first retarget after
    // genesis.
    int32_t blocksToGoBack = difficultyAdjustmentInterval - 1;
    if (nHeight + 1 != difficultyAdjustmentInterval) {
        blocksToGoBack = difficultyAdjustmentInterval;
    }

    // Go back by what we want to be 14 days worth of blocks
    int32_t nHeightFirst = pindexPrev->nHeight - blocksToGoBack;
    assert(nHeightFirst >= 0);

    const CBlockIndex *pindexFirst = pindexPrev->GetAncestor(nHeightFirst);
    assert(pindexFirst);

    const int64_t retargetTimespan = daaParams.nPowTargetTimespan;
    const int64_t nActualTimespan =
        pindexPrev->GetBlockTime() - pindexFirst->GetBlockTime();
    int64_t nModulatedTimespan = nActualTimespan;

    if (daaParams.fDigishieldDifficultyCalculation) {
        // DigiShield implementation
        nModulatedTimespan =
            retargetTimespan + (nModulatedTimespan - retargetTimespan) / 8;
    }

    // Limit adjustment step
    if (nModulatedTimespan < daaParams.nMinTimespan) {
        nModulatedTimespan = daaParams.nMinTimespan;
    } else if (nModulatedTimespan > daaParams.nMaxTimespan) {
        nModulatedTimespan = daaParams.nMaxTimespan;
    }

    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew;
    arith_uint256 bnOld;
    bnNew.SetCompact(pindexPrev->nBits);
    bnOld = bnNew;
    bnNew *= nModulatedTimespan;
    bnNew /= retargetTimespan;

    if (bnNew > bnPowLimit) {
        bnNew = bnPowLimit;
    }

    return bnNew.GetCompact();
}

// Check that on difficulty adjustments, the new difficulty does not increase
// or decrease beyond the permitted limits.
bool PermittedDifficultyTransition(const Consensus::Params &params,
                                   int64_t height, uint32_t old_nbits,
                                   uint32_t new_nbits,
                                   int64_t new_block_time) {
    const Consensus::DaaParams daaParams = params.DaaParamsAtHeight(height - 1);

    // Pre-fix testnet/regtest behaviour: any difficulty transition is
    // permitted because min-difficulty blocks may legitimately drop the
    // target to powLimit at any time. Once the testnet DAA spam fix is
    // active (selected purely by the new block's height), do NOT
    // short-circuit so that spam headers are rejected during initial
    // header presync rather than wasting bandwidth downloading millions
    // of them. new_block_time is retained in the signature for backwards
    // compatibility but is no longer consulted for activation.
    (void)new_block_time;
    if (params.fPowNoRetargeting) {
        return true;
    }
    if (daaParams.fPowAllowMinDifficultyBlocks &&
        !IsTestnetDaaFixEnabled(params, height)) {
        return true;
    }

    // Keeping the same difficulty as the prev block is always permitted,
    // assuming the initial difficulty was valid, so bail out early.
    // The initial difficulty is valid because we start from the genesis block
    // and we stop calling this function as soon as it returns false.
    // This avoids further computation for most blocks prior to DAA forks.
    if (old_nbits == new_nbits) {
        return true;
    }

    // Prior to Digishield, the difficulty could change only every 240 blocks,
    // so we can bail out early if we observe a difficulty change at an
    // unexpected block height.
    if (!IsDigishieldEnabled(params, height - 1) &&
        height % params.DifficultyAdjustmentInterval(daaParams) != 0) {
        return false;
    }

    arith_uint256 observed_new_target;
    // Check [0, powLimit] range for all DAA algorithms.
    if (!NBitsToTarget(params, new_nbits, observed_new_target)) {
        return false;
    }

    int64_t smallest_timespan = daaParams.nMinTimespan;
    int64_t largest_timespan = daaParams.nMaxTimespan;

    // When the testnet DAA spam fix is active, a min-difficulty block can
    // legitimately drop the target by up to 4x (vs DigiShield's normal ~1.5x
    // bound). Relax the upper bound here to match CappedMinDifficulty so
    // honest min-diff blocks don't fail the presync transition check.
    if (IsTestnetDaaFixEnabled(params, height)) {
        const int64_t minDiffMaxTimespan = 4 * daaParams.nPowTargetTimespan;
        if (minDiffMaxTimespan > largest_timespan) {
            largest_timespan = minDiffMaxTimespan;
        }
    }

    const arith_uint256 pow_limit = UintToArith256(params.powLimit);
    observed_new_target.SetCompact(new_nbits);

    // Calculate the largest difficulty value possible:
    arith_uint256 largest_difficulty_target;
    largest_difficulty_target.SetCompact(old_nbits);
    largest_difficulty_target *= largest_timespan;
    largest_difficulty_target /= daaParams.nPowTargetTimespan;

    if (largest_difficulty_target > pow_limit) {
        largest_difficulty_target = pow_limit;
    }

    // Round and then compare this new calculated value to what is observed.
    arith_uint256 maximum_new_target;
    maximum_new_target.SetCompact(largest_difficulty_target.GetCompact());
    if (maximum_new_target < observed_new_target) {
        return false;
    }

    // Calculate the smallest difficulty value possible:
    arith_uint256 smallest_difficulty_target;
    smallest_difficulty_target.SetCompact(old_nbits);
    smallest_difficulty_target *= smallest_timespan;
    smallest_difficulty_target /= daaParams.nPowTargetTimespan;

    if (smallest_difficulty_target > pow_limit) {
        smallest_difficulty_target = pow_limit;
    }

    // Round and then compare this new calculated value to what is observed.
    arith_uint256 minimum_new_target;
    minimum_new_target.SetCompact(smallest_difficulty_target.GetCompact());
    if (minimum_new_target > observed_new_target) {
        return false;
    }

    return true;
}

bool CheckProofOfWork(const BlockHash &hash, uint32_t nBits,
                      const Consensus::Params &params) {
    arith_uint256 bnTarget;
    if (!NBitsToTarget(params, nBits, bnTarget)) {
        return false;
    }

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget) {
        return false;
    }

    return true;
}

bool NBitsToTarget(const Consensus::Params &params, uint32_t nBits,
                   arith_uint256 &target) {
    bool fNegative;
    bool fOverflow;

    target.SetCompact(nBits, &fNegative, &fOverflow);

    return !(fNegative || target == 0 || fOverflow ||
             target > UintToArith256(params.powLimit));
}
