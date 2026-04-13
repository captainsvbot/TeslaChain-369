#!/usr/bin/env python3
# Copyright (c) 2024 The TeslaChain-369 developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test TeslaChain 3-6-9 Triadic Consensus via RPC mining.

Tests that the C++ node correctly computes AXIS fields (hashPrevAxisBlock,
hashAxisMerkleRoot) when mining via RPC. Since CreateNewBlock() in miner.cpp
already auto-populates these fields, we mine via RPC and verify the results.

Block classification:
- LINK: height % 3 != 0 (1, 2, 4, 5, 7, 8...)
- AXIS: height % 3 == 0 and height % 9 != 0 (3, 6, 12, 15...)
- SUPER_AXIS: height % 9 == 0 (9, 18, 27...)
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


def classify_block(height):
    """Classify block type by height using 3-6-9 rules."""
    if height % 9 == 0:
        return "SUPER_AXIS"
    elif height % 3 == 0:
        return "AXIS"
    else:
        return "LINK"


def get_block_header_bytes(node, height):
    """Get raw bytes of block header at given height (first 144 bytes)."""
    block_hash = node.getblockhash(height)
    full_hex = node.getblock(block_hash, 0)
    return bytes.fromhex(full_hex[:288])  # First 144 bytes as hex


class TriadicConsensusTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.chain = "regtest"
        self.setup_clean_chain = True
        # Suppress DeriveTarget debug fprintf to stderr so stop_node's
        # wait_until_stopped(expected_stderr='') check does not fail.
        self.extra_args = [["-nodebuglogfile"]] * self.num_nodes

    def setup_network(self):
        self.setup_nodes()
        self.connect_nodes(0, 1)

    def test_header_size(self):
        """Test that block headers are 144 bytes (80 base + 64 AXIS)."""
        self.log.info("Testing block header size (144 bytes)")

        node = self.nodes[0]
        self.generate(node, 1)
        header_bytes = get_block_header_bytes(node, 1)

        assert len(header_bytes) == 144, f"Header should be 144 bytes, got {len(header_bytes)}"
        self.log.info("  Block header is 144 bytes")

    def test_first_axis_block_height_3(self):
        """Test that height 3 (first AXIS block) is correctly formed."""
        self.log.info("Testing first AXIS block (height 3)")

        node = self.nodes[0]
        self.generate(node, 3)

        genesis_hash = int(node.getblockhash(0), 16)
        header_bytes = get_block_header_bytes(node, 3)

        hash_prev_axis = int.from_bytes(header_bytes[80:112], 'little')
        hash_axis_merkle = int.from_bytes(header_bytes[112:144], 'little')

        assert_equal(hash_prev_axis, genesis_hash)
        assert_equal(hash_axis_merkle, genesis_hash)
        self.log.info("  Height 3: hashPrevAxisBlock=GENESIS, hashAxisMerkleRoot=GENESIS")

    def test_axis_fields_by_height(self):
        """Test AXIS field values are correct at each height."""
        self.log.info("Testing AXIS field values by height")

        node = self.nodes[0]
        self.generate(node, 12)

        for height in range(1, 13):
            block_type = classify_block(height)
            header_bytes = get_block_header_bytes(node, height)

            hash_prev_axis = int.from_bytes(header_bytes[80:112], 'little')
            hash_axis_merkle = int.from_bytes(header_bytes[112:144], 'little')

            if block_type == "LINK":
                assert hash_prev_axis == 0, f"Height {height} ({block_type}): hashPrevAxisBlock should be 0"
                assert hash_axis_merkle == 0, f"Height {height} ({block_type}): hashAxisMerkleRoot should be 0"
                self.log.info(f"  Height {height} ({block_type}): hashPrevAxisBlock=0, hashAxisMerkleRoot=0")
            else:
                assert hash_prev_axis != 0, f"Height {height} ({block_type}): hashPrevAxisBlock should be non-zero"
                assert hash_axis_merkle != 0, f"Height {height} ({block_type}): hashAxisMerkleRoot should be non-zero"
                self.log.info(f"  Height {height} ({block_type}): non-zero AXIS fields")

    def test_multi_node_sync(self):
        """Test that two nodes stay in sync via P2P."""
        self.log.info("Testing multi-node sync")

        node0 = self.nodes[0]
        node1 = self.nodes[1]

        self.generate(node0, 10)
        self.sync_blocks([node0, node1])

        assert_equal(node0.getbestblockhash(), node1.getbestblockhash())
        assert_equal(node0.getblockcount(), node1.getblockcount())
        self.log.info(f"  Both nodes synced at height {node0.getblockcount()}")

    def run_test(self):
        """Run all tests."""
        self.log.info("TeslaChain 3-6-9 Triadic Consensus Tests")
        self.log.info("=" * 50)

        self.test_header_size()
        self.test_first_axis_block_height_3()
        self.test_axis_fields_by_height()
        self.test_multi_node_sync()

        self.log.info("=" * 50)
        self.log.info("All tests passed!")


if __name__ == "__main__":
    TriadicConsensusTest(__file__).main()
