// Copyright (c) 2024 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow/pow.h>

#include <chain.h>
#include <chainparams.h>
#include <config.h>

#include <test/util/random.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

std::vector<CBlockIndex> MakeMockBlocks(size_t length, int32_t startHeight) {
    std::vector<CBlockIndex> blocks(length);
    for (size_t i = 0; i < length; i++) {
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = startHeight + i;
    }
    return blocks;
}

BOOST_FIXTURE_TEST_SUITE(dogecoin_daa_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(test_first_daa) {
    DummyConfig config(ChainTypeToString(ChainType::MAIN));
    std::vector<CBlockIndex> blocks = MakeMockBlocks(240, 0);

    CBlockHeader header;
    blocks[0].nTime = 1386325540; // Block # 0

    // f9533416310fc4484cf43405a858b06afc9763ad401d267c1835d77e7d225a4e
    CBlockIndex *pindexLast = &blocks[239];
    BOOST_CHECK_EQUAL(pindexLast->nHeight, 239);
    pindexLast->nTime = 1386475638; // Block #239
    pindexLast->nBits = 0x1e0ffff0;

    const uint32_t expected_nbits = 0x1e0fffff;
    BOOST_CHECK_EQUAL(
        GetNextWorkRequired(pindexLast, &header, config.GetChainParams()),
        expected_nbits);
    BOOST_CHECK(PermittedDifficultyTransition(
        config.GetChainParams().GetConsensus(), pindexLast->nHeight + 1,
        pindexLast->nBits, expected_nbits));
}

BOOST_AUTO_TEST_CASE(get_next_work_pre_digishield) {
    DummyConfig config(ChainTypeToString(ChainType::MAIN));
    std::vector<CBlockIndex> blocks = MakeMockBlocks(241, 9359);

    CBlockHeader header;
    blocks[0].nTime = 1386942008; // Block # 9359

    CBlockIndex *pindexLast = &blocks[240];
    BOOST_CHECK_EQUAL(pindexLast->nHeight, 9599);
    pindexLast->nTime = 1386954113;
    pindexLast->nBits = 0x1c1a1206;

    const uint32_t expected_nbits = 0x1c15ea59;
    BOOST_CHECK_EQUAL(
        GetNextWorkRequired(pindexLast, &header, config.GetChainParams()),
        expected_nbits);
    BOOST_CHECK(PermittedDifficultyTransition(
        config.GetChainParams().GetConsensus(), pindexLast->nHeight + 1,
        pindexLast->nBits, expected_nbits));
}

BOOST_AUTO_TEST_CASE(get_next_work_digishield) {
    DummyConfig config(ChainTypeToString(ChainType::MAIN));
    std::vector<CBlockIndex> blocks = MakeMockBlocks(2, 144999);

    CBlockHeader header;
    blocks[0].nTime = 1395094427; // Block # 144999

    // First hard-fork at 145,000, which applies to block 145,001 onwards
    CBlockIndex *pindexLast = &blocks[1];
    BOOST_CHECK_EQUAL(pindexLast->nHeight, 145000);
    pindexLast->nTime = 1395094679;
    pindexLast->nBits = 0x1b499dfd;

    const uint32_t expected_nbits = 0x1b671062;
    BOOST_CHECK_EQUAL(
        GetNextWorkRequired(pindexLast, &header, config.GetChainParams()),
        expected_nbits);
    BOOST_CHECK(PermittedDifficultyTransition(
        config.GetChainParams().GetConsensus(), pindexLast->nHeight + 1,
        pindexLast->nBits, expected_nbits));
}

BOOST_AUTO_TEST_CASE(get_next_work_digishield_modulated_upper) {
    DummyConfig config(ChainTypeToString(ChainType::MAIN));
    std::vector<CBlockIndex> blocks = MakeMockBlocks(2, 145106);

    CBlockHeader header;
    blocks[0].nTime = 1395100835; // Block # 145,106

    // Test the upper bound on modulated time using mainnet block #145,107
    CBlockIndex *pindexLast = &blocks[1];
    BOOST_CHECK_EQUAL(pindexLast->nHeight, 145107);
    pindexLast->nTime = 1395101360;
    pindexLast->nBits = 0x1b3439cd;

    const uint32_t expected_nbits = 0x1b4e56b3;
    BOOST_CHECK_EQUAL(
        GetNextWorkRequired(pindexLast, &header, config.GetChainParams()),
        expected_nbits);
    BOOST_CHECK(PermittedDifficultyTransition(
        config.GetChainParams().GetConsensus(), pindexLast->nHeight + 1,
        pindexLast->nBits, expected_nbits));
    BOOST_CHECK(!PermittedDifficultyTransition(
        config.GetChainParams().GetConsensus(), pindexLast->nHeight + 1,
        pindexLast->nBits, expected_nbits + 1));
}

BOOST_AUTO_TEST_CASE(get_next_work_digishield_modulated_lower) {
    DummyConfig config(ChainTypeToString(ChainType::MAIN));
    std::vector<CBlockIndex> blocks = MakeMockBlocks(2, 149422);

    CBlockHeader header;
    blocks[0].nTime = 1395380517; // Block # 149,422

    // Test the lower bound on modulated time using mainnet block #149,423
    CBlockIndex *pindexLast = &blocks[1];
    BOOST_CHECK_EQUAL(pindexLast->nHeight, 149423);
    pindexLast->nTime = 1395380447;
    pindexLast->nBits = 0x1b446f21;
    const uint32_t expected_nbits = 0x1b335358;
    BOOST_CHECK_EQUAL(
        GetNextWorkRequired(pindexLast, &header, config.GetChainParams()),
        expected_nbits);
    BOOST_CHECK(PermittedDifficultyTransition(
        config.GetChainParams().GetConsensus(), pindexLast->nHeight + 1,
        pindexLast->nBits, expected_nbits));
    BOOST_CHECK(!PermittedDifficultyTransition(
        config.GetChainParams().GetConsensus(), pindexLast->nHeight + 1,
        pindexLast->nBits, expected_nbits - 1));
}

BOOST_AUTO_TEST_CASE(get_next_work_digishield_rounding) {
    DummyConfig config(ChainTypeToString(ChainType::MAIN));
    std::vector<CBlockIndex> blocks = MakeMockBlocks(2, 145000);

    CBlockHeader header;
    blocks[0].nTime = 1395094679;

    // Test case for correct rounding of modulated time - this depends on
    // handling of integer division, and is not obvious from the code
    CBlockIndex *pindexLast = &blocks[1];
    BOOST_CHECK_EQUAL(pindexLast->nHeight, 145001);
    pindexLast->nTime = 1395094727;
    pindexLast->nBits = 0x1b671062;
    const uint32_t expected_nbits = 0x1b6558a4;
    BOOST_CHECK_EQUAL(
        GetNextWorkRequired(pindexLast, &header, config.GetChainParams()),
        expected_nbits);
    BOOST_CHECK(PermittedDifficultyTransition(
        config.GetChainParams().GetConsensus(), pindexLast->nHeight + 1,
        pindexLast->nBits, expected_nbits));
}

// Helper: build a chain of `length` blocks at a fixed nTime so that
// GetMedianTimePast() of the last block returns exactly that time.
static std::vector<CBlockIndex>
MakeMockBlocksWithTime(size_t length, int32_t startHeight, int64_t blockTime,
                       uint32_t nBits) {
    std::vector<CBlockIndex> blocks = MakeMockBlocks(length, startHeight);
    for (auto &b : blocks) {
        b.nTime = blockTime;
        b.nBits = nBits;
    }
    return blocks;
}

// --- Testnet DAA spam fix tests -------------------------------------------
// The legacy testnet rule allows min-difficulty (drop to powLimit) after a
// 2*spacing (120s) gap, with no cap on the drop. The fix:
//   1. Increases the gate to 10*spacing (600s).
//   2. Caps the drop at prev_target * 4 (i.e. 1/4 of previous difficulty),
//      not all the way to powLimit.
// Activation is HEIGHT-based at block 2,240,000 (the last commonly-agreed
// honest header observed during presync). Pre-fork heights retain legacy
// behaviour; post-fork heights enforce the capped/throttled rule.

constexpr int32_t TESTNET_DAA_FIX_HEIGHT = 2240000;
// Pre-activation height: post-Digishield (digishieldMinDiffHeight = 157500)
// but well below the fork height, so legacy min-difficulty path applies.
constexpr int32_t PRE_ACTIVATION_HEIGHT = 200000;
// Post-activation height: comfortably past the fork, so the new
// capped/throttled min-difficulty rule applies.
constexpr int32_t POST_ACTIVATION_HEIGHT = TESTNET_DAA_FIX_HEIGHT + 100;
// A representative non-trivial nBits well above powLimit. Compact 0x1d00ffff
// is the standard Bitcoin-difficulty "1" target.
constexpr uint32_t SAMPLE_NBITS = 0x1d00ffff;
// Arbitrary timestamp - block time no longer affects activation, only the
// inter-block spacing check.
constexpr int64_t SAMPLE_BLOCK_TIME = 1745020800;

BOOST_AUTO_TEST_CASE(testnet_daa_fix_legacy_rule_unchanged_pre_activation) {
    // Pre-activation height: 121-second gap should still trigger
    // min-difficulty and return powLimit (legacy behaviour preserved).
    DummyConfig config(ChainTypeToString(ChainType::TESTNET));

    std::vector<CBlockIndex> blocks = MakeMockBlocksWithTime(
        12, PRE_ACTIVATION_HEIGHT - 11, SAMPLE_BLOCK_TIME, SAMPLE_NBITS);
    CBlockIndex *pindexLast = &blocks.back();

    CBlockHeader header;
    header.nTime = pindexLast->GetBlockTime() + 121; // > 2*60s gap

    const uint32_t expected_nbits =
        UintToArith256(config.GetChainParams().GetConsensus().powLimit)
            .GetCompact();
    BOOST_CHECK_EQUAL(
        GetNextWorkRequired(pindexLast, &header, config.GetChainParams()),
        expected_nbits);
}

BOOST_AUTO_TEST_CASE(testnet_daa_fix_post_activation_short_gap_holds_diff) {
    // Post-activation: 121-second gap is BELOW the new 600s threshold, so
    // min-difficulty must NOT trigger. DigiShield then computes a regular
    // retarget for the (very) short timespan.
    DummyConfig config(ChainTypeToString(ChainType::TESTNET));

    std::vector<CBlockIndex> blocks = MakeMockBlocksWithTime(
        12, POST_ACTIVATION_HEIGHT - 11, SAMPLE_BLOCK_TIME, SAMPLE_NBITS);
    CBlockIndex *pindexLast = &blocks.back();

    CBlockHeader header;
    header.nTime = pindexLast->GetBlockTime() + 121; // < 600s, no min-diff

    const uint32_t powLimit =
        UintToArith256(config.GetChainParams().GetConsensus().powLimit)
            .GetCompact();
    const uint32_t result =
        GetNextWorkRequired(pindexLast, &header, config.GetChainParams());

    // Critical: must NOT have dropped to powLimit.
    BOOST_CHECK(result != powLimit);
}

BOOST_AUTO_TEST_CASE(testnet_daa_fix_post_activation_long_gap_capped) {
    // Post-activation: a 1000-second gap is past the new 600s threshold, so
    // min-difficulty triggers, but the drop must be CAPPED at prev_target*4
    // rather than going all the way to powLimit.
    DummyConfig config(ChainTypeToString(ChainType::TESTNET));

    std::vector<CBlockIndex> blocks = MakeMockBlocksWithTime(
        12, POST_ACTIVATION_HEIGHT - 11, SAMPLE_BLOCK_TIME, SAMPLE_NBITS);
    CBlockIndex *pindexLast = &blocks.back();

    CBlockHeader header;
    header.nTime = pindexLast->GetBlockTime() + 1000; // > 600s, triggers

    const uint32_t powLimit =
        UintToArith256(config.GetChainParams().GetConsensus().powLimit)
            .GetCompact();

    arith_uint256 expected;
    expected.SetCompact(SAMPLE_NBITS);
    expected *= 4;
    const uint32_t expected_nbits = expected.GetCompact();

    const uint32_t result =
        GetNextWorkRequired(pindexLast, &header, config.GetChainParams());

    BOOST_CHECK_EQUAL(result, expected_nbits);
    // Sanity: the cap is meaningful, i.e. we did NOT drop to powLimit.
    BOOST_CHECK(result != powLimit);

    // PermittedDifficultyTransition must accept this legitimate drop after
    // the fix: gating is now purely height-based.
    BOOST_CHECK(PermittedDifficultyTransition(
        config.GetChainParams().GetConsensus(), pindexLast->nHeight + 1,
        pindexLast->nBits, expected_nbits, header.nTime));
}

BOOST_AUTO_TEST_CASE(testnet_daa_fix_rejects_uncapped_min_diff) {
    // Post-activation height: a header claiming nBits = powLimit (the
    // spammer's forged claim) must be rejected by
    // PermittedDifficultyTransition.
    DummyConfig config(ChainTypeToString(ChainType::TESTNET));

    const uint32_t powLimit =
        UintToArith256(config.GetChainParams().GetConsensus().powLimit)
            .GetCompact();

    // At a post-activation height, the bogus drop to powLimit is rejected.
    BOOST_CHECK(!PermittedDifficultyTransition(
        config.GetChainParams().GetConsensus(),
        /*height=*/POST_ACTIVATION_HEIGHT,
        /*old_nbits=*/SAMPLE_NBITS,
        /*new_nbits=*/powLimit,
        /*new_block_time=*/SAMPLE_BLOCK_TIME));

    // The new_block_time argument is now ignored - the same call with
    // new_block_time=0 must yield the same rejection (proves activation is
    // height-based, not time-based).
    BOOST_CHECK(!PermittedDifficultyTransition(
        config.GetChainParams().GetConsensus(),
        /*height=*/POST_ACTIVATION_HEIGHT, SAMPLE_NBITS, powLimit,
        /*new_block_time=*/0));

    // At a pre-activation height, the same drop is allowed (legacy
    // behaviour preserved for the lower portion of the chain).
    BOOST_CHECK(PermittedDifficultyTransition(
        config.GetChainParams().GetConsensus(),
        /*height=*/PRE_ACTIVATION_HEIGHT, SAMPLE_NBITS, powLimit,
        /*new_block_time=*/SAMPLE_BLOCK_TIME));
}

BOOST_AUTO_TEST_SUITE_END()
