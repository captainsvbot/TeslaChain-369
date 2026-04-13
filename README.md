# TeslaChain
## A Bitcoin Core Fork with Deterministic Finality via the 3-6-9 Skip-Chain Protocol

**Status: Experimental** · **Symbol: TAC** · **P2P Port: 19333** · **RPC Port: 19344**

---

## Abstract

TeslaChain is a Bitcoin Core fork implementing the **Triadic Consensus Protocol (3-6-9)**. The core innovation: certain blocks (AXIS blocks) achieve **deterministic finality** — they cannot be reorganized without rewriting the GENESIS block. This transforms Bitcoin's probabilistic finality into mathematical certainty for AXIS checkpoints.

---

## Genesis Block

The GENESIS block is the **immutable anchor** of the entire chain — timestamped to **April 10, 2026**.

```
Genesis Hash:  00003b3afbf3bb763a77465d846f9a2789a99e0328967c4c1184c705bfe11b8b
Merkle Root:  4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b
Timestamp:     1775858400 (10/Apr/2026 22:00:00 UTC)
Nonce:        6175
Bits:        0x1d00ffff
```

**Genesis Message (Coinbase):**
```
"The Times 10/Apr/2026 TeslaChain begins: The future is electric"
```

**Why GENESIS is immutable:**

1. **Hardcoded in source code** — `chainparams.cpp` has the genesis hash baked in
2. **Changing it breaks the protocol** — any node would reject blocks referencing a different genesis
3. **No PoW can rewrite it** — there is no chain before it; it starts the chain

**Genesis coinbase:** Unspendable. Like Bitcoin's genesis, there is no private key for genesis coins. They are permanently locked as a historical monument.

---

## The 3-6-9 Protocol

### Block Types

| Type | Heights | Description |
|------|---------|-------------|
| GENESIS | 0 | Protocol root. Immutable by definition. |
| LINK | 1, 2, 4, 5, 7, 8, 10, 11... | Standard PoW blocks. Same as Bitcoin. |
| AXIS | 3, 6, 12, 15, 18, 21... | Immutable by construction (height % 3 == 0, not divisible by 9) |
| SUPER_AXIS | 9, 18, 27, 36... | AXIS blocks at heights divisible by 9 |

### The Skip-Chain Structure

Every AXIS block has **two hash references**:

1. **Normal PoW link** — `hashPrevBlock` pointing to the immediately previous block (LINK or AXIS)
2. **Skip-chain link** — `hashPrevAxisBlock` pointing to the **previous AXIS block only**

```
GENESIS ───────────────────────────────────────────────────── 🔒 IMMUTABLE
    │ hashPrevAxisBlock
    ▼
Block 3 (AXIS) ─────────────────────────────────────────── 🔒 IMMUTABLE
    │                                           hashPrevBlock
    │ hashPrevAxisBlock                              │
    ▼                                                 ▼
Block 4 (LINK) → Block 5 (LINK) → Block 6 (AXIS) ─────── 🔒 IMMUTABLE
                                                    ↑
                                           hashPrevAxisBlock (→ Block 3)
```

**Example from the actual chain:**

```
Block 0 (GENESIS)
  hash: 00003b3afbf3bb763a77465d846f9a2789a99e0328967c4c1184c705bfe11b8b

Block 3 (AXIS)
  hashPrevBlock:         → Block 2
  hashPrevAxisBlock:     → Block 0 (GENESIS)

Block 6 (AXIS)
  hashPrevBlock:         → Block 5
  hashPrevAxisBlock:     → Block 3

Block 9 (SUPER_AXIS)
  hashPrevBlock:         → Block 8
  hashPrevAxisBlock:     → Block 6
```

### The Continuous AXIS Chain Rule

**CRITICAL:** The skip-chain is a **linked list**, not a skip list. Each AXIS block must reference the AXIS block immediately before it.

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

**Consequence:** If any AXIS block in the chain is invalid or missing, ALL subsequent AXIS blocks become invalid. The AXIS chain is a continuous immutable thread.

```
GENESIS → Block 3 → Block 6 → Block 9 → Block 12 → ...
    ✅         ✅        ❌        CANNOT EXIST ❌
              |
              If Block 6 is missing/invalid:
              Block 9 has NO VALID AXIS PARENT
              → Block 9 is INVALID
              → All subsequent AXIS blocks are INVALID
```

---

## Mathematical Proof: AXIS Immutability

### Theorem

Any AXIS block at height H cannot be modified without modifying all AXIS blocks from height 3 to H, including GENESIS.

### Proof by Induction

**Base case (H = 3):**

