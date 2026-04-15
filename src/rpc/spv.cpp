// Copyright (c) 2024 TeslaChain contributors
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <rpc/spv.h>

#include <chain.h>
#include <consensus/merkle.h>
#include <core_io.h>
#include <kernel/chainparams.h>
#include <node/blockstorage.h>
#include <node/context.h>
#include <primitives/block.h>
#include <rpc/server.h>
#include <rpc/server_util.h>
#include <rpc/util.h>
#include <spv.h>
#include <txdb.h>
#include <univalue.h>
#include <validation.h>
#include <validationinterface.h>

#include <string>
#include <vector>

using node::NodeContext;

namespace spv {

static RPCMethod getaxisproof()
{
    return RPCMethod{
        "getaxisproof",
        "Returns an SPV proof for a transaction in an AXIS block.\n"
        "\nThis constructs a proof that allows lightweight clients to verify "
        "that a transaction was included in an AXIS block without downloading the full chain.\n",
        {
            {"txid", RPCArg::Type::STR, RPCArg::Optional::NO, "The transaction ID to prove"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR_HEX, "txid", "The transaction ID"},
                {RPCResult::Type::NUM, "height", "The block height containing the transaction"},
                {RPCResult::Type::STR_HEX, "blockhash", "The hash of the block containing the transaction"},
                {RPCResult::Type::NUM, "txindex", "The transaction index in the block"},
                {RPCResult::Type::OBJ, "targetheader", "The AXIS block header", {
                    {RPCResult::Type::NUM, "height", "Block height"},
                    {RPCResult::Type::STR_HEX, "hash", "Block hash"},
                    {RPCResult::Type::STR_HEX, "hashprevaxis", "Previous AXIS block hash"},
                    {RPCResult::Type::STR_HEX, "hashaxismerkleroot", "Cumulative AXIS merkle root"},
                }},
                {RPCResult::Type::ARR, "merklebranch", "Merkle proof branch", {
                    {RPCResult::Type::OBJ, "", "", {
                        {RPCResult::Type::STR_HEX, "hash", "Merkle tree node hash"},
                        {RPCResult::Type::BOOL, "right", "True if sibling is to the right"},
                    }},
                }},
                {RPCResult::Type::ARR, "axischain", "AXIS skip-chain headers back to GENESIS", {
                    {RPCResult::Type::OBJ, "", "", {
                        {RPCResult::Type::NUM, "height", "AXIS block height"},
                        {RPCResult::Type::STR_HEX, "hash", "AXIS block hash"},
                        {RPCResult::Type::STR_HEX, "hashprevaxis", "Previous AXIS block hash"},
                        {RPCResult::Type::STR_HEX, "hashaxismerkleroot", "Cumulative AXIS merkle root"},
                    }},
                }},
                {RPCResult::Type::STR_HEX, "nbits", "Compact difficulty target"},
            }
        },
        RPCExamples{
            HelpExampleCli("getaxisproof", "\"abc123...\"")
            + HelpExampleRpc("getaxisproof", "\"abc123...\"")
        },
        [](const RPCMethod& self, const JSONRPCRequest& request) -> UniValue
        {
            NodeContext& node = EnsureAnyNodeContext(request.context);
            ChainstateManager& chainman = EnsureChainman(node);

            uint256 txid = ParseHashV(request.params[0], "txid");

            // Search for transaction in blockchain
            std::pair<uint256, int> found = std::make_pair(uint256::ZERO, -1);
            std::vector<uint256> merkleTree;
            CBlockHeader blockHeader;
            int blockHeight = -1;
            int txIndex = -1;

            {
                LOCK(cs_main);
                CChain& chain = chainman.ActiveChain();

                // Search blocks from tip backwards
                for (int height = chain.Height(); height >= 0; height--) {
                    const CBlockIndex* pindex = chain[height];
                    if (!pindex) continue;

                    // Only look in AXIS blocks (every 3 blocks)
                    if (!spv::IsAxisBlock(height)) continue;

                    // Read block
                    CBlock block;
                    if (!chainman.m_blockman.ReadBlock(block, *pindex)) continue;

                    // Search for transaction and get merkle path
                    for (size_t i = 0; i < block.vtx.size(); i++) {
                        if (block.vtx[i]->GetHash().ToUint256() == txid) {
                            // Get merkle path for this transaction
                            merkleTree = TransactionMerklePath(block, static_cast<uint32_t>(i));
                            blockHeader = static_cast<CBlockHeader>(block);
                            blockHeight = height;
                            txIndex = static_cast<int>(i);

                            // Build AXIS chain
                            std::vector<AxisHeader> axisChain;
                            for (int h = height; h > 0; h -= 3) {
                                const CBlockIndex* idx = chain[h];
                                if (!idx) break;
                                AxisHeader ah;
                                ah.nHeight = idx->nHeight;
                                ah.hash = idx->GetBlockHash();
                                ah.hashPrevAxis = idx->hashPrevAxisBlock;
                                ah.hashAxisMerkleRoot = idx->hashAxisMerkleRoot;
                                axisChain.push_back(ah);
                                if (h == 0) break;
                            }

                            found = std::make_pair(pindex->GetBlockHash(), height);
                            break;
                        }
                    }
                    if (found.second != -1) break;
                }
            }

            if (found.second == -1) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not found in any AXIS block");
            }

            // Build the proof
            AxisSPVProof proof;
            if (!spv::GetAxisProof(txid, blockHeight, txIndex, merkleTree, blockHeader, {}, proof)) {
                throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to construct SPV proof");
            }

            // Serialize to UniValue
            UniValue result(UniValue::VOBJ);
            result.pushKV("txid", txid.GetHex());
            result.pushKV("height", blockHeight);
            result.pushKV("blockhash", blockHeader.GetHash().GetHex());
            result.pushKV("txindex", txIndex);

            // Target header
            UniValue targetHeader(UniValue::VOBJ);
            targetHeader.pushKV("height", proof.targetHeader.nHeight);
            targetHeader.pushKV("hash", proof.targetHeader.hash.GetHex());
            targetHeader.pushKV("hashprevaxis", proof.targetHeader.hashPrevAxis.GetHex());
            targetHeader.pushKV("hashaxismerkleroot", proof.targetHeader.hashAxisMerkleRoot.GetHex());
            result.pushKV("targetheader", targetHeader);

            // Merkle branch
            UniValue merkleBranch(UniValue::VARR);
            for (size_t i = 0; i < proof.merkleBranch.hashes.size(); i++) {
                UniValue branchNode(UniValue::VOBJ);
                branchNode.pushKV("hash", proof.merkleBranch.hashes[i].GetHex());
                branchNode.pushKV("right", (bool)proof.merkleBranch.right[i]);
                merkleBranch.push_back(std::move(branchNode));
            }
            result.pushKV("merklebranch", merkleBranch);

            // AXIS chain
            UniValue axisChain(UniValue::VARR);
            for (const auto& ah : proof.axisChain) {
                UniValue axisHeader(UniValue::VOBJ);
                axisHeader.pushKV("height", ah.nHeight);
                axisHeader.pushKV("hash", ah.hash.GetHex());
                axisHeader.pushKV("hashprevaxis", ah.hashPrevAxis.GetHex());
                axisHeader.pushKV("hashaxismerkleroot", ah.hashAxisMerkleRoot.GetHex());
                axisChain.push_back(std::move(axisHeader));
            }
            result.pushKV("axischain", axisChain);

            result.pushKV("nbits", strprintf("%08x", proof.nBits));

            return result;
        },
    };
}

