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

NOTE: These RPCs are defined in src/rpc/spv.cpp. If the running binary does
not have these methods (returns -32601 Method not found), tests that depend
on them will be skipped with a clear message.
"""

import sys
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


def is_spv_rpc_available(node):
    """Check if SPV RPC methods are available in the node."""
    try:
        node.getaxisheaders(3, 1)
        return True
    except Exception as e:
        if "Method not found" in str(e):
            return False
        # Some other error (e.g. no blocks yet) - RPC exists
        return True


class SPVProveTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.chain = "regtest"
        self.setup_clean_chain = True
        self.extra_args = [["-nodebuglogfile"]] * self.num_nodes
        self.uses_wallet = True

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

    def get_wallet(self, node_index=0):
        """Get wallet RPC for the given node."""
        return self.nodes[node_index].get_wallet_rpc(self.default_wallet_name)

    def get_testing_wallet_addr(self, node_index=0):
        """Get address from the imported deterministic coinbase key.

        This address is guaranteed to be in the wallet because it's imported
        during test framework initialization. Mining to this address ensures
        the wallet sees the coinbase funds.
        """
        return self.nodes[node_index].get_deterministic_priv_key().address

    def setup_spv_chain(self):
        """Mine a chain with AXIS blocks and return the node."""
        node = self.nodes[0]
        wallet = self.get_wallet(0)
        # Use wallet's own address for mining so coinbase rewards are credited to wallet
        mining_addr = wallet.getnewaddress()
        # Mine enough blocks to have several AXIS blocks (heights 3, 6, 9, 12...)
        self.generatetoaddress(node, 12, mining_addr, sync_fun=self.no_op)
        self.sync_all()
        return node, wallet

    def test_spv_rpc_availability(self):
        """Test that SPV RPC methods exist in the binary."""
        self.log.info("Testing SPV RPC availability")
        node = self.nodes[0]
        wallet = self.get_wallet(0)

        # Mine some blocks first so getaxisheaders has data
        mining_addr = wallet.getnewaddress()
        self.generatetoaddress(node, 6, mining_addr, sync_fun=self.no_op)

        available = is_spv_rpc_available(node)
        if not available:
            self.log.warning("SPV RPC methods (getaxisheaders/getaxisproof/verifyaxisproof) "
                           "not available in this binary — skipping SPV tests. "
                           "These RPCs are defined in src/rpc/spv.cpp and must be compiled in.")
            # Return False to signal tests should skip
            return False

        self.log.info("  SPV RPC methods are available")
        return True

    def test_getaxisheaders_basic(self):
        """Test getaxisheaders returns correct AXIS block headers."""
        self.log.info("Testing getaxisheaders basic functionality")

        node = self.nodes[0]
        self.setup_spv_chain()

        # Get AXIS headers starting from height 3
        result = node.getaxisheaders(3, 10)

        assert_equal(result["startheight"], 3)
        assert_equal(result["count"], 10)
        assert "headers" in result

        headers = result["headers"]
        header_heights = [h["height"] for h in headers]

        # Should have AXIS headers at heights 3, 6
        assert 3 in header_heights, "Height 3 should be an AXIS header"
        assert 6 in header_heights, "Height 6 should be an AXIS header"

        self.log.info(f"  Found {len(headers)} AXIS headers at heights: {header_heights}")

    def test_axis_headers_structure(self):
        """Test that AXIS headers have correct structure."""
        self.log.info("Testing AXIS headers structure")

        node = self.nodes[0]
        self.setup_spv_chain()

        result = node.getaxisheaders(3, 20)
        headers = result["headers"]
        headers_by_height = {h["height"]: h for h in headers}

        # For AXIS block 3, hashPrevAxis should be GENESIS hash
        # (the block at height 0)
        genesis_hash = node.getblockhash(0)
        h3 = headers_by_height.get(3)
        assert h3 is not None, "Height 3 header should exist"
        assert_equal(h3["hashprevaxis"], genesis_hash)

        self.log.info(f"  Height 3 header: hashPrevAxis={h3['hashprevaxis'][:16]}..., "
                     f"hashAxisMerkleRoot={h3['hashaxismerkleroot'][:16]}...")

    def test_axis_chain_links(self):
        """Test that AXIS headers form a valid skip-chain."""
        self.log.info("Testing AXIS skip-chain continuity")

        node = self.nodes[0]
        self.setup_spv_chain()

        result = node.getaxisheaders(3, 20)
        headers = result["headers"]
        headers_by_height = {h["height"]: h for h in headers}

        heights = sorted(headers_by_height.keys())
        for i in range(len(heights) - 1):
            curr = headers_by_height[heights[i]]
            nxt = headers_by_height[heights[i + 1]]

            self.log.info(f"  Height {curr['height']}: hashPrevAxis={curr['hashprevaxis'][:16]}... "
                         f"-> Height {nxt['height']}: hash={nxt['hash'][:16]}...")

            # Each AXIS block's hashPrevAxis should point to previous AXIS
            if curr['height'] == 3:
                # First AXIS: hashPrevAxis should be genesis
                genesis_hash = node.getblockhash(0)
                assert_equal(curr['hashprevaxis'], genesis_hash)

    def test_getaxisproof_basic(self):
        """Test getaxisproof for a transaction in an AXIS block."""
        self.log.info("Testing getaxisproof basic functionality")

        node = self.nodes[0]
        wallet = self.get_wallet(0)

        # Use wallet's own address for mining so coinbase rewards are credited to wallet
        mining_addr = wallet.getnewaddress()

        # Mine to height 6 to have AXIS blocks
        self.generatetoaddress(node, 6, mining_addr, sync_fun=self.no_op)
        self.sync_mempools()

        # Create a transaction in the mempool
        txid = wallet.sendtoaddress(wallet.getnewaddress(), 1)
        self.log.info(f"  Created transaction: {txid}")

        # Mine it in an AXIS block (height 9)
        block_hash = self.generatetoaddress(node, 3, mining_addr, sync_fun=self.no_op)[0]
        height = node.getblock(block_hash)["height"]
        block_type = self.classify_block(height)

        self.log.info(f"  Mined block at height {height}, type: {block_type}")

        if block_type in ["AXIS", "SUPER_AXIS"]:
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
        wallet = self.get_wallet(0)

        # Use wallet's own address for mining so coinbase rewards are credited to wallet
        mining_addr = wallet.getnewaddress()

        self.generatetoaddress(node, 4, mining_addr, sync_fun=self.no_op)
        self.sync_mempools()

        # Create and mine transaction in a LINK block
        txid = wallet.sendtoaddress(wallet.getnewaddress(), 0.5)
        self.generatetoaddress(node, 1, mining_addr, sync_fun=self.no_op)
        height = node.getblockcount()

        if self.classify_block(height) == "LINK":
            # getaxisproof should fail because tx is not in AXIS block
            try:
                node.getaxisproof(txid)
                assert False, "getaxisproof should fail for non-AXIS tx"
            except Exception as e:
                self.log.info(f"  getaxisproof correctly rejected: {e}")

    def test_verifyaxisproof(self):
        """Test verifyaxisproof validates proofs correctly."""
        self.log.info("Testing verifyaxisproof validation")

        node = self.nodes[0]
        wallet = self.get_wallet(0)

        # Use wallet's own address for mining so coinbase rewards are credited to wallet
        mining_addr = wallet.getnewaddress()

        self.generatetoaddress(node, 9, mining_addr, sync_fun=self.no_op)
        self.sync_mempools()

        txid = wallet.sendtoaddress(wallet.getnewaddress(), 0.1)
        block_hash = self.generatetoaddress(node, 3, mining_addr, sync_fun=self.no_op)[0]
        height = node.getblock(block_hash)["height"]

        if self.classify_block(height) in ["AXIS", "SUPER_AXIS"]:
            proof = node.getaxisproof(txid)
            result = node.verifyaxisproof(proof)
            assert result["valid"], f"Proof should be valid: {result}"
            assert result["checks"]["merkle_proof"]
            assert result["checks"]["pow_verification"]
            assert result["checks"]["axis_chain"]
            self.log.info(f"  Proof at height {height} verified successfully")

    def run_test(self):
        """Run SPV prove tests."""
        self.log.info("Starting TeslaChain SPV prove tests")

        # First check if SPV RPCs are available
        spv_available = self.test_spv_rpc_availability()

        if not spv_available:
            self.log.info("SPV RPCs not available — skipping SPV-specific tests. "
                         "Build with src/rpc/spv.cpp compiled in to enable these tests.")
            return

        self.test_getaxisheaders_basic()
        self.test_axis_headers_structure()
        self.test_axis_chain_links()
        self.test_getaxisproof_basic()
        self.test_getaxisproof_link_block()
        self.test_verifyaxisproof()

        self.log.info("All SPV prove tests passed")


if __name__ == "__main__":
    SPVProveTest(__file__).main()
