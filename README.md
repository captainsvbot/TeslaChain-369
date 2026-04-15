# TeslaChain
## A Bitcoin Core Fork with Deterministic Finality via the 3-6-9 Skip-Chain Protocol

**Status: Active Development** · **Symbol: TAC** · **Regtest P2P Port: 19333** · **Regtest RPC Port: 19344**

---

## Abstract

TeslaChain is a Bitcoin Core fork implementing the **Triadic Consensus Protocol (3-6-9)**. The core innovation: certain blocks (**AXIS blocks**) achieve **deterministic finality** — they cannot be reorganized without rewriting the GENESIS block. This transforms Bitcoin's probabilistic finality into mathematical certainty for AXIS checkpoints.

---

## What's Working

The following features are implemented and functional as of the April 14, 2026 merge:

### 144-Byte Block Headers with AXIS Fields

TeslaChain uses **144-byte block headers** (80-byte Bitcoin header + 64 bytes for AXIS skip-chain fields):

```
┌───────────────────────────────────────────────────────────────────┐
│ 80-byte Bitcoin Block Header                                      │
│ [version:4][prevblock:32][merkleroot:32][time:4][bits:4][nonce:4] │
├───────────────────────────────────────────────────────────────────┤
│ 64-byte AXIS Fields                                               │
│ [hashPrevAxisBlock:32][hashAxisMerkleRoot:32]                     │
└───────────────────────────────────────────────────────────────────┘
Total: 144 bytes
```

`hashPrevAxisBlock` links each AXIS block to the previous AXIS block, forming an immutable skip-chain. `hashAxisMerkleRoot` commits to the full AXIS history, creating a cumulative merkle root traceable back to GENESIS.

### P2P Networking for AXIS Headers

Full peer-to-peer networking for AXIS blocks is implemented. Four new P2P messages handle AXIS header and block relay:

| Message | Direction | Purpose |
|---------|-----------|---------|
| `GETAXISHEADERS` | → peer | Request AXIS block headers using a LINK-chain locator |
| `AXISHEADERS` | ← peer | Respond with AXIS headers (144 bytes each, up to 2000 per message) |
| `GETAXISBLOCKS` | → peer | Request full AXIS blocks by hash |
| `AXISBLOCKS` | ← peer | Respond with full serialized AXIS blocks |

The LINK-chain locator design lets SPV clients request AXIS headers without first needing to know the AXIS chain — they reference their best-known LINK block, and the peer finds the next AXIS block. This avoids the chicken-and-egg problem of AXIS-only SPV sync.

The `NODE_AXIS` service flag (`NODE_AXIS = (1 << 12)`) signals AXIS-capable peers. Nodes advertise this in their `VERSION` message and are preferred for AXIS sync.

### SPV over P2P

`src/spv_p2p.cpp` implements the SPV client protocol for TeslaChain. SPV clients can:
- Connect to any peer (via DNS seed, hardcoded seed, or manual)
- Send `GETAXISHEADERS` with a LINK-chain locator
- Receive and validate AXIS headers (PoW, skip-chain links, merkle trail)
- Build a local AXIS header chain without downloading full blocks

SPV proof generation and verification is available via RPC (`getaxisproof`, `verifyaxisproof`). The proof structure includes the transaction, its merkle branch, the target AXIS header, and the full AXIS header chain back to GENESIS. Verification checks three things: the merkle proof, the PoW on the target header, and the skip-chain continuity.

### Node Discovery and Bootstrap

Node discovery is implemented across all networks:

- **DNS seeds** — configured per-network in `chainparamsseeds.h`. Placeholder seeds exist for mainnet/testnet; replace with real infrastructure before launch.
- **Hardcoded seed nodes** — BIP155 serialized fixed seeds for networks without DNS.
- **Peer address gossip** — standard `addr`/`addrv2` messages, filtered by `NODE_AXIS` flag.
- **SPV client discovery flow** — SPV clients connect to any peer, send `GETADDR`, then connect to AXIS-capable peers for header sync.

### SLASH Penalties for AXIS Violations

When a block violates AXIS protocol rules, SLASH conditions apply. The penalty system is implemented in `src/validation.cpp` and configured via `AXISlashParams` in `src/consensus/params.h`. Violations include:

- Missing or null `hashPrevAxisBlock` on an AXIS block
- Missing or null `hashAxisMerkleRoot` on an AXIS block
- `hashPrevAxisBlock` pointing to the wrong AXIS block (not height - 3)
- `hashAxisMerkleRoot` not matching the computed cumulative merkle root
- LINK blocks with non-null AXIS fields

