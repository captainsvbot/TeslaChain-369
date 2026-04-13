# TeslaChain
## A Bitcoin Core Fork with Deterministic Finality via the 3-6-9 Skip-Chain Protocol

**Status: Active Development** · **Symbol: TAC** · **Regtest P2P Port: 19333** · **Regtest RPC Port: 19344**

---

## Abstract

TeslaChain is a Bitcoin Core fork implementing the **Triadic Consensus Protocol (3-6-9)**. The core innovation: certain blocks (**AXIS blocks**) achieve **deterministic finality** — they cannot be reorganized without rewriting the GENESIS block. This transforms Bitcoin's probabilistic finality into mathematical certainty for AXIS checkpoints.

---

## Quick Start

```bash
# Build
cd teslachain-core
./autogen.sh && ./configure && make -j$(nproc)

# Start regtest node
./build/bin/bitcoind -regtest -listen=0

# Mine blocks (RPC)
./build/bin/bitcoin-cli -regtest generate 10

# Check block types
./build/bin/bitcoin-cli -regtest getblock 3   # AXIS block
./build/bin/bitcoin-cli -regtest getblock 6   # AXIS block
./build/bin/bitcoin-cli -regtest getblock 9   # SUPER_AXIS block
```

---

## Genesis Block

The GENESIS block is the **immutable anchor** of the entire chain — timestamped to **April 10, 2026**.

```
Genesis Hash:  144cc8ae15a2ba8590e05fa4ab6315eca0f08b26f4f2ef298f7bea271280f353
Merkle Root:  613d37b6ef1898496fe5cfd153adf86b2338d381c4c46e5d5b3092f94bcbcc6f
Timestamp:    1775858400 (10/Apr/2026 22:00:00 UTC)
Nonce:        745750 (mainnet) | 1 (testnet/regtest)
Bits:         0x201fffff (TESLACHAIN) | 0x1d00ffff (Bitcoin compat)
```

**Genesis Message (Coinbase):**
```
"The Times 10/Apr/2026 TeslaChain begins: The future is electric"
```

**Why GENESIS is immutable:**
1. **Hardcoded in source** — `chainparams.cpp` bakes in the genesis hash
2. **Protocol anchor** — Any block referencing a different genesis is rejected
3. **No chain before it** — PoW cannot rewrite what doesn't exist

---

## The 3-6-9 Protocol

### Block Types

| Type | Heights | AXIS Fields | Description |
|------|---------|-------------|-------------|
| GENESIS | 0 | — | Protocol root. Immutable by definition. |
| LINK | 1, 2, 4, 5, 7, 8, 10, 11... | `hashPrevAxisBlock=0`, `hashAxisMerkleRoot=0` | Standard PoW blocks. Same as Bitcoin. |
| AXIS | 3, 6, 12, 15, 18, 21... | Non-zero | Immutable by construction. Height % 3 == 0, not divisible by 9. |
| SUPER_AXIS | 9, 18, 27, 36... | Non-zero | AXIS blocks at heights divisible by 9. |

### Block Header Size

TeslaChain uses **144-byte block headers** (80-byte Bitcoin header + 64 bytes for AXIS fields):

```
┌─────────────────────────────────────────────────────────────┐
│ 80-byte Bitcoin Block Header                                │
│ [version:4][prevblock:32][merkleroot:32][time:4][bits:4][nonce:4] │
├─────────────────────────────────────────────────────────────┤
│ 64-byte AXIS Fields                                         │
│ [hashPrevAxisBlock:32][hashAxisMerkleRoot:32]               │
└─────────────────────────────────────────────────────────────┘
Total: 144 bytes
```

### The Skip-Chain Structure

Every AXIS block has **two hash references**:

1. **Normal PoW link** — `hashPrevBlock` pointing to the immediately previous block
2. **Skip-chain link** — `hashPrevAxisBlock` pointing to the **previous AXIS block only**

```
GENESIS ───────────────────────────────────────────── 🔒 IMMUTABLE
    │ hashPrevAxisBlock
    ▼
Block 3 (AXIS) ────────────────────────────────────── 🔒 IMMUTABLE
    │                                           hashPrevBlock
    │ hashPrevAxisBlock                              │
    ▼                                                 ▼
Block 4 (LINK) → Block 5 (LINK) → Block 6 (AXIS) ── 🔒 IMMUTABLE
                                                    ↑
                                           hashPrevAxisBlock (→ Block 3)
```

### AXIS Merkle Root Computation

For each AXIS block, `hashAxisMerkleRoot` chains the AXIS history:

