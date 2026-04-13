// Copyright (c) 2025-present The TeslaChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <consensus/merkle.h>
#include <hash.h>
#include <pow.h>
#include <test/util/random.h>
#include <test/util/common.h>
#include <test/util/setup_common.h>
#include <util/check.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

using namespace util::hex_literals;

BOOST_FIXTURE_TEST_SUITE(triadic_consensus_tests, BasicTestingSetup)

// ============================================================
// 1. Block type classification
// ============================================================
BOOST_AUTO_TEST_CASE(block_type_classification)
{
    // Genesis (height 0) — special LINK
    BOOST_CHECK_EQUAL(static_cast<int>(GetTriadicBlockType(0)),
                      static_cast<int>(TriadicBlockType::LINK));
    BOOST_CHECK(!IsAxisBlock(0));
    BOOST_CHECK(!IsSuperAxisBlock(0));

    // LINK blocks: 1, 2, 4, 5, 7, 8
    for (int h : {1, 2, 4, 5, 7, 8}) {
        BOOST_CHECK_MESSAGE(GetTriadicBlockType(h) == TriadicBlockType::LINK,
                           "height " << h << " should be LINK");
        BOOST_CHECK(!IsAxisBlock(h));
        BOOST_CHECK(!IsSuperAxisBlock(h));
    }

    // AXIS blocks (not SUPER): 3, 6
    for (int h : {3, 6}) {
        BOOST_CHECK_MESSAGE(GetTriadicBlockType(h) == TriadicBlockType::AXIS,
                           "height " << h << " should be AXIS");
        BOOST_CHECK(IsAxisBlock(h));
        BOOST_CHECK(!IsSuperAxisBlock(h));
    }

    // SUPER_AXIS blocks: 9 (also AXIS)
    BOOST_CHECK_EQUAL(GetTriadicBlockType(9), TriadicBlockType::SUPER_AXIS);
    BOOST_CHECK(IsAxisBlock(9));
    BOOST_CHECK(IsSuperAxisBlock(9));

    // CanReorganizeTo: AXIS blocks are final
    BOOST_CHECK(!CanReorganizeTo(3));
    BOOST_CHECK(!CanReorganizeTo(6));
    BOOST_CHECK(!CanReorganizeTo(9));
    // LINK blocks can be reorged
    BOOST_CHECK(CanReorganizeTo(1));
    BOOST_CHECK(CanReorganizeTo(2));
    BOOST_CHECK(CanReorganizeTo(4));
}

// ============================================================
// 2. CBlockHeader AXIS fields are settable and GetHash() works
// ============================================================
BOOST_AUTO_TEST_CASE(block_header_axis_fields)
{
    CBlockHeader h;
    h.SetNull();
    BOOST_CHECK(h.hashPrevAxisBlock.IsNull());
    BOOST_CHECK(h.hashAxisMerkleRoot.IsNull());

    // Set AXIS fields to non-zero
    uint256 nz;
    for (int i = 0; i < 32; ++i) nz.data()[i] = (unsigned char)i;
    h.hashPrevAxisBlock = nz;
    h.hashAxisMerkleRoot = nz;

    BOOST_CHECK(!h.hashPrevAxisBlock.IsNull());
    BOOST_CHECK(!h.hashAxisMerkleRoot.IsNull());

    // GetHash is idempotent
    uint256 hash1 = h.GetHash();
    uint256 hash2 = h.GetHash();
    BOOST_CHECK_EQUAL(hash1.ToString(), hash2.ToString());
    BOOST_CHECK(!hash1.IsNull());

    // Reset
    h.SetNull();
    BOOST_CHECK(h.hashPrevAxisBlock.IsNull());
    BOOST_CHECK(h.hashAxisMerkleRoot.IsNull());
}