static RPCMethod getaxisheaders()
{
    return RPCMethod{
        "getaxisheaders",
        "Returns AXIS block headers for building the skip-chain.\n"
        "\nThis allows lightweight clients to download only AXIS headers "
        "for chain verification, at ~48 bytes per block (vs 80 for Bitcoin SPV).\n",
        {
            {"startheight", RPCArg::Type::NUM, RPCArg::Optional::NO, "The starting AXIS block height (must be divisible by 3)"},
            {"count", RPCArg::Type::NUM, RPCArg::Optional::NO, "Number of AXIS headers to return (max 2000)"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::NUM, "startheight", "The first AXIS block height returned"},
                {RPCResult::Type::NUM, "count", "Number of AXIS headers returned"},
                {RPCResult::Type::NUM, "chainheight", "Current chain height"},
                {RPCResult::Type::NUM, "actualcount", "Actual number of headers returned (may be less than count if chain ends)"},
                {RPCResult::Type::ARR, "headers", "AXIS block headers", {
                    {RPCResult::Type::OBJ, "", "", {
                        {RPCResult::Type::NUM, "height", "Block height"},
                        {RPCResult::Type::STR_HEX, "hash", "Block hash (double-SHA256 of 144-byte header)"},
                        {RPCResult::Type::STR_HEX, "hashprevaxis", "Previous AXIS block hash"},
                        {RPCResult::Type::STR_HEX, "hashaxismerkleroot", "Cumulative AXIS merkle root"},
                        {RPCResult::Type::STR_HEX, "nbits", "Compact difficulty target"},
                        {RPCResult::Type::NUM_TIME, "time", "Block timestamp"},
                    }},
                }},
            }
        },
        RPCExamples{
            HelpExampleCli("getaxisheaders", "3 100")
            + HelpExampleRpc("getaxisheaders", "3, 100")
        },
        [](const RPCMethod& self, const JSONRPCRequest& request) -> UniValue
        {
            NodeContext& node = EnsureAnyNodeContext(request.context);
            ChainstateManager& chainman = EnsureChainman(node);

            int startHeight = request.params[0].getInt<int>();
            int count = request.params[1].getInt<int>();

            if (startHeight < 0 || startHeight % 3 != 0) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "startheight must be a non-negative multiple of 3");
            }
            if (count <= 0 || count > 2000) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "count must be between 1 and 2000");
            }

            UniValue result(UniValue::VOBJ);
            result.pushKV("startheight", startHeight);
            result.pushKV("count", count);

            int chainHeight = 0;
            std::vector<AxisHeader> headers;

            {
                LOCK(cs_main);
                CChain& chain = chainman.ActiveChain();
                chainHeight = chain.Height();

                for (int i = 0; i < count; i++) {
                    int height = startHeight + (i * 3);
                    if (height > chainHeight) break;

                    const CBlockIndex* pindex = chain[height];
                    if (!pindex) break;

                    AxisHeader ah;
                    ah.nHeight = pindex->nHeight;
                    ah.hash = pindex->GetBlockHash();
                    ah.hashPrevAxis = pindex->hashPrevAxisBlock;
                    ah.hashAxisMerkleRoot = pindex->hashAxisMerkleRoot;
                    headers.push_back(ah);
                }
            }

            result.pushKV("chainheight", chainHeight);

            UniValue headersArr(UniValue::VARR);
            for (const auto& h : headers) {
                UniValue header(UniValue::VOBJ);
                header.pushKV("height", h.nHeight);
                header.pushKV("hash", h.hash.GetHex());
                header.pushKV("hashprevaxis", h.hashPrevAxis.GetHex());
                header.pushKV("hashaxismerkleroot", h.hashAxisMerkleRoot.GetHex());
                // Get nBits from the actual block
                {
                    LOCK(cs_main);
                    CChain& chain = chainman.ActiveChain();
                    const CBlockIndex* pindex = chain[h.nHeight];
                    if (pindex) {
                        header.pushKV("nbits", strprintf("%08x", pindex->nBits));
                        header.pushKV("time", pindex->nTime);
                    }
                }
                headersArr.push_back(std::move(header));
            }
            result.pushKV("headers", headersArr);
            result.pushKV("actualcount", (int)headers.size());

            return result;
        },
    };
}