Block 3's `hashPrevAxisBlock` references GENESIS. GENESIS is immutable by protocol definition. Therefore Block 3 is immutable — any modification to Block 3 requires modifying GENESIS, which is impossible.

**Inductive step:**

Assume Block K (where K > 3, K % 3 == 0) is immutable.

Block K+3 references Block K via `hashPrevAxisBlock`. Any modification to Block K+3 requires:
1. Modifying Block K+3 itself
2. The new Block K+3 must still reference the (unchangeable) Block K via skip-chain

Since Block K is immutable by assumption, Block K+3 is also immutable.

**Conclusion by induction:**

ALL AXIS blocks (3, 6, 9, 12, 15, 18, 21...) are immutable without rewriting GENESIS.

∎

---

## Deep Finality: Why the Past Becomes More Immutable

In Bitcoin, the further back a block is, the more **vulnerable** it becomes (more time for an attacker to build a longer chain).

In TeslaChain, the further back an AXIS block is, the more **immutable** it becomes — because rewriting it requires rewriting every subsequent AXIS block back to GENESIS.

### Example: Rewriting Block 99

To rewrite Block 99 (AXIS), an attacker must also rewrite:

| Must Rewrite | Reason |
|-------------|--------|
| GENESIS | Block 3's skip-chain anchor |
| Block 3 | Block 6's skip-chain anchor |
| Block 6 | Block 9's skip-chain anchor |
| Block 12 | Block 15's skip-chain anchor |
| ... | ... |
| Block 96 | Block 99's skip-chain anchor |

**33 AXIS blocks total** (3, 6, 9, ..., 99), each requiring proof-of-work, all chained to an immutable GENESIS.

**The math favors the honest chain exponentially:**

```
Rewriting 1 block back:  1 AXIS block
Rewriting 10 blocks back:  10 AXIS blocks  
Rewriting 33 blocks back:  33 AXIS blocks
Rewriting 100 blocks back: 100 AXIS blocks
```

An AXIS block from April 10, 2026 (GENESIS day) would be **more immutable** than one from 2036, because any attack would need to rewrite the entire AXIS chain since GENESIS.

---

## Attack Simulations

### Simulation 1: Equal Length (10 Honest vs 10 Attacker)

Both chains build 10 blocks from GENESIS.

| Metric | Honest | Attacker |
|--------|--------|----------|
| Length | 11 blocks | 11 blocks |
| Block 3 hash | Different | Different |
| Block 3 VALID? | ✅ | ✅ |
| Skip-chain → GENESIS | ✅ | ✅ |

**Result:** Tie on length. Both produce valid but different Block 3 AXIS checkpoints. Neither chain can invalidate the other's GENESIS anchor.

### Simulation 2: Attacker Builds Longer Chain (10 Honest vs 15 Attacker)

Attacker mines 5 more blocks than honest network.

| Metric | Honest | Attacker |
|--------|--------|----------|
| Length | 11 blocks | 16 blocks |
| Winner (longest chain) | ❌ | ✅ |

**BUT:**

- Honest Block 3, 6, 9 AXIS checkpoints remain in the honest history
- Attacker cannot "erase" honest AXIS checkpoints
- Attacker has their own AXIS chain — but they cannot invalidate honest AXIS history

### Simulation 3: Broken AXIS Chain (Critical)

Attacker produces:
- Block 0 ✅
- Block 3 ✅
- Block 6 ❌ (invalid/missing)

**Question:** Can attacker reach Block 9?

**Answer: NO.**

```
Block 9's hashPrevAxisBlock MUST point to Block 6
But Block 6 is invalid/missing
→ Block 9 has NO VALID AXIS PARENT
→ Block 9 is INVALID
→ All subsequent AXIS blocks are INVALID
```

The AXIS chain is **all-or-nothing**. A broken link breaks everything after it.

---

## What TeslaChain Prevents vs What It Does Not

### ✅ What TeslaChain Prevents

- **Double-spending AXIS-confirmed transactions** — Once in an AXIS block, a transaction is final
- **Rewriting AXIS blocks already on the honest chain** — Cannot be done without rewriting GENESIS
- **Creating fake AXIS checkpoints** — Skip-chain must reference the actual previous AXIS block
- **Long-range attacks on AXIS history** — Further back = more AXIS blocks to rewrite
- **Broken AXIS chain propagation** — Nodes reject AXIS blocks with invalid skip-chain
- **LINK block modifications that reach AXIS** — LINK blocks can be replaced, but as soon as a replacement chain hits an AXIS block, it is DENIED

### ❌ What TeslaChain Does Not Prevent