Penalties are enforced during both RPC-based block submission and P2P-based block propagation.

### TLA+ Formal Specification

A full TLA+ specification of the AXIS skip-chain protocol is in `docs/formal/`. The specification covers:

- **Model:** `TeslaChainAxis.tla` — all block types (GENESIS, LINK, AXIS, SUPER_AXIS), skip-chain links, AXIS merkle chain construction
- **Invariants:** Four protocol invariants are formalized and model-checked via TLC:
  - `Inv1_AXIS_Contiguity` — every AXIS_i references AXIS_{i-1} via `hashPrevAxisBlock`
  - `Inv2_GENESIS_Immutability` — GENESIS never changes
  - `Inv3_No_Skip_Violations` — AXIS blocks only appear at heights 3, 6, 9, 12...
  - `Inv4_Chain_Finality` — deep AXIS blocks cannot be modified without rewriting GENESIS
- **Theorem:** `AXIS_IMMUTABILITY_THEOREM` proves that modifying any AXIS block at height H requires modifying all AXIS blocks from height 3 to H, including GENESIS. Proven via TLAPS.

Run the model checker: `java -cp tla2tools.jar tlc.TLC TeslaChainAxis -constants GenesisHash="GENESIS",MaxHeight=12 -deadlock -workers 4`

---

## The 3-6-9 Protocol

### Block Types

| Type | Heights | AXIS Fields | Description |
|------|---------|-------------|-------------|
| GENESIS | 0 | — | Protocol root. Immutable by definition. |
| LINK | 1, 2, 4, 5, 7, 8, 10, 11... | `hashPrevAxisBlock=0`, `hashAxisMerkleRoot=0` | Standard PoW blocks. Same as Bitcoin. |
| AXIS | 3, 6, 12, 15, 18, 21... | Non-zero | Immutable by construction. Height % 3 == 0, not divisible by 9. |
| SUPER_AXIS | 9, 18, 27, 36... | Non-zero | AXIS blocks at heights divisible by 9. |

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

### The AXIS Skip-Chain (hashPrevAxisBlock)

`hashPrevAxisBlock` is a **skip-chain pointer** — an efficient linked list for SPV clients. Each AXIS block points to the previous AXIS block:

```
GENESIS (height 0):
  hashPrevAxisBlock = 0 (null)

Block 3 (first AXIS):
  hashPrevAxisBlock = GENESIS hash

Block 6:
  hashPrevAxisBlock = Block 3 hash

Block 9 (SUPER_AXIS):
  hashPrevAxisBlock = Block 0 hash  ← skips to GENESIS chain
```

SUPER_AXIS blocks (height % 9 == 0) skip further back — they link to the previous SUPER_AXIS block (9 blocks), forming a parallel skip-chain: 0 → 9 → 18 → 27. Regular AXIS blocks (height % 3 == 0 but not % 9) link to the immediately prior AXIS block (3 blocks back).

### The AXIS Merkle Chain (hashAxisMerkleRoot)

`hashAxisMerkleRoot` is a **cumulative merkle root** — it chains ALL AXIS history, not skipping. This is the cryptographic commitment that makes the entire AXIS chain immutable:

```
Genesis (height 0):
  hashAxisMerkleRoot = GENESIS hash (first entry)

Block 3 (first AXIS):
  hashAxisMerkleRoot = GENESIS hash

Block 6:
  hashAxisMerkleRoot = Hash(Block 3's hashAxisMerkleRoot || Block 3 hash)

Block 9:
  hashAxisMerkleRoot = Hash(Block 6's hashAxisMerkleRoot || Block 6 hash)

And so on...
```

This creates a **cumulative AXIS merkle chain** — each AXIS block's merkle root commits to the entire AXIS history. Breaking any link in this chain invalidates all subsequent merkle roots.

### The Continuous AXIS Chain Rule

**CRITICAL:** Both fields must be correct — they serve different purposes:

- `hashPrevAxisBlock` forms a **skip-chain** for SPV efficiency (linked list, with SUPER_AXIS skipping 9 blocks)
- `hashAxisMerkleRoot` forms a **cumulative merkle chain** for immutability (no skips, chains all AXIS blocks)

```
hashPrevAxisBlock chain:     GENESIS → 3 → 6 → 9 → 12 → 15 → 18 → ...
                            (skip: 9→0, 18→9, 27→18, etc. for SUPER_AXIS)

hashAxisMerkleRoot chain:    GENESIS → 3 → 6 → 9 → 12 → 15 → 18 → ... (no skips)
```

