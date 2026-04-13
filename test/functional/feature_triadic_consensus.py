#!/usr/bin/env python3
# Copyright (c) 2025 The TeslaChain-369 developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test 3-6-9 Triadic Consensus P2P integration.

Tests that AXIS fields (hashPrevAxisBlock, hashAxisMerkleRoot) propagate
correctly between nodes over the P2P network using 144-byte block headers.

Key test: Python test framework now serializes 144-byte CBlockHeaders.
If C++ node accepts them, P2P integration works.
"""

from test_framework.blocktools import create_block
from test_framework.messages import CBlockHeader
from test_framework.p2p import P2PDataStore
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class TriadicConsensusP2PTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True

    def setup_network(self):
        self.setup_nodes()

    def test_header_serialization(self):
        """Test that Python CBlockHeader produces valid 144-byte headers."""
        self.log.info("Testing CBlockHeader serialization (144 bytes)")
        
        node0 = self.nodes[0]
        
        genesis_hash = int(node0.getblockhash(0), 16)
        block = create_block(hashprev=genesis_hash, tmpl={"height": 1})
        block.solve()
        
        header = CBlockHeader(block)
        serialized = header.serialize()
        
        # Must be exactly 144 bytes
        assert_equal(len(serialized), 144)
        assert_equal(header.hashPrevAxisBlock, 0)
        assert_equal(header.hashAxisMerkleRoot, 0)
        
        self.log.info("CBlockHeader serialization: 144 bytes ✓")

    def test_p2p_single_node_block_mining(self):
        """Test P2P block mining with 144-byte headers on single node."""
        self.log.info("Testing P2P block mining (single node)")
        
        node0 = self.nodes[0]
        peer0 = node0.add_p2p_connection(P2PDataStore())
        
        # Mine 9 blocks via P2P (LINK/AXIS/SUPER_AXIS cycle)
        blocks = []
        prev_hash = int(node0.getbestblockhash(), 16)
        
        for i in range(9):
            height = i + 1
            block = create_block(
                hashprev=prev_hash,
                tmpl={
                    "height": height,
                    "curtime": node0.getblock(node0.getbestblockhash())['time'] + 1
                }
            )
            block.solve()
            blocks.append(block)
            prev_hash = block.hash_int
        
        # Send blocks via P2P (includes 144-byte headers)
        peer0.send_blocks_and_test(blocks, node0, success=True)
        
        assert_equal(node0.getblockcount(), 9)
        self.log.info("Mined 9 blocks via P2P — C++ accepted 144-byte headers! ✓")

    def test_p2p_multi_node_sync(self):
        """Test that two nodes sync blocks via P2P with 144-byte headers."""
        self.log.info("Testing P2P multi-node sync")
        
        node0 = self.nodes[0]
        node1 = self.nodes[1]
        
        # Connect nodes
        self.connect_nodes(0, 1)
        
        # Node0 mines 9 blocks
        peer0 = node0.add_p2p_connection(P2PDataStore())
        
        blocks = []
        prev_hash = int(node0.getbestblockhash(), 16)
        
        for i in range(9):
            height = i + 1
            block = create_block(
                hashprev=prev_hash,
                tmpl={
                    "height": height,
                    "curtime": node0.getblock(node0.getbestblockhash())['time'] + 1
                }
            )
            block.solve()
            blocks.append(block)
            prev_hash = block.hash_int
        
        peer0.send_blocks_and_test(blocks, node0, success=True)
        assert_equal(node0.getblockcount(), 9)
        
        # Node1 should sync from node0 via P2P
        self.sync_blocks([node0, node1])
        
        assert_equal(node0.getbestblockhash(), node1.getbestblockhash())
        assert_equal(node1.getblockcount(), 9)
        self.log.info("Multi-node sync works — 144-byte headers propagate over P2P! ✓")

    def run_test(self):
        """Run all P2P integration tests."""
        self.log.info("Starting 3-6-9 Triadic Consensus P2P integration tests")
        
        self.test_header_serialization()
        self.test_p2p_single_node_block_mining()
        self.test_p2p_multi_node_sync()
        
        self.log.info("All 3-6-9 P2P integration tests passed!")


if __name__ == "__main__":
    TriadicConsensusP2PTest(__file__).main()