```
Genesis (height 0):
  hashAxisMerkleRoot = GENESIS hash (first entry in AXIS merkle)

Block 3 (first AXIS):
  hashPrevAxisBlock = GENESIS hash
  hashAxisMerkleRoot = GENESIS hash

Block 6:
  hashPrevAxisBlock = Block 3 hash
  hashAxisMerkleRoot = Hash(Block 3's hashAxisMerkleRoot || Block 3 hash)

Block 9:
  hashPrevAxisBlock = Block 6 hash
  hashAxisMerkleRoot = Hash(Block 6's hashAxisMerkleRoot || Block 6 hash)

And so on...
```

This creates a **cumulative AXIS merkle chain** — each AXIS block includes all previous AXIS block hashes in its merkle root.

---

## The Continuous AXIS Chain Rule

**CRITICAL:** The skip-chain is a **linked list**. Each AXIS block must reference the AXIS block immediately before it.

```
Block 9's hashPrevAxisBlock MUST point to Block 6
Block 6's hashPrevAxisBlock MUST point to Block 3
Block 3's hashPrevAxisBlock MUST point to GENESIS
```

**You cannot skip:**
```
Block 9 → Block 3?  ❌ INVALID
Block 9 → Block 6?  ✅ VALID (only if Block 6 exists)
```

**Consequence:** A broken link breaks everything after it. If any AXIS block is invalid or missing, ALL subsequent AXIS blocks become invalid.

```
GENESIS → Block 3 → Block 6 → Block 9 → Block 12 → ...
    ✅         ✅        ❌        CANNOT EXIST ❌

If Block 6 is missing/invalid:
  → Block 9 has NO VALID AXIS PARENT → Block 9 is INVALID
  → All subsequent AXIS blocks are INVALID
```

---

## Mathematical Proof: AXIS Immutability

### Theorem

Any AXIS block at height H cannot be modified without modifying all AXIS blocks from height 3 to H, including GENESIS.

### Proof by Induction

**Base case (H = 3):**
Block 3's `hashPrevAxisBlock` references GENESIS. GENESIS is immutable by protocol definition. Therefore Block 3 is immutable — any modification requires modifying GENESIS, which is impossible.

**Inductive step:**
Assume Block K (K > 3, K % 3 == 0) is immutable. Block K+3 references Block K via `hashPrevAxisBlock`. Any modification to Block K+3 requires the new Block K+3 to reference the (unchangeable) Block K. Since Block K is immutable, Block K+3 is also immutable.

**Conclusion by induction:**
ALL AXIS blocks (3, 6, 9, 12, 15, 18, 21...) are immutable without rewriting GENESIS.

∎

---

## Deep Finality: Why the Past Becomes More Immutable

In Bitcoin, the further back a block is, the more **vulnerable** it becomes (more time for an attacker to build a longer chain).

In TeslaChain, the further back an AXIS block is, the more **immutable** it becomes — rewriting it requires rewriting every subsequent AXIS block back to GENESIS.

**Example: Rewriting Block 99 (AXIS)**
To rewrite Block 99, an attacker must rewrite: GENESIS, Block 3, Block 6, Block 9... Block 96 (33 AXIS blocks total), each requiring proof-of-work.

| Blocks Back | AXIS Blocks to Rewrite |
|-------------|----------------------|
| 10 | 10 |
| 33 | 33 |
| 100 | 100 |

An AXIS block from April 10, 2026 (GENESIS day) is **more immutable** than one from 2036 — any attack needs to rewrite the entire AXIS chain since GENESIS.

---

## What TeslaChain Prevents vs Does Not

### ✅ What TeslaChain Prevents

- **Double-spending AXIS-confirmed transactions** — Once in an AXIS block, a transaction is final
- **Rewriting AXIS blocks** — Cannot be done without rewriting GENESIS
- **Creating fake AXIS checkpoints** — Skip-chain must reference the actual previous AXIS block
- **Broken AXIS chain propagation** — Nodes reject AXIS blocks with invalid skip-chain
- **LINK modifications reaching AXIS** — Replacement chains hitting AXIS are DENIED

### ❌ What TeslaChain Does Not Prevent

- **Future reorgs** — After the present block, reorgs are still possible (but not past AXIS)
- **51% attacks on LINK blocks** — LINK blocks remain probabilistic like Bitcoin

---

## Architecture

### Key Source Files

| File | Purpose |
|------|---------|
| `src/node/miner.cpp` | AXIS field computation during block creation |
| `src/validation.cpp` | Skip-chain validation in `ContextualCheckBlockHeader` |
| `src/hash.h` | `HashWriter` for AXIS merkle computation |
| `src/kernel/chainparams.cpp` | Genesis params, network config |
| `src/uint256.h` | 256-bit integer handling (little-endian) |
| `test/functional/feature_triadic_consensus.py` | RPC-based 3-6-9 integration tests |