// ============================================================
// 3. CBlockIndex AXIS fields round-trip through GetBlockHeader
// ============================================================
BOOST_AUTO_TEST_CASE(blockindex_get_header)
{
    uint256 axisHash, merkleRoot;
    for (int i = 0; i < 32; ++i) {
        axisHash.data()[i] = (unsigned char)(i ^ 0xAA);
        merkleRoot.data()[i] = (unsigned char)(i ^ 0xBB);
    }

    CBlockIndex idx;
    idx.hashPrevAxisBlock = axisHash;
    idx.hashAxisMerkleRoot = merkleRoot;

    CBlockHeader h = idx.GetBlockHeader();
    BOOST_CHECK_EQUAL(h.hashPrevAxisBlock.ToString(), axisHash.ToString());
    BOOST_CHECK_EQUAL(h.hashAxisMerkleRoot.ToString(), merkleRoot.ToString());
}

// ============================================================
// 4. Regtest genesis PoW is valid (TeslaChain primary test chain)
// ============================================================
BOOST_AUTO_TEST_CASE(regtest_genesis_pow)
{
    auto params = CreateChainParams(*m_node.args, ChainType::REGTEST);
    const CBlock& genesis = params->GenesisBlock();
    const auto& consensus = params->GetConsensus();

    // Verify genesis hash and chainparams consistency
    BOOST_CHECK_EQUAL(genesis.GetHash(), consensus.hashGenesisBlock);

    // Verify PoW
    bool powOk = CheckProofOfWork(genesis.GetHash(), genesis.nBits, consensus);
    BOOST_CHECK_MESSAGE(powOk, "Regtest genesis PoW should be valid");

    // Check basic params
    BOOST_CHECK_EQUAL(genesis.nNonce, 1U);        // nonce=1 for 144-byte header
    BOOST_CHECK_EQUAL(genesis.nTime, 1775858400U); // 2026-04-10 22:00:00 UTC
}

// ============================================================
// 5. TeslaChain mainnet genesis PoW is valid
// ============================================================
BOOST_AUTO_TEST_CASE(teslachain_genesis_pow)
{
    auto params = CreateChainParams(*m_node.args, ChainType::TESLACHAIN);
    const CBlock& genesis = params->GenesisBlock();
    const auto& consensus = params->GetConsensus();

    BOOST_CHECK_EQUAL(genesis.GetHash(), consensus.hashGenesisBlock);
    bool powOk = CheckProofOfWork(genesis.GetHash(), genesis.nBits, consensus);
    BOOST_CHECK_MESSAGE(powOk, "TeslaChain genesis PoW should be valid");
    BOOST_CHECK_EQUAL(genesis.nNonce, 2U);
}

// ============================================================
// 6. Regtest genesis Merkle root is consistent
// ============================================================
BOOST_AUTO_TEST_CASE(regtest_merkle_root)
{
    auto params = CreateChainParams(*m_node.args, ChainType::REGTEST);
    const CBlock& genesis = params->GenesisBlock();

    uint256 computedMerkle = BlockMerkleRoot(genesis);
    BOOST_CHECK_EQUAL(computedMerkle.ToString(), genesis.hashMerkleRoot.ToString());
}

// ============================================================
// 7. Hash(a || b) != Hash(b || a) — order matters for AXIS merkle
// ============================================================
BOOST_AUTO_TEST_CASE(axis_merkle_accumulation_order)
{
    uint256 a{}, b{};
    for (int i = 0; i < 32; ++i) {
        a.data()[i] = 0xAA;
        b.data()[i] = 0xBB;
    }

    HashWriter ss1;
    ss1 << a << b;
    uint256 h1 = ss1.GetHash();

    HashWriter ss2;
    ss2 << b << a;
    uint256 h2 = ss2.GetHash();

    BOOST_CHECK(h1 != h2);
}

// ============================================================
// 8. Super AXIS block (height 9) is also an AXIS block
// ============================================================
BOOST_AUTO_TEST_CASE(super_axis_is_also_axis)
{
    BOOST_CHECK(IsSuperAxisBlock(9));
    BOOST_CHECK(IsAxisBlock(9));
    BOOST_CHECK_EQUAL(GetTriadicBlockType(9), TriadicBlockType::SUPER_AXIS);

    BOOST_CHECK(!IsSuperAxisBlock(6));
    BOOST_CHECK(IsAxisBlock(6));
}

BOOST_AUTO_TEST_SUITE_END()