static RPCMethod verifyaxisproof()
{
    return RPCMethod{
        "verifyaxisproof",
        "Verifies an SPV proof for an AXIS block transaction.\n"
        "\nThis validates the merkle proof, PoW, and AXIS skip-chain.\n",
        {
            {"proof", RPCArg::Type::OBJ, RPCArg::Optional::NO, "The SPV proof to verify",
                {
                    {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction ID"},
                    {"height", RPCArg::Type::NUM, RPCArg::Optional::NO, "The block height containing the transaction"},
                    {"blockhash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The hash of the block containing the transaction"},
                    {"txindex", RPCArg::Type::NUM, RPCArg::Optional::NO, "The transaction index in the block"},
                    {"targetheader", RPCArg::Type::OBJ, RPCArg::Optional::NO, "The AXIS block header", {
                        {"height", RPCArg::Type::NUM, RPCArg::Optional::NO, "Block height"},
                        {"hash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Block hash"},
                        {"hashprevaxis", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Previous AXIS block hash"},
                        {"hashaxismerkleroot", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Cumulative AXIS merkle root"},
                    }},
                    {"merklebranch", RPCArg::Type::ARR, RPCArg::Optional::NO, "Merkle proof branch", {
                        {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "", {
                            {"hash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Merkle tree node hash"},
                            {"right", RPCArg::Type::BOOL, RPCArg::Optional::NO, "True if sibling is to the right"},
                        }},
                    }},
                    {"axischain", RPCArg::Type::ARR, RPCArg::Optional::NO, "AXIS skip-chain headers back to GENESIS", {
                        {"", RPCArg::Type::OBJ, RPCArg::Optional::OMITTED, "", {
                            {"height", RPCArg::Type::NUM, RPCArg::Optional::NO, "AXIS block height"},
                            {"hash", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "AXIS block hash"},
                            {"hashprevaxis", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Previous AXIS block hash"},
                            {"hashaxismerkleroot", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Cumulative AXIS merkle root"},
                        }},
                    }},
                    {"nbits", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "Compact difficulty target"},
                }},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::BOOL, "valid", "Whether the proof is valid"},
                {RPCResult::Type::STR, "error", "Error message if invalid"},
                {RPCResult::Type::OBJ, "checks", "Individual check results", {
                    {RPCResult::Type::BOOL, "merkle_proof", "Merkle proof verification"},
                    {RPCResult::Type::BOOL, "pow_verification", "Proof of work verification"},
                    {RPCResult::Type::BOOL, "axis_chain", "AXIS skip-chain verification"},
                }},
            }
        },
        RPCExamples{
            HelpExampleCli("verifyaxisproof", "{\"txid\": \"...\", \"height\": 6, ...}")
            + HelpExampleRpc("verifyaxisproof", "{\"txid\": \"...\", \"height\": 6, ...}")
        },
        [](const RPCMethod& self, const JSONRPCRequest& request) -> UniValue
        {
            NodeContext& node = EnsureAnyNodeContext(request.context);
            ChainstateManager& chainman = EnsureChainman(node);

            const UniValue& proofObj = request.params[0];

            AxisSPVProof proof;

            // Parse target header
            proof.targetHeader.nHeight = proofObj["targetheader"]["height"].getInt<int>();
            proof.targetHeader.hash = ParseHashV(proofObj["targetheader"]["hash"], "targetheader.hash");
            proof.targetHeader.hashPrevAxis = ParseHashV(proofObj["targetheader"]["hashprevaxis"], "targetheader.hashprevaxis");
            proof.targetHeader.hashAxisMerkleRoot = ParseHashV(proofObj["targetheader"]["hashaxismerkleroot"], "targetheader.hashaxismerkleroot");

            // Parse transaction
            proof.txid = ParseHashV(proofObj["txid"], "txid");
            proof.txHeight = proofObj["height"].getInt<int>();
            proof.txIndex = proofObj["txindex"].getInt<int>();
            proof.txBlockHash = ParseHashV(proofObj["blockhash"], "blockhash");

            // Parse merkle branch
            const UniValue& merkleArr = proofObj["merklebranch"];
            for (size_t i = 0; i < merkleArr.size(); i++) {
                const UniValue& node = merkleArr[i];
                proof.merkleBranch.hashes.push_back(ParseHashV(node["hash"], "merklebranch.hash"));
                proof.merkleBranch.right.push_back(node["right"].get_bool());
            }

            // Parse axis chain
            const UniValue& axisArr = proofObj["axischain"];
            for (size_t i = 0; i < axisArr.size(); i++) {
                const UniValue& h = axisArr[i];
                AxisHeader ah;
                ah.nHeight = h["height"].getInt<int>();
                ah.hash = ParseHashV(h["hash"], "axischain.hash");
                ah.hashPrevAxis = ParseHashV(h["hashprevaxis"], "axischain.hashprevaxis");
                ah.hashAxisMerkleRoot = ParseHashV(h["hashaxismerkleroot"], "axischain.hashaxismerkleroot");
                proof.axisChain.push_back(ah);
            }

            // Parse PoW info
            proof.nBits = proofObj["nbits"].getInt<uint32_t>();
            proof.blockHash = proof.targetHeader.hash;

            // Get pow limit from consensus
            uint256 powLimit = chainman.GetConsensus().powLimit;

            // Verify
            bool merkleOk = spv::VerifyMerkleProof(proof.txid, proof.merkleBranch, proof.targetHeader.hash);
            bool powOk = spv::VerifyAxisPoW(proof.targetHeader, powLimit);
            bool axisChainOk = spv::VerifyAxisChain(proof.axisChain, proof.targetHeader.hash);
            bool allValid = merkleOk && powOk && axisChainOk;

            UniValue result(UniValue::VOBJ);
            result.pushKV("valid", allValid);

            if (!allValid) {
                std::string error;
                if (!merkleOk) error = "merkle_proof_failed";
                else if (!powOk) error = "pow_verification_failed";
                else if (!axisChainOk) error = "axis_chain_invalid";
                result.pushKV("error", error);
            }

            UniValue checks(UniValue::VOBJ);
            checks.pushKV("merkle_proof", merkleOk);
            checks.pushKV("pow_verification", powOk);
            checks.pushKV("axis_chain", axisChainOk);
            result.pushKV("checks", checks);

            return result;
        },
    };
}

void RegisterSPVRPCCommands(CRPCTable& t)
{
    static const CRPCCommand commands[]{
        {"blockchain", &getaxisproof},
        {"blockchain", &getaxisheaders},
        {"blockchain", &verifyaxisproof},
    };
    for (const auto& c : commands) {
        t.appendCommand(c.name, &c);
    }
}

} // namespace spv