### C++ Validation Flow

```
ContextualCheckBlockHeader()
  ├── Check PoW (unchanged from Bitcoin)
  ├── Check timestamp
  └── If AXIS block (height % 3 == 0):
      ├── hashPrevAxisBlock must be NON-ZERO
      ├── hashAxisMerkleRoot must be NON-ZERO
      ├── hashPrevAxisBlock → previous AXIS block (height - 3)
      ├── If height == 3: hashAxisMerkleRoot == GENESIS hash
      └── If height > 3: hashAxisMerkleRoot == Hash(prev_AXIS_merkle || prev_AXIS_hash)
```

### Mining Auto-Computation

When `CreateNewBlock()` mines an AXIS block, it automatically computes:
- `hashPrevAxisBlock` = hash of previous AXIS block
- `hashAxisMerkleRoot` = cumulative AXIS merkle root

LINK blocks get `hashPrevAxisBlock = 0` and `hashAxisMerkleRoot = 0`.

---

## Running Tests

```bash
# Build test binary
cd build && cmake .. && make -j4

# Run the triadic consensus test
./build/bin/test_bitcoin --run_test=triadic_consensus_tests

# Run functional tests
./build/bin/test_bitcoin --legacyrpc  # or use test runner:
python3 test/functional/feature_triadic_consensus.py --configfile=build/test/config.ini
```

### Functional Test Coverage

| Test | What it verifies |
|------|-----------------|
| `feature_triadic_consensus.py` | 144-byte headers, LINK/AXIS classification, RPC mining, multi-node sync |

---

## Project Status

### What's Working

- [x] 144-byte block headers with AXIS fields
- [x] AXIS validation in C++ (`ContextualCheckBlockHeader`)
- [x] Mining auto-computes AXIS fields (`CreateNewBlock`)
- [x] RPC-based block creation with correct AXIS fields
- [x] Genesis block loads correctly (mainnet/testnet/regtest)
- [x] `generatetoaddress` produces valid 3-6-9 chains
- [x] P2P block propagation between nodes
- [x] Skip-chain linked list rule enforced
- [x] AXIS merkle chain computation
- [x] Block 3 (first AXIS) anchored to GENESIS

### What's Not Implemented

- [ ] **Mainnet launch** — Requires bootstrap nodes, network infra
- [ ] **Wallet GUI** — CLI wallet works; GUI not yet ported
- [ ] **SLASH conditions** — No penalty mechanism for AXIS violations
- [ ] **Formal verification** — TLA+/Coq proof of skip-chain consensus
- [ ] **SPV prove** — Lightweight proof of AXIS inclusion

---

## Network Configuration

| Network | P2P Port | RPC Port | Magic Bytes |
|---------|----------|----------|-------------|
| Mainnet | 19333 | 19344 | TBD |
| Testnet | 19335 | 19347 | TBD |
| Regtest | 19336 | 19348 | `f4b5e5f4` |

---

## GitHub Repository

**Primary:** https://github.com/captainsvbot/TeslaChain-369

### Recent Commits

| Commit | Description |
|--------|-------------|
| `8f0949d7` | Merge: Take RPC-based test (our version) |
| `aa011019` | fix: Correct AXIS skip-chain field computation in mining for block 3 |
| `90ada4ece` | test: AXIS skip-chain P2P tests + validation fix |
| `e95febaeb` | test: Triadic consensus tests + genesis nonce fixes |
| `fad6cc46` | Merge pull request #1: Add AXIS fields to CBlockHeader |

---

## References

[1] S. Nakamoto, "Bitcoin: A Peer-to-Peer Electronic Cash System," https://bitcoin.org/bitcoin.pdf, 2008.

[2] Bitcoin Core Repository, https://github.com/bitcoin/bitcoin, 2024.

[3] V. Buterin and V. Griffith, "Casper the Friendly Finality Gadget," arXiv:1710.09437, 2017.

---

## Acknowledgments

- **Satoshi Nakamoto** — Invented Bitcoin
- **Bitcoin Core team** — Reference implementation
- **Nikola Tesla** — The 3-6-9 pattern (inspiration)

---

## License

TeslaChain is a fork of Bitcoin Core. The Bitcoin Core base code is available under the MIT License (see COPYING file).

The TeslaChain protocol, 3-6-9 skip-chain consensus, AXIS/SUPER_AXIS layers, and related innovations are proprietary and released for peer review only. No permission is granted for reuse, modification, or deployment without explicit written consent.

---

*TeslaChain: Bitcoin's security model, with deterministic finality for AXIS blocks.*

**Symbol:** TAC · **Max Supply:** 21,000,000 · **Consensus:** PoW + 3-6-9 Skip-Chain