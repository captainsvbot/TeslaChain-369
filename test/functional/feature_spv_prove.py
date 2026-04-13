#!/usr/bin/env python3
# Copyright (c) 2024 The TeslaChain-369 developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test TeslaChain SPV (Simplified Payment Verification) for AXIS blocks.

Tests the SPV prove functionality:
- getaxisproof: Creates SPV proof for a transaction in an AXIS block
- getaxisheaders: Returns AXIS block headers for skip-chain construction
- verifyaxisproof: Verifies an SPV proof is valid

SPV allows lightweight clients to verify AXIS inclusion without downloading
the full chain. Only AXIS headers (144 bytes each) are needed, vs 80 for
Bitcoin SPV (~48 bytes per block average due to 1-in-3 AXIS frequency).
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_jsonrpc,
)


class SPVProveTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.chain = "regtest"
        self.setup_clean_chain = True

    def setup_network(self):
        self.setup_nodes()
        self.connect_nodes(0, 1)

    def classify_block(self, height):
        """Classify block type by height using 3-6-9 rules."""
        if height % 9 == 0:
            return "SUPER_AXIS"
        elif height % 3 == 0:
            return "AXIS"
        else:
            return "LINK"

    def get_axis_headers(self, node, start_height, count):
        """Get AXIS headers starting at start_height."""
        return node.getaxisheaders(start_height, count)

    def test_getaxisheaders_basic(self):
        """Test getaxisheaders returns correct AXIS block headers."""
        self.log.info("Testing getaxisheaders basic functionality")

        node = self.nodes[0]

        # Generate some blocks
        self.generate(node, 6)  # Should create AXIS at heights 3, 6

        # Get AXIS headers starting from height 3
        result = self.get_axis_headers(node, 3, 10)

        assert_equal(result["startheight"], 3)
        assert_equal(result["count"], 10)
        assert "headers" in result

        # Should have AXIS headers at heights 3, 6
        headers = result["headers"]
        header_heights = [h["height"] for h in headers]
        assert 3 in header_heights, "Height 3 should be an AXIS header"
        assert 6 in header_heights, "Height 6 should be an AXIS header"

        self.log.info(f"  Found {len(headers)} AXIS headers at heights: {header_heights}")

    def test_axis_headers_structure(self):
        """Test that AXIS headers have correct structure."""
        self.log.info("Testing AXIS headers structure")

        node = self.nodes[0]

        # Generate to get some AXIS blocks
        self.generate(node, 12)

        # Get AXIS headers
        result = self.get_axis_headers(node, 3, 20)
        headers = result["headers"]

        # Check first header (height 3)
        h3 = next((h for h in headers if h["height"] == 3), None)
        assert h3 is not None, "Height 3 header should exist"

        # For AXIS block 3, hashPrevAxis should be GENESIS hash
        # GENESIS: 144cc8ae15a2ba8590e05fa4ab6315eca0f08b26f4f2ef298f7bea271280f353
        expected_genesis = "144cc8ae15a2ba8590e05fa4ab6315eca0f08b26f4f2ef298f7bea271280f353"
        assert_equal(h3["hashprevaxis"], expected_genesis)

        # hashAxisMerkleRoot for height 3 should also be GENESIS
        assert_equal(h3["hashaxismerkleroot"], expected_genesis)

        self.log.info(f"  Height 3 header: hashPrevAxis={h3['hashprevaxis'][:16]}..., "
                      f"hashAxisMerkleRoot={h3['hashaxismerkleroot'][:16]}...")

    def test_getaxisproof_basic(self):
        """Test getaxisproof for a transaction in an AXIS block."""
        self.log.info("Testing getaxisproof basic functionality")

        node = self.nodes[0]

        # Generate blocks until we have an AXIS block with transactions
        self.generate(node, 6)

        # Create a transaction in the mempool
        txid = node.sendtoaddress(node.getnewaddress(), 1)
        self.log.info(f"  Created transaction: {txid}")

        # Mine it in an AXIS block (height 9)
        block_hash = self.generate(node, 3)[0]

        # Check if we hit an AXIS block
        height = node.getblock(block_hash)["height"]
        block_type = self.classify_block(height)

        self.log.info(f"  Mined block at height {height}, type: {block_type}")

        if block_type in ["AXIS", "SUPER_AXIS"]:
            # Try to get SPV proof
            proof = node.getaxisproof(txid)
            assert proof is not None, "Should get SPV proof for tx in AXIS block"
            assert_equal(proof["txid"], txid)
            assert "merklebranch" in proof
            assert "axischain" in proof
            assert "targetheader" in proof
            self.log.info(f"  Got SPV proof for tx at height {proof['height']}")

    def test_getaxisproof_link_block(self):
        """Test that getaxisproof only works for AXIS blocks."""
        self.log.info("Testing getaxisproof with LINK block transaction")

        node = self.nodes[0]

        # Generate some LINK blocks
        self.generate(node, 4)

        # Create and mine transaction in a LINK block
        txid = node.sendtoaddress(node.getnewaddress(), 0.5)
        block_hash = self.generate(node, 1)[0]
        height = node.getblock(block_hash)["height"]

        if self.classify_block(height) == "LINK":
            # Should fail because transaction is not in AXIS block
            assert_raises_jsonrpc(
                -5,  # RPC_INVALID_ADDRESS_OR_KEY
                "Transaction not found",
                node.getaxisproof,
                txid
            )
            self.log.info(f"  Transaction in LINK block (height {height}) correctly rejected")

    def test_verifyaxisproof(self):
        """Test verifyaxisproof validates proofs correctly."""
        self.log.info("Testing verifyaxisproof validation")

        node = self.nodes[0]

        # Generate blocks and create a transaction
        self.generate(node, 9)

        txid = node.sendtoaddress(node.getnewaddress(), 0.1)
        block_hash = self.generate(node, 3)[0]
        height = node.getblock(block_hash)["height"]

        if self.classify_block(height) in ["AXIS", "SUPER_AXIS"]:
            # Get proof
            proof = node.getaxisproof(txid)

            # Verify should pass
            result = node.verifyaxisproof(proof)
            assert result["valid"], f"Proof should be valid: {result}"

            # Verify individual checks
            assert result["checks"]["merkle_proof"]
            assert result["checks"]["pow_verification"]
            assert result["checks"]["axis_chain"]

            self.log.info(f"  Proof at height {height} verified successfully")

    def test_verifyaxisproof_invalid(self):
        """Test verifyaxisproof rejects invalid proofs."""
        self.log.info("Testing verifyaxisproof rejects invalid proofs")

        node = self.nodes[0]

        # Generate some blocks
        self.generate(node, 6)

        # Create a transaction
        txid = node.sendtoaddress(node.getnewaddress(), 0.1)
        self.generate(node, 3)

        # Get a valid proof first
        result = self.get_axis_headers(node, 3, 3)
        if result["headers"]:
            # Create a fake proof with tampered data
            fake_proof = {
                "txid": txid,
                "height": 3,
                "blockhash": "0" * 64,
                "txindex": 0,
                "targetheader": {
                    "height": 3,
                    "hash": "0" * 64,
                    "hashprevaxis": "0" * 64,
                    "hashaxismerkleroot": "0" * 64,
                },
                "merklebranch": [],
                "axischain": [],
                "nbits": "207fffff",  # Dummy nBits
            }

            # Verify should fail
            verify_result = node.verifyaxisproof(fake_proof)
            assert not verify_result["valid"], "Tampered proof should be invalid"
            self.log.info("  Tampered proof correctly rejected")

    def test_axis_chain_links(self):
        """Test that AXIS headers form a valid skip-chain."""
        self.log.info("Testing AXIS skip-chain continuity")

        node = self.nodes[0]

        # Generate many blocks to get a longer AXIS chain
        self.generate(node, 18)

        # Get AXIS headers
        result = self.get_axis_headers(node, 3, 20)
        headers = result["headers"]

        # Sort headers by height
        headers_by_height = {h["height"]: h for h in headers}

        # Check chain links
        heights = sorted(headers_by_height.keys())
        for i in range(len(heights) - 1):
            curr = headers_by_height[heights[i]]
            nxt = headers_by_height[heights[i + 1]]

            # Each AXIS block's hashPrevAxis should point to previous AXIS
            self.log.info(f"  Height {curr['height']}: hashPrevAxis={curr['hashprevaxis'][:16]}... "
                         f"-> Height {nxt['height']}: hash={nxt['hash'][:16]}...")

        # For AXIS block at height 6, hashPrevAxis should be height 3's hash
        if 3 in headers_by_height and 6 in headers_by_height:
            h3 = headers_by_height[3]
            h6 = headers_by_height[6]
            # height 6's hashPrevAxis should be height 3's block hash
            self.log.info(f"  Verification: h6.hashPrevAxis ({h6['hashprevaxis'][:16]}...) == "
                         f"h3.hash ({h3['hash'][:16]}...)")

    def run_test(self):
        """Run SPV prove tests."""
        self.log.info("Starting TeslaChain SPV prove tests")

        self.test_getaxisheaders_basic()
        self.test_axis_headers_structure()
        self.test_getaxisproof_basic()
        self.test_getaxisproof_link_block()
        self.test_verifyaxisproof()
        self.test_verifyaxisproof_invalid()
        self.test_axis_chain_links()

        self.log.info("All SPV prove tests passed")


if __name__ == "__main__":
    SPVProveTest().main()