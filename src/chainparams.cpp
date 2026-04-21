// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2020 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>

#include <cashaddr.h>
#include <common/args.h>
#include <consensus/merkle.h>
#include <currencyunit.h>
#include <logging.h>
#include <tinyformat.h>
#include <uint256.h>
#include <util/chaintype.h>
#include <util/strencodings.h>

#include <cassert>
#include <limits>
#include <stdexcept>
#include <string>

static std::unique_ptr<const CChainParams> globalChainParams;

const CChainParams &Params() {
    assert(globalChainParams);
    return *globalChainParams;
}

void ReadChainArgs(const ArgsManager &args,
                   CChainParams::ChainOptions &options) {
    options.ecash = args.GetBoolArg("-ecash", cashaddr::DEFAULT_ECASH);
    // Only relevant for REGTEST
    options.fastprune = args.GetBoolArg("-fastprune", false);

    // Parse -recoverycheckpoint=HEIGHT:HASH (repeatable). Each entry is
    // injected into the chain's checkpoint map at construction so that any
    // header at HEIGHT with a different hash is rejected, and no fork can
    // ever reorg back below HEIGHT once the chain has passed it.
    //
    // Used to neatly fork the testnet away from the spam cascade by pinning
    // the last legitimate pre-spam tip. Silently ignored on mainnet (the
    // testnet/regtest chainparams constructors are the only ones that
    // consume options.extraCheckpoints).
    for (const std::string &raw : args.GetArgs("-recoverycheckpoint")) {
        const size_t sep = raw.find(':');
        if (sep == std::string::npos || sep == 0 || sep + 1 >= raw.size()) {
            LogPrintf("WARNING: -recoverycheckpoint=%s ignored (expected "
                      "HEIGHT:HASH)\n", raw);
            continue;
        }
        int64_t height = -1;
        try {
            height = std::stoll(raw.substr(0, sep));
        } catch (const std::exception &) {
            LogPrintf("WARNING: -recoverycheckpoint=%s ignored (bad height)\n",
                      raw);
            continue;
        }
        if (height < 0 || height > std::numeric_limits<int>::max()) {
            LogPrintf("WARNING: -recoverycheckpoint=%s ignored (height "
                      "out of range)\n", raw);
            continue;
        }
        const std::string hashHex = raw.substr(sep + 1);
        if (hashHex.size() != 64 || !IsHex(hashHex)) {
            LogPrintf("WARNING: -recoverycheckpoint=%s ignored (bad hash)\n",
                      raw);
            continue;
        }
        BlockHash hash{uint256S(hashHex)};
        options.extraCheckpoints.emplace_back(static_cast<int>(height), hash);
        LogPrintf("Recovery checkpoint registered: height=%d hash=%s\n",
                  static_cast<int>(height), hash.GetHex());
    }
}

std::unique_ptr<const CChainParams> CreateChainParams(const ArgsManager &args,
                                                      const ChainType chain) {
    auto opts = CChainParams::ChainOptions{};
    ReadChainArgs(args, opts);
    switch (chain) {
        case ChainType::MAIN:
            return CChainParams::Main(opts);
        case ChainType::TESTNET:
            return CChainParams::TestNet(opts);
        case ChainType::REGTEST: {
            return CChainParams::RegTest(opts);
        }
    }
    throw std::invalid_argument(
        strprintf("%s: Invalid ChainType value", __func__));
}

void SelectParams(const ChainType chain) {
    SelectBaseParams(chain);
    globalChainParams = CreateChainParams(gArgs, chain);
}