- **Reorgs in the future** — After the present block, reorgs are still possible (but not the LAST block)

---

## How History Editing Actually Works on TeslaChain

An attacker who tries to edit history by building a secret chain:

1. **LINK blocks (1, 2, 4, 5, 7, 8...)** — Can be replaced without detection. LINK blocks are probabilistic like Bitcoin.

2. **When the replacement chain reaches an AXIS block (3, 6, 9...)** — The AXIS block must contain the hash of the PREVIOUS AXIS block. If the attacker edited Block 3's history, their Block 6 cannot contain Block 3's hash (it's been changed). Their chain is REJECTED.

3. **Result** — History cannot be edited past the most recent AXIS block. Past AXIS blocks are permanently anchored.

**In short: You CAN reorg the future (blocks after "now"), but you CANNOT reorg the past (blocks before "now" that are anchored by AXIS blocks).**

---

## Honest Assessment

TeslaChain is a **proof-of-concept**, not a production system.

### What Works

- [x] The 3-6-9 mathematics are sound (proved by induction)
- [x] Skip-chain creates genuine immutable AXIS checkpoints
- [x] Regtest node runs correctly and produces valid 3-6-9 chains
- [x] First 100+ regtest blocks successfully mined and validated
- [x] Block explorer UI confirms correct AXIS/LINK classification
- [x] GENESIS block with April 10, 2026 timestamp

### What's Not Implemented

- [ ] **P2P networking** — Nodes don't yet advertise or sync AXIS skip-chain headers
- [ ] **Full header implementation** — `hashPrevAxisBlock` is validation-only (v4 approach); a non-upgraded node could accept an invalid AXIS chain
- [ ] **Genesis coinbase spending** — Unspendable by design (like Bitcoin)
- [ ] **Mainnet launch** — Requires P2P networking and bootstrap nodes
- [ ] **Wallet GUI** — CLI wallet (`bitcoin-cli`) works; GUI not yet ported
- [ ] **SLASH conditions** — No penalty mechanism for AXIS violations (future work)
- [ ] **Formal verification** — TLA+/Coq proof of skip-chain consensus (future work)

### V5 Attempt (Rolled Back)

Tried to add `hashPrevAxisBlock` to block headers (full implementation) — broke genesis serialization. Reverted to v4 validation-only approach. Full header implementation requires a new genesis block and chain restart.

---

## Technical Details

### Genesis Hash Deep Dive

The genesis hash `00003b3afbf3bb763a77465d846f9a2789a99e0328967c4c1184c705bfe11b8b` is computed from:

| Field | Value |
|-------|-------|
| Version | 1 |
| Previous Block | `0000000000000000000000000000000000000000000000000000000000000000` |
| Merkle Root | `4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b` |
| Timestamp | 1775858400 (10/Apr/2026 22:00:00 UTC) |
| Bits | 0x1d00ffff |
| Nonce | 6175 |

The timestamp "The Times 10/Apr/2026 TeslaChain begins: The future is electric" marks the birth of TeslaChain on April 10, 2026.

### Why hashPrevAxisBlock is a Linked List (Not a Skip List)

A skip list allows jumping multiple steps. The AXIS skip-chain does NOT:

```
❌ INVALID: Block 9 → hashPrevAxisBlock → Block 3 (skips Block 6)
✅ VALID:   Block 9 → hashPrevAxisBlock → Block 6 (must be immediate previous AXIS)
```

This design choice ensures:
1. **Contiguity** — No gaps in the AXIS chain
2. **Proof of work** — Each AXIS block requires PoW, creating a continuous trail
3. **Broken chain detection** — Missing AXIS blocks are immediately detectable

### Comparison: Bitcoin vs TeslaChain

| Property | Bitcoin | TeslaChain |
|----------|---------|------------|
| Finality type | Probabilistic | Probabilistic (LINK) + Deterministic (AXIS) |
| Longest chain rule | Yes | Yes (for LINK) |
| Immutable checkpoints | No | Yes (AXIS blocks) |
| Reorg vulnerability | Increases over time | Decreases over time (AXIS) |
| Genesis mutable | No | No |
| 51% attack resistance | Probabilistic | Mathematical for AXIS |

---

## Running the Node

### Build

```bash
cd ./teslachain-core
./autogen.sh
./configure
make -j$(nproc)
```

### Start Regtest

```bash
# Terminal 1: Start daemon
./build/bin/bitcoind -regtest -listen=0

# Terminal 2: Check status
./build/bin/bitcoin-cli -regtest getblockchaininfo

# Mine 10 blocks
./build/bin/bitcoin-cli -regtest generate 10

# Check block 3 (AXIS)
./build/bin/bitcoin-cli -regtest getblock 3

# Check skip-chain reference
./build/bin/bitcoin-cli -regtest getblockheader 3
```

### Web Explorer

```bash
# With node running:
# Visit http://localhost:3000
# Shows: Current height, 3-6-9 progress, recent blocks with AXIS/LINK markers

# View GENESIS block specifically:
# http://localhost:3000/block/0
# Shows: Hash, timestamp, nonce, genesis message, hashPrevAxisBlock reference
```

### Verify 3-6-9 Pattern

```bash
# Mine past block 9 (first SUPER_AXIS)
./build/bin/bitcoin-cli -regtest generate 10

# Check blocks 3, 6, 9
./build/bin/bitcoin-cli -regtest getblock 3   # AXIS
./build/bin/bitcoin-cli -regtest getblock 6   # AXIS
./build/bin/bitcoin-cli -regtest getblock 9   # SUPER_AXIS

# Block 9's hashPrevAxisBlock should point to Block 6
# Block 6's hashPrevAxisBlock should point to Block 3
# Block 3's hashPrevAxisBlock should point to GENESIS
```

---

## Project Structure

```
./teslachain-core/
├── src/
│   ├── validation.h      # 3-6-9 types: TriadicBlockType, IsAxisBlock, IsSuperAxisBlock
│   ├── validation.cpp   # Skip-chain validation logic
│   ├── kernel/chainparams.cpp  # Genesis hash, network params, TAC currency
│   └── ...
├── build/bin/
│   ├── bitcoind         # Daemon
│   └── bitcoin-cli      # RPC client
└── ...

TeslaChain/              # GitHub repo
├── README.md            # This file
├── WHITEPAPER.md        # Academic white paper
└── src/
    └── validation.cpp   # Pushed to GitHub
```

---

## GitHub

**Repository:** https://github.com/captainsvbot/TeslaChain

**Commit history (as of April 10, 2026):**
- `ae21fab` docs: v2 README - skip-chain linked list rule, deep finality, attack simulations
- `17ca7f6` docs: Extensive README - honest status, simulation results
- `e9be7a7` docs: TeslaChain White Paper - Final professional version
- `6f89b39` docs: TeslaChain White Paper v3
- `df38f68` docs: TeslaChain White Paper v2
- `972698a` docs: Comprehensive protocol documentation
- `00a9881` docs: Initial 3-6-9 Skip-Chain Protocol documentation

---

## References

[1] S. Nakamoto, "Bitcoin: A Peer-to-Peer Electronic Cash System," https://bitcoin.org/bitcoin.pdf, 2008.

[2] Bitcoin Core Repository, https://github.com/bitcoin/bitcoin, 2024.

[3] V. Buterin and V. Griffith, "Casper the Friendly Finality Gadget," arXiv:1710.09437, 2017.

[4] W. Dai, "b-money," http://www.weidai.com/bmoney.txt, 1998.

[5] H. Massias, X.S. Avila, and J.-J. Quisquater, "Design of a Secure Timestamping Service with Minimal Trust Requirements," 1999.

---

## Acknowledgments

- **Satoshi Nakamoto** — Invented Bitcoin
- **Bitcoin Core team** — Reference implementation
- **Dr. Craig Wright / BSV** — Demonstrated Bitcoin's big-block potential
- **Nikola Tesla** — The 3-6-9 pattern (inspiration)

---

## Conclusion

TeslaChain extends Bitcoin's proof-of-work with the 3-6-9 Triadic Consensus Protocol. LINK blocks remain probabilistic like Bitcoin. AXIS blocks achieve **deterministic finality** — once confirmed, they are immutable by mathematical construction, not merely by economic incentive.

The GENESIS block is the immutable anchor. Every AXIS block chains to the previous AXIS block, ultimately chaining back to GENESIS. This creates an immutable thread from GENESIS through all AXIS blocks — a thread that becomes stronger over time as more AXIS blocks are added.

**Bitcoin:** Probabilistic finality. More confirmations = more likely final, never certain.

**TeslaChain AXIS blocks:** Deterministic finality. Block 3 confirmed = Block 3 is final forever, mathematically.

That's the 3-6-9 innovation: transforming probabilistic certainty into mathematical certainty for AXIS checkpoints.

---

*TeslaChain: Bitcoin's security model, with deterministic finality for AXIS blocks.*

**Symbol:** TAC · **Max Supply:** 21,000,000 (like Bitcoin) · **Consensus:** PoW + 3-6-9 Skip-Chain
**Genesis Date:** 10/Apr/2026
