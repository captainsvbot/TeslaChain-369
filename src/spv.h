// Copyright (c) 2024 TeslaChain contributors
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SPV_H
#define BITCOIN_SPV_H

#include <primitives/block.h>
#include <uint256.h>

#include <vector>

// TeslaChain SPV (Simplified Payment Verification) for AXIS blocks
//
// Background:
// - AXIS blocks occur every 3 blocks (heights 3, 6, 9, 12...)
// - Each AXIS block has hashPrevAxisBlock linking to the previous AXIS
// - hashAxisMerkleRoot is a cumulative merkle of all AXIS block hashes back to GENESIS
// - This creates a skip-chain that allows SPV verification without downloading full chain
//
// For SPV, client only needs AXIS headers (144 bytes each):
//   - At 1 AXIS per 3 blocks, average is ~48 bytes/block vs 80 for Bitcoin SPV

namespace spv {

// Hardcoded GENESIS hash for AXIS chain verification
static const uint256 AXIS_GENESIS_HASH{"144cc8ae15a2ba8590e05fa4ab6315eca0f08b26f4f2ef298f7bea271280f353"};

// Represents a single AXIS block header in the skip-chain
struct AxisHeader {
    int nHeight;          // Block height
    uint256 hash;         // Block hash (this AXIS block's PoW hash)
    uint256 hashPrevAxis; // Previous AXIS block hash
    uint256 hashAxisMerkleRoot; // Cumulative AXIS merkle root

    AxisHeader() : nHeight(0), hash(uint256::ZERO), hashPrevAxis(uint256::ZERO), hashAxisMerkleRoot(uint256::ZERO) {}

    AxisHeader(int height, const uint256& blockHash, const uint256& prevAxis, const uint256& axisMerkle)
        : nHeight(height), hash(blockHash), hashPrevAxis(prevAxis), hashAxisMerkleRoot(axisMerkle) {}
};

// Represents a merkle proof branch for a transaction
struct MerkleBranch {
    std::vector<uint256> hashes; // Intermediate merkle hashes
    std::vector<bool> right;      // For each hash: true if it's a right sibling, false if left

    void clear() { hashes.clear(); right.clear(); }
    size_t size() const { return hashes.size(); }
};

// Complete SPV proof for a transaction in an AXIS block
struct AxisSPVProof {
    // Target AXIS block (where the transaction resides)
    AxisHeader targetHeader;

    // Transaction details
    uint256 txid;           // Transaction ID
    uint256 txBlockHash;    // Hash of the block containing the tx (not necessarily AXIS for LINK blocks)
    int txHeight;           // Height of block containing tx
    int txIndex;            // Transaction index within block

    // Merkle proof from tx to block's merkle root
    MerkleBranch merkleBranch;

    // AXIS skip-chain headers (from target AXIS back to GENESIS)
    // This allows verification that targetHeader is properly linked
    std::vector<AxisHeader> axisChain;

    // PoW information
    uint32_t nBits;         // Difficulty target from target header
    uint256 blockHash;      // Full block hash of the AXIS block

    void clear() {
        targetHeader = AxisHeader();
        txid.SetNull();
        txBlockHash.SetNull();
        txHeight = 0;
        txIndex = 0;
        merkleBranch.clear();
        axisChain.clear();
        nBits = 0;
        blockHash.SetNull();
    }
};

/**
 * Check if a block height is an AXIS block (every 3 blocks: 3, 6, 9, 12...)
 */
inline bool IsAxisBlock(int nHeight) {
    return nHeight > 0 && nHeight % 3 == 0;
}

/**
 * Get the previous AXIS block height (skip of 3)
 */
inline int GetPrevAxisHeight(int nHeight) {
    return nHeight - 3;
}

/**
 * Build merkle branch from transaction position
 */
void BuildMerkleBranch(const std::vector<uint256>& merkleTree, int txIndex, MerkleBranch& branch);

/**
 * Verify merkle proof: compute root from txid and branch, compare to expected
 */
bool VerifyMerkleProof(const uint256& txid, const MerkleBranch& branch, const uint256& expectedMerkleRoot);

/**
 * Build SPV proof for a transaction
 * Returns false if transaction not found or not in an AXIS block
 */
bool GetAxisProof(const uint256& txid, int txHeight, int txIndex,
                  const std::vector<uint256>& merkleTree,
                  const CBlockHeader& blockHeader,
                  const std::vector<AxisHeader>& axisChain,
                  AxisSPVProof& proof);

/**
 * Walk hashPrevAxisBlock chain back to GENESIS and verify each link
 */
bool VerifyAxisChain(const std::vector<AxisHeader>& axisChain, const uint256& targetHash);

/**
 * Verify AXIS block PoW
 */
bool VerifyAxisPoW(const AxisHeader& header, const uint256& powLimit);

/**
 * Verify a complete SPV proof (merkle + PoW + skip-chain)
 */
bool VerifyAxisProof(const AxisSPVProof& proof, const uint256& powLimit);

} // namespace spv

#endif // BITCOIN_SPV_H