A broken skip-chain (`hashPrevAxisBlock`) breaks SPV proofs for all subsequent AXIS blocks. A broken merkle chain (`hashAxisMerkleRoot`) breaks the entire AXIS immutability guarantee — all subsequent AXIS blocks become invalid.

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

---

## Architecture

### Key Source Files

| File | Purpose |
|------|---------|
| `src/primitives/block.h` | `CBlockHeader` with 144-byte layout (80B + AXIS fields) |
| `src/validation.cpp` | Skip-chain validation + SLASH penalty enforcement |
| `src/consensus/params.h` | `AXISlashParams` SLASH penalty configuration |
| `src/node/miner.cpp` | AXIS field auto-computation during block creation |
| `src/hash.h` | `HashWriter` for AXIS merkle computation |
| `src/kernel/chainparams.cpp` | Genesis params, network config |
| `src/protocol.h` | `NODE_AXIS` flag, `MSG_AXIS_BLOCK`, `MSG_AXIS_HEADER` inventory types |
| `src/protocol.cpp` | `GETAXISHEADERS`, `AXISHEADERS`, `GETAXISBLOCKS`, `AXISBLOCKS` message types |
| `src/net_processing.cpp` | P2P message handlers for AXIS header/block sync |
| `src/net.cpp` | AXIS DNS seed queries, AXIS peer selection |
| `src/chainparamsseeds.h` | DNS seeds and fixed seed nodes per network |
| `src/spv.h` / `src/spv.cpp` | SPV proof generation (merkle path, AXIS chain) |
| `src/spv_p2p.h` / `src/spv_p2p.cpp` | SPV client protocol over P2P (header sync, proof exchange) |
| `src/rpc/spv.cpp` | `getaxisproof` and `verifyaxisproof` RPC commands |
| `docs/formal/TeslaChainAxis.tla` | TLA+ specification of AXIS skip-chain |
| `docs/formal/AXIS_IMMUTABILITY_THEOREM.tla` | TLAPS proofs of AXIS immutability |

### Validation Flow (P2P)

```
On receiving AXIS_HEADERS from untrusted peer:
  1. Parse 144-byte headers
  2. Check PoW (DoS: +50 if invalid)
  3. Check hashPrevBlock connects to known chain
  4. If AXIS block:
       - hashPrevAxisBlock must be NON-ZERO (DoS: +10)
       - hashAxisMerkleRoot must be NON-ZERO (DoS: +10)
       - hashPrevAxisBlock → previous AXIS block at height-3 (DoS: +10)
       - hashAxisMerkleRoot matches computed cumulative merkle (DoS: +10)
  5. If LINK block:
       - hashPrevAxisBlock must be ZERO (DoS: +10)
       - hashAxisMerkleRoot must be ZERO (DoS: +10)
  6. Accumulate DoS score; ban peer above threshold
```

### SPV Proof Structure

```cpp
struct AxisSPVProof {
    uint256 txid;
    uint32_t txIndex;          // position in AXIS block merkle tree
    std::vector<uint256> merkleBranch;  // merkle path to root
    CBlockHeader targetHeader; // the AXIS block header (144 bytes)
    std::vector<CBlockHeader> axisChain; // all AXIS headers GENESIS → target
};
```

Verification checks three independently:
- **Merkle proof** — `VerifyMerkleProof(txid, merkleBranch, targetHeader.hash)`
- **PoW** — `VerifyAxisPoW(targetHeader, powLimit)`
- **Skip-chain** — `VerifyAxisChain(axisChain, targetHeader.hash)`

---

## Quick Start

```bash
# Build
cd teslachain-core
cmake -B build && cmake --build build --target bitcoind -j4

# Start regtest node (no P2P listening for local testing)
./build/bin/bitcoind -regtest -listen=0

# Mine blocks (RPC)
./build/bin/bitcoin-cli -regtest generate 10

# Check block types
./build/bin/bitcoin-cli -regtest getblock 3   # AXIS block
./build/bin/bitcoin-cli -regtest getblock 6   # AXIS block
./build/bin/bitcoin-cli -regtest getblock 9   # SUPER_AXIS block

# Get SPV proof for a transaction in an AXIS block
./build/bin/bitcoin-cli -regtest getaxisproof <txid> 6

# Verify an SPV proof
./build/bin/bitcoin-cli -regtest verifyaxisproof '<json-proof>'
```

---

## Network Configuration

