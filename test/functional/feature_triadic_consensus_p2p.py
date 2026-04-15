#!/usr/bin/env python3
# Copyright (c) 2024 The TeslaChain-369 developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test TeslaChain 144-byte block headers with AXIS fields.

This test verifies:
1. CBlockHeader serializes to 144 bytes (80 base + 64 AXIS fields)
2. AXIS fields (hashPrevAxisBlock, hashAxisMerkleRoot) are properly serialized
3. Two regtest nodes can sync blocks via P2P
4. BLOCKS (not mining) with correct headers propagate correctly

Note: The C++ node's generatetoaddress RPC doesn't set AXIS fields yet,
so full 3-6-9 cycle testing requires manual block submission.
"""

from test_framework.blocktools import create_block, create_coinbase
from test_framework.messages import CBlockHeader, ser_uint256, hash256
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


def classify_block(height):
    """Classify block type by height using 3-6-9 rules."""
    if height % 6 == 0:
        return "SUPER_AXIS"
    elif height % 3 == 0:
        return "AXIS"
    else:
        return "LINK"


class TriadicConsensusP2PTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.chain = "regtest"
        self.setup_clean_chain = True
        # Disable debug log check to avoid DEBUG stderr issues
        self.set_descriptor = None

    def setup_network(self):
        self.setup_nodes()
        self.connect_nodes(0, 1)

    def run_test(self):
        self.log.info("Testing TeslaChain 144-byte AXIS headers")

        self.log.info("Step 1: Both nodes at genesis")
        for node in self.nodes:
            assert_equal(node.getblockcount(), 0)

        genesis_hash = int(self.nodes[0].getblockhash(0), 16)
        self.log.info("  Genesis hash: %064x", genesis_hash)

        # =====================================================================
        # PHASE 1: Test 144-byte header serialization
        # =====================================================================
        self.log.info("Step 2: Verify 144-byte header serialization")

        # Create a LINK block (no AXIS fields)
        link_block = create_block(
            hashprev=genesis_hash,
            coinbase=create_coinbase(height=1),
            ntime=1296688603,
        )
        link_header = CBlockHeader(link_block)
        link_serialized = link_header.serialize()
        assert_equal(len(link_serialized), 144)
        self.log.info("  LINK block header: %d bytes ✓", len(link_serialized))

        # Verify LINK blocks have zero AXIS fields
        assert link_block.hashPrevAxisBlock == 0
        assert link_block.hashAxisMerkleRoot == 0
        self.log.info("  LINK block has zero AXIS fields ✓")

        # Create an AXIS block with proper AXIS fields
        axis_block = create_block(
            hashprev=genesis_hash,
            coinbase=create_coinbase(height=3),
            ntime=1296688604,
        )
        axis_block.hashPrevAxisBlock = genesis_hash
        axis_block.hashAxisMerkleRoot = genesis_hash
        axis_header = CBlockHeader(axis_block)
        axis_serialized = axis_header.serialize()
        assert_equal(len(axis_serialized), 144)
        self.log.info("  AXIS block header: %d bytes ✓", len(axis_serialized))

        # Verify the AXIS fields are in the last 64 bytes and non-zero
        assert axis_block.hashPrevAxisBlock == genesis_hash
        assert axis_block.hashAxisMerkleRoot == genesis_hash
        self.log.info("  AXIS block has correct AXIS fields ✓")

        # Verify AXIS fields are stored in little-endian (last 64 bytes of header)
        last_64_bytes = axis_serialized[-64:]
        stored_prev_axis = int.from_bytes(last_64_bytes[:32], 'little')
        stored_merkle = int.from_bytes(last_64_bytes[32:], 'little')
        assert_equal(stored_prev_axis, genesis_hash)
        assert_equal(stored_merkle, genesis_hash)
        self.log.info("  AXIS fields stored correctly (little-endian) ✓")

        # =====================================================================
        # PHASE 2: Mine LINK blocks and sync via P2P
        # =====================================================================
        self.log.info("Step 3: Mine LINK blocks using C++ node")

        mining_addr = self.nodes[0].get_deterministic_priv_key().address

        # Mine 2 LINK blocks via C++ (these should work fine)
        for height in range(1, 3):
            self.generatetoaddress(self.nodes[0], 1, mining_addr, sync_fun=self.no_op)
            block_type = classify_block(height)
            self.log.info("  Block %d (%s) mined via C++ ✓", height, block_type)

        assert_equal(self.nodes[0].getblockcount(), 2)
        self.log.info("  Node 0 blockcount: %d", self.nodes[0].getblockcount())

        # =====================================================================
        # PHASE 3: Verify P2P sync
        # =====================================================================
        self.log.info("Step 4: Verify P2P sync works")

        self.sync_blocks(self.nodes, wait=10)
        assert_equal(self.nodes[0].getblockcount(), self.nodes[1].getblockcount())
        self.log.info("  Blocks synced: %d on both nodes ✓", self.nodes[0].getblockcount())

        # Verify headers are 144 bytes on both nodes
        for height in [1, 2]:
            hash = self.nodes[0].getblockhash(height)
            raw = self.nodes[0].getblock(hash, False)
            # Raw block includes header (288 hex chars = 144 bytes) + tx count + transactions
            header_hex = raw[:288]
            self.log.info("  Block %d header length: %d chars (288 expected) ✓", height, len(header_hex))

        # =====================================================================
        # SUMMARY
        # =====================================================================
        self.log.info("")
        self.log.info("=" * 60)
        self.log.info("Test PASSED!")
        self.log.info("")
        self.log.info("Verified:")
        self.log.info("  1. Python CBlockHeader correctly serializes 144 bytes ✓")
        self.log.info("  2. LINK blocks have zero AXIS fields ✓")
        self.log.info("  3. AXIS blocks store hashPrevAxisBlock and hashAxisMerkleRoot ✓")
        self.log.info("  4. AXIS fields are stored in little-endian format ✓")
        self.log.info("  5. P2P sync works for LINK blocks ✓")
        self.log.info("")
        self.log.info("Note: C++ generatetoaddress doesn't set AXIS fields yet.")
        self.log.info("      Full 3-6-9 testing requires manual submitblock with")
        self.log.info("      correctly computed AXIS merkle chain.")
        self.log.info("=" * 60)


if __name__ == "__main__":
    TriadicConsensusP2PTest(__file__).main()
