// Copyright (c) 2024 TeslaChain contributors
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <spv.h>

#include <arith_uint256.h>
#include <hash.h>
#include <primitives/block.h>
#include <uint256.h>

#include <algorithm>

namespace spv {

void BuildMerkleBranch(const std::vector<uint256>& merkleTree, int txIndex, MerkleBranch& branch)
{
    branch.clear();
    
    if (merkleTree.empty() || txIndex < 0 || txIndex >= (int)merkleTree.size()) {
        return;
    }

    int pos = txIndex;
    int levelSize = (int)merkleTree.size();

    // Walk up the merkle tree, collecting sibling hashes
    while (levelSize > 1) {
        // Determine sibling position (left or right)
        int siblingPos = (pos % 2 == 0) ? pos + 1 : pos - 1;
        
        if (siblingPos < levelSize) {
            branch.hashes.push_back(merkleTree[siblingPos]);
            branch.right.push_back(siblingPos > pos);
        }
        
        // Move to next level
        pos = pos / 2;
        levelSize = (levelSize + 1) / 2;
    }
}

bool VerifyMerkleProof(const uint256& txid, const MerkleBranch& branch, const uint256& expectedMerkleRoot)
{
    if (branch.hashes.empty()) {
        // Single tx, root equals txid
        return txid == expectedMerkleRoot;
    }

    uint256 current = txid;
    
    for (size_t i = 0; i < branch.hashes.size(); i++) {
        if (branch.right[i]) {
            // Sibling is on the right: Hash(current, sibling)
            current = Hash(current, branch.hashes[i]);
        } else {
            // Sibling is on the left: Hash(sibling, current)
            current = Hash(branch.hashes[i], current);
        }
    }
    
    return current == expectedMerkleRoot;
}

bool GetAxisProof(const uint256& txid, int txHeight, int txIndex,
                  const std::vector<uint256>& merkleTree,
                  const CBlockHeader& blockHeader,
                  const std::vector<AxisHeader>& axisChain,
                  AxisSPVProof& proof)
{
    proof.clear();
    
    // Check if this is an AXIS block
    if (!IsAxisBlock(txHeight)) {
        return false;
    }
    
    // Build the target header
    proof.targetHeader = AxisHeader(txHeight, blockHeader.GetHash(),
                                     blockHeader.hashPrevAxisBlock,
                                     blockHeader.hashAxisMerkleRoot);
    
    // Transaction details
    proof.txid = txid;
    proof.txHeight = txHeight;
    proof.txIndex = txIndex;
    proof.txBlockHash = blockHeader.GetHash();
    
    // Build merkle branch
    BuildMerkleBranch(merkleTree, txIndex, proof.merkleBranch);
    
    // Copy AXIS chain
    proof.axisChain = axisChain;
    
    // PoW info
    proof.nBits = blockHeader.nBits;
    proof.blockHash = blockHeader.GetHash();
    
    return true;
}

bool VerifyAxisChain(const std::vector<AxisHeader>& axisChain, const uint256& targetHash)
{
    if (axisChain.empty()) {
        return false;
    }
    
    // First header must match target
    if (axisChain[0].hash != targetHash) {
        return false;
    }
    
    // Walk the chain back to GENESIS
    for (size_t i = 0; i < axisChain.size(); i++) {
        const AxisHeader& header = axisChain[i];
        
        if (header.nHeight > 0) {
            if (header.hashPrevAxis.IsNull()) {
                // All AXIS blocks except GENESIS must have hashPrevAxis set
                return false;
            }
            
            if (header.nHeight == 3) {
                // First AXIS block must point to GENESIS
                if (header.hashPrevAxis != AXIS_GENESIS_HASH) {
                    return false;
                }
            } else {
                // Subsequent AXIS blocks must point to previous AXIS in our chain
                bool foundPrev = false;
                for (size_t j = i + 1; j < axisChain.size(); j++) {
                    if (axisChain[j].hash == header.hashPrevAxis) {
                        // Verify hashAxisMerkleRoot computation
                        HashWriter ss;
                        ss << axisChain[j].hashAxisMerkleRoot;
                        ss << axisChain[j].hash;
                        uint256 expectedMerkle = ss.GetHash();
                        if (header.hashAxisMerkleRoot != expectedMerkle) {
                            return false;
                        }
                        foundPrev = true;
                        break;
                    }
                }
                if (!foundPrev) {
                    return false;
                }
            }
        }
    }
    
    // Verify last header is GENESIS (or points to it)
    const AxisHeader& lastHeader = axisChain.back();
    if (lastHeader.nHeight > 0 && lastHeader.hashPrevAxis != AXIS_GENESIS_HASH) {
        return false;
    }
    
    return true;
}

bool VerifyAxisPoW(const AxisHeader& header, const uint256& powLimit)
{
    // Check against target: hash must be less than or equal to target
    // PoW is valid if hash <= target
    arith_uint256 target = UintToArith256(powLimit);
    arith_uint256 hash = UintToArith256(header.hash);
    
    return hash <= target;
}

bool VerifyAxisProof(const AxisSPVProof& proof, const uint256& powLimit)
{
    // 1. Verify PoW on target header
    if (!VerifyAxisPoW(proof.targetHeader, powLimit)) {
        return false;
    }
    
    // 2. Verify merkle proof
    if (!VerifyMerkleProof(proof.txid, proof.merkleBranch, proof.targetHeader.hash)) {
        return false;
    }
    
    // 3. Verify AXIS skip-chain
    if (!VerifyAxisChain(proof.axisChain, proof.targetHeader.hash)) {
        return false;
    }
    
    return true;
}

} // namespace spv