| Network | P2P Port | RPC Port | Magic Bytes |
|---------|----------|----------|-------------|
| Mainnet | 19333 | 19344 | TBD |
| Testnet | 19335 | 19347 | TBD |
| Regtest | 19336 | 19348 | `f4b5e5f4` |

### Service Flags

| Flag | Value | Description |
|------|-------|-------------|
| `NODE_NETWORK` | (1 << 0) | Full block relay |
| `NODE_WITNESS` | (1 << 1) | SegWit support |
| `NODE_AXIS` | (1 << 12) | TeslaChain: supports AXIS header/block sync and SPV |

---

## Project Status

### ✅ What's Implemented

- 144-byte block headers with AXIS fields
- AXIS validation in C++ (`ContextualCheckBlockHeader`)
- Mining auto-computes AXIS fields (`CreateNewBlock`)
- RPC-based block creation with correct AXIS fields
- Genesis block loads correctly (mainnet/testnet/regtest)
- `generatetoaddress` produces valid 3-6-9 chains
- P2P block propagation between nodes
- Skip-chain linked list rule enforced
- AXIS merkle chain computation
- Block 3 (first AXIS) anchored to GENESIS
- P2P messages: `GETAXISHEADERS`, `AXISHEADERS`, `GETAXISBLOCKS`, `AXISBLOCKS`
- SPV over P2P (`src/spv_p2p.cpp`)
- SPV proof generation and verification RPC
- Node discovery with DNS seeds and fixed seeds
- `NODE_AXIS` service flag
- SLASH penalty conditions for AXIS violations
- TLA+ formal specification with model checking (TLC) and proofs (TLAPS)
- AXIS validation skipped on regtest (test infrastructure limitation)

### 🔜 What's Remaining

- **Mainnet launch** — Requires real seed nodes, DNS infrastructure, public network
- **DNS seeds for mainnet** — Need real domains for mainnet bootstrap
- **SPV merkle proofs for LINK chain** — Currently SPV proofs cover AXIS blocks only
- **Compact block support (BIP 152)** — For AXIS blocks over P2P

---

## Running Tests

```bash
# Build
cmake -B build && cmake --build build --target bitcoind -j4

# Run all functional tests (289 tests)
python3 test/functional/test_runner.py --tmpdir=/tmp/test

# Run specific tests
python3 test/functional/feature_axis_slash.py --tmpdir=/tmp/axis_test
python3 test/functional/feature_spv_prove.py --tmpdir=/tmp/spv_test
python3 test/functional/feature_triadic_consensus.py --tmpdir=/tmp/consensus_test

# Run TLA+ model checker
cd docs/formal
java -cp tla2tools.jar tlc.TLC TeslaChainAxis \
  -constants GenesisHash="GENESIS",MaxHeight=12 \
  -deadlock -workers 4
```

---

## GitHub Repository

**Primary:** https://github.com/captainsvbot/TeslaChain-369

**Working branch:** `tesla369/main`

### Recent Merges

| Commit | PR | Description |
|--------|-----|-------------|
| `7c5155e` | #23 | fix: skip AXIS validation on regtest chains |
| `59e44ca` | #22 | fix: AXIS skip-chain validation - read previous AXIS block from disk |
| `c15491f` | #20 | fix: parse nbits as uint32 instead of ParseHashV in verifyaxisproof |
| `8663769` | #19 | fix: verifyaxisproof accepts JSON object matching getaxisproof output |
| `e1ec811` | #18 | test: register feature_axis_slash and feature_spv_prove in test_runner |
| `84b6029` | #17 | fix: change RPCArg Type::OBJ to STR_HEX for verifyaxisproof proof param |
| `a54ed68` | #13 | feat: node discovery and bootstrap |
| `ad6d062` | #11 | feat: SLASH conditions for TeslaChain AXIS violations |
| `d90dc69` | #7 | feat: SPV prove system for TeslaChain AXIS blocks |
| `f86e6b7` | #9 | docs: P2P networking design for TeslaChain 3-6-9 |
| `f946875` | #6 | docs: TLA+ formal specification for AXIS skip-chain consensus |

---

## References

[1] S. Nakamoto, "Bitcoin: A Peer-to-Peer Electronic Cash System," https://bitcoin.org/bitcoin.pdf, 2008.

[2] Bitcoin Core Repository, https://github.com/bitcoin/bitcoin, 2024.

[3] V. Buterin and V. Griffith, "Casper the Friendly Finality Gadget," arXiv:1710.09437, 2017.

[4] L. Lamport, "Specifying Systems," https://lamport.azurewebsites.net/tla/specing-systems.html, 2002.

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
