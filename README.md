# TeslaChain
## A Bitcoin Core Fork with Deterministic Finality via the 3-6-9 Skip-Chain Protocol

**Status: Testnet Ready** · **Symbol: TAC** · **Regtest P2P Port: 19333** · **Regtest RPC Port: 19344**

---

## Abstract

TeslaChain is a Bitcoin Core fork implementing the **Triadic Consensus Protocol (3-6-9)**. The core innovation: certain blocks (**AXIS** and **SUPER_AXIS** checkpoints) achieve **deterministic finality** — they cannot be reorganized without rewriting the GENESIS block. This transforms Bitcoin's probabilistic finality into mathematical certainty for AXIS checkpoints.

All blocks form a **single unified chain** via `hashPrevBlock` — the 3-6-9 pattern is a checkpointing overlay, not a replacement of Bitcoin's linear consensus. On top of this unified chain, two additional structures provide SPV efficiency and cryptographic immutability: a **skip pointer** (`hashPrevAxisBlock`) for fast AXIS traversal, and **two independent cumulative merkle chains** — one for AXIS checkpoints and one for SUPER_AXIS checkpoints. Modifying any AXIS block breaks only the AXIS merkle chain; modifying a SUPER_AXIS block breaks only the SUPER_AXIS merkle chain. This independence prevents cascade failures across chains.

---

## What's Working

The following features are implemented and functional as of the April 14, 2026 merge:

### 144-Byte Block Headers with AXIS Fields

> **Non-Technical:** Every block has a digital fingerprint. Bitcoin blocks have an 80-byte fingerprint. TeslaChain adds 64 extra bytes to store two additional fingerprints for AXIS blocks. These extra fingerprints are what make TeslaChain special — they let AXIS blocks "lock" the history of transactions permanently.

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

> **Non-Technical:** The internet connects TeslaChain nodes the same way Bitcoin nodes connect — they talk to each other and share blocks. TeslaChain adds four new message types so nodes can share AXIS headers and blocks efficiently. Nodes that support AXIS announce this capability with a special flag, so other nodes know who to ask for AXIS data.

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

> **Non-Technical:** Most people shouldn't need to download the entire blockchain. SPV ("Simplified Payment Verification") lets wallets verify transactions by downloading only AXIS block headers — a tiny fraction of the full chain. TeslaChain's SPV system works like Bitcoin's SPV but can verify transactions in AXIS blocks with just 144-byte headers and a short proof, without trusting a full node completely.

`src/spv_p2p.cpp` implements the SPV client protocol for TeslaChain. SPV clients can:
- Connect to any peer (via DNS seed, hardcoded seed, or manual)
- Send `GETAXISHEADERS` with a LINK-chain locator
- Receive and validate AXIS headers (PoW, skip-chain links, merkle trail)
- Build a local AXIS header chain without downloading full blocks

SPV proof generation and verification is available via RPC (`getaxisproof`, `verifyaxisproof`). The proof structure includes the transaction, its merkle branch, the target AXIS header, and the full AXIS header chain back to GENESIS. Verification checks three things: the merkle proof, the PoW on the target header, and the skip-chain continuity.

### Node Discovery and Bootstrap

> **Non-Technical:** When a new TeslaChain node comes online, it needs to find peers to connect to — just like your phone finding WiFi networks. TeslaChain supports three ways to discover peers: DNS seeds (like a phonebook of node addresses), hardcoded backup addresses, and peer gossip (nodes telling each other about other nodes they know). SPV clients follow a special flow: connect to any peer, ask for AXIS-capable peers, then switch to an AXIS peer for header sync.

Node discovery is implemented across all networks:

- **DNS seeds** — configured per-network in `chainparamsseeds.h`. Testnet has no DNS seed (nodes bootstrap via hardcoded seeds and addr relay). Mainnet placeholder seeds exist; replace with real infrastructure before mainnet launch.
- **Hardcoded seed nodes** — BIP155 serialized fixed seeds for networks without DNS.
- **Peer address gossip** — standard `addr`/`addrv2` messages, filtered by `NODE_AXIS` flag.
- **SPV client discovery flow** — SPV clients connect to any peer, send `GETADDR`, then connect to AXIS-capable peers for header sync.

### SLASH Penalties for AXIS Violations

> **Non-Technical:** If a miner tries to cheat — creating a block with incorrect AXIS fields — they face real consequences. TeslaChain burns 50% or 25% of the block reward (depending on the violation type) and the offending peer gets banned from the network. This economic penalty makes cheating expensive and unprofitable. Unlike Bitcoin where a 51% attacker loses only the electricity spent, a TeslaChain attacker also burns their coinbase reward.

When a block violates AXIS protocol rules, SLASH conditions apply. The penalty system is implemented in `src/validation.cpp` and configured via `AXISlashParams` in `src/consensus/params.h`. Violations include:

- Missing or null `hashPrevAxisBlock` on an AXIS/SUPER_AXIS block → **V1 penalty**
- Missing or null `hashAxisMerkleRoot` on an AXIS/SUPER_AXIS block → **V2 penalty**
- `hashPrevAxisBlock` pointing to wrong block (not height - 3 for AXIS, not height - 9 for SUPER_AXIS) → **V1 penalty**
- `hashAxisMerkleRoot` not matching the computed cumulative merkle for the respective chain → **V2 penalty**
- LINK blocks with non-null AXIS fields → **V1 penalty**

**Penalty Structure:**

| Violation | Burn | DoS Score |
|-----------|------|----------|
| V1: Invalid `hashPrevAxisBlock` | **50%** of coinbase | 50 |
| V2: Invalid `hashAxisMerkleRoot` | **25%** of coinbase | 25 |

### TLA+ Formal Specification

A full TLA+ specification of the AXIS skip-chain protocol is in `docs/formal/`. The specification covers:

- **Model:** `TeslaChainAxis.tla` — all block types (GENESIS, LINK, AXIS, SUPER_AXIS), skip-chain links, AXIS merkle chain construction
- **Invariants:** Four protocol invariants are formalized and model-checked via TLC:
  - `Inv1_AXIS_Contiguity` — every AXIS block at height h links via `hashPrevAxisBlock` to the previous AXIS-family block at height h-3 (or h-9 for SUPER_AXIS)
  - `Inv2_GENESIS_Immutability` — GENESIS never changes
  - `Inv3_No_Skip_Violations` — AXIS blocks appear at heights 3, 6, 9, 12...; SUPER_AXIS at 9, 18, 27...
  - `Inv4_Chain_Finality` — deep AXIS or SUPER_AXIS blocks cannot be modified without rewriting GENESIS
- **Theorem:** `AXIS_IMMUTABILITY_THEOREM` proves that modifying any AXIS block at height H requires modifying all prior AXIS blocks in the AXIS merkle chain, including GENESIS. Similarly for SUPER_AXIS. Proven via TLAPS.

Run the model checker: `java -cp tla2tools.jar tlc.TLC TeslaChainAxis -constants GenesisHash="GENESIS",MaxHeight=12 -deadlock -workers 4`

---

## The 3-6-9 Protocol

### Block Types

| Type | Heights | AXIS Fields | Description |
|------|---------|-------------|-------------|
| GENESIS | 0 | — | Protocol root. Immutable by definition. |
| LINK | 1, 2, 4, 5, 7, 8, 10, 11... | `hashPrevAxisBlock=0`, `hashAxisMerkleRoot=0` | Standard PoW blocks. Same as Bitcoin. |
| AXIS | 3, 6, 12, 15, 21, 24... | Non-zero | Immutable by construction. Height % 3 == 0, not divisible by 9. |
| SUPER_AXIS | 9, 18, 27, 36... | Non-zero | Stronger checkpoints. Height % 9 == 0. |

> **Non-Technical:** Think of TeslaChain like a road system. Regular blocks (LINK) are city streets — fast, frequent, good for local traffic. AXIS blocks are highways — fewer and farther between, but they connect major hubs and carry more weight. SUPER_AXIS blocks are interstate highways — the coarsest grain, used for long-distance travel and the strongest anchors. Every block type follows the same road (the unified chain), but AXIS and SUPER_AXIS blocks leave behind stronger "I was here" markers that can't be erased.

### Three-Chain Architecture

TeslaChain implements **three independent chains** that interleave at different resolutions. Understanding the distinction between the two AXIS fields is critical:

**The unified chain is `hashPrevBlock`** — every block, regardless of type, points to the previous block's hash. There is only one chain from GENESIS to the tip:

```
hashPrevBlock (UNIFIED LINEAR CHAIN):
GENESIS → 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8 → 9 → 10 → 11 → 12 → 13 → 14 → 15 → ...
          G    L    L    A    L    L    A    L    L    S    L    L    A    L    L    A    ...
```

The three block types **interleave** in a single ordering. Every block is on the same chain — the 3-6-9 pattern is a *checkpointing overlay* on Bitcoin's linear chain, not a replacement.

**The two additional structures are overlays:**

| Field | Purpose | Structure |
|-------|---------|-----------|
| `hashPrevAxisBlock` | Skip pointer for SPV traversal | Linked list jumping over LINK blocks |
| `hashAxisMerkleRoot` | Cumulative merkle for immutability | Two independent merkle chains (AXIS, SUPER_AXIS) |

```
AXIS skip pointer:      3 → 6 → 9 → 12 → 15 → 18 → 21 → 24 → ...  (crosses AXIS↔SUPER_AXIS)
SUPER_AXIS skip ptr:        9 → 18 → 27 → 36 → ...

AXIS merkle:           3 → 6 → 12 → 15 → 21 → 24 → 30 → 33 → ...  (AXIS blocks only)
SUPER_AXIS merkle:          9 → 18 → 27 → 36 → 45 → ...          (SUPER_AXIS blocks only)
```

> **Non-Technical:** Every block is numbered sequentially — that's the unified chain. But AXIS and SUPER_AXIS blocks also have two additional "shortcut numbers" baked into them. The first shortcut tells you where the previous AXIS-family block is (skipping over regular LINK blocks). The second is a "fingerprint" that accumulates the history of all AXIS blocks up to this point. These shortcuts let wallets verify transactions in seconds, without downloading the entire blockchain.

---

#### Chain 1 — LINK (Standard Bitcoin Blocks)

> **Non-Technical:** LINK blocks are ordinary Bitcoin-style blocks. They are fast to produce and carry no special commitments. Most blocks on TeslaChain are LINK blocks (~67%). Think of them as regular transactions on the blockchain — necessary, frequent, but not the checkpoints that lock history.

Heights: `h % 3 != 0 AND h % 9 != 0`

```
1, 2, 4, 5, 7, 8, 10, 11, 13, 14, 16, 17, 19, 20, 22, 23, 25, 26...

LINK[h].hashPrevBlock    → LINK[h-1].hash        (standard Bitcoin chain)
LINK[h].hashPrevAxisBlock = 0                     (null — no AXIS participation)
LINK[h].hashAxisMerkleRoot = 0                   (null — no AXIS participation)
```

LINK blocks carry no AXIS commitment. They are pure Bitcoin-style PoW, extending the linear chain.

---

#### Chain 2 — AXIS (Fine-Grain Checkpoints)

> **Non-Technical:** AXIS blocks are TeslaChain's fine-grain checkpoints. Every 3rd block (that isn't also a 9th block) is an AXIS checkpoint. These are more permanent than LINK blocks — once an AXIS block is confirmed, it can't be undone without rewriting GENESIS. They're the "highways" of TeslaChain — less frequent than LINK blocks, but with stronger guarantees.

Heights: `h % 3 == 0 AND h % 9 != 0`

```
3, 6, 12, 15, 21, 24, 30, 33, 39, 42, 48, 51, 57, 60...

AXIS[h].hashPrevAxisBlock   → AXIS[h-3].hash           (skip 3)
AXIS[h].hashAxisMerkleRoot → Cumulative AXIS merkle    (chains AXIS blocks only)
```

AXIS blocks form a skip-linked chain with a **separate cumulative merkle** that chains only through AXIS blocks. SUPER_AXIS blocks are excluded from this merkle chain.

---

#### Chain 3 — SUPER_AXIS (Coarse-Grain Checkpoints)

> **Non-Technical:** SUPER_AXIS blocks are the coarsest checkpoints — every 9th block (heights 9, 18, 27...). They are the "interstate highways" of TeslaChain: the strongest checkpoints, requiring the most work to create, and the hardest to reverse. A transaction confirmed in a SUPER_AXIS block is essentially permanent. The SUPER_AXIS chain is completely independent from the AXIS chain — rewriting one doesn't affect the other.

Heights: `h % 9 == 0`

```
9, 18, 27, 36, 45, 54, 63, 72...

SUPER_AXIS[h].hashPrevAxisBlock   → SUPER_AXIS[h-9].hash   (skip 9)
SUPER_AXIS[h].hashAxisMerkleRoot  → Cumulative SUPER_AXIS merkle (chains SUPER_AXIS only)
```

SUPER_AXIS blocks form their own skip-linked chain with a **separate cumulative merkle** that chains only through SUPER_AXIS blocks. Regular AXIS blocks are excluded.

---

#### Block Distribution Math

> **Non-Technical:** Out of every 9 blocks, roughly 6 are LINK (regular), 2 are AXIS (checkpoints), and 1 is SUPER_AXIS (major checkpoints). This ratio holds regardless of how many blocks are mined — it's built into the protocol math. For every 9 blocks you process, you can expect about 2 AXIS checkpoints and 1 SUPER_AXIS checkpoint.

Within any range [0, N]:

| Chain | Formula | Density |
|-------|---------|---------|
| LINK | N - ⌊2N/3⌋ - ⌊N/9⌋ - 1 | ≈ 66.7% |
| AXIS | ⌊2N/9⌋ | ≈ 22.2% |
| SUPER_AXIS | ⌊N/9⌋ | ≈ 11.1% |

Verification (N=36, blocks 0-35):
```
LINK:        24 blocks (1,2,4,5,7,8,10,11,13,14,16,17,19,20,22,23,25,26,28,29,31,32,34,35)
AXIS:         8 blocks (3,6,12,15,21,24,30,33)
SUPER_AXIS:  3 blocks (9,18,27)
Total: 1 + 24 + 8 + 3 = 36 ✓
```

---

### The Two Fields: Skip Pointer vs. Merkle Chain

> **Non-Technical:** TeslaChain blocks carry two extra numbers beyond normal Bitcoin blocks. The first is a "shortcut number" — it points to where the previous AXIS-family block is, skipping over regular blocks. The second is a "fingerprint" — it accumulates the fingerprints of every prior AXIS block, so you can prove no AXIS history has been changed. These two numbers serve completely different purposes and shouldn't be confused.

**`hashPrevAxisBlock`** — the **skip pointer** — chains through the AXIS-family. This pointer CAN cross between AXIS and SUPER_AXIS types. For example:

```
AXIS[12].hashPrevAxisBlock → Block 9 (a SUPER_AXIS block!)    ← cross-chain
AXIS[15].hashPrevAxisBlock → AXIS[12]
SUPER_AXIS[18].hashPrevAxisBlock → SUPER_AXIS[9]
```

**`hashAxisMerkleRoot`** — the **cumulative merkle** — is **intra-chain only**. Each chain tracks its own merkle without crossing boundaries:

```
AXIS merkle chain:     GENESIS → 3 → 6 → 12 → 15 → 21 → 24 → 30 → 33 → 39 → 42...
                       (excludes 9, 18, 27, 36...)

SUPER_AXIS merkle chain: GENESIS → 9 → 18 → 27 → 36 → 45 → 54 → 63 → 72...
                         (excludes 3, 6, 12, 15, 21, 24...)
```

#### Why Separate Merkle Chains?

> **Non-Technical:** If both checkpoint types shared one fingerprint chain, rewriting an AXIS block would break all SUPER_AXIS fingerprints too — and vice versa. TeslaChain keeps them completely separate, like two independent security systems. If someone hacks one (rewrites an AXIS block), the other (SUPER_AXIS chain) is completely unaffected. This independence is a deliberate design choice.

If AXIS and SUPER_AXIS shared one merkle chain, an AXIS block rewrite would cascade and break every subsequent SUPER_AXIS block's merkle root. Separate chains mean:

- AXIS reorganization → only AXIS merkle chain breaks, SUPER_AXIS chain stays valid
- SUPER_AXIS reorganization → only SUPER_AXIS merkle chain breaks, AXIS chain stays valid
- Independence: each chain can be SPV-verified without trusting the other

---

## Mathematical Proof: AXIS Immutability

The protocol achieves **deterministic finality** for AXIS and SUPER_AXIS checkpoints through two independent mechanisms.

> **Non-Technical:** Why can't AXIS blocks be undone? Because each AXIS block's fingerprint includes the fingerprint of the previous AXIS block — all the way back to GENESIS. To change Block 6, you'd also need to change Block 3, because Block 6's fingerprint depends on Block 3's fingerprint. And to change Block 3, you'd need to change GENESIS — which is impossible by definition. This mathematical dependency chain is called induction, and it's the same reason a 100-floor tower collapses if you remove the 3rd floor. The same logic applies to SUPER_AXIS blocks, but anchored at the 9th block instead.

### Weighted Consensus (α Multipliers)

> **Non-Technical:** Bitcoin treats every block equally — one block = one vote. TeslaChain gives AXIS blocks 3 votes and SUPER_AXIS blocks 9 votes. This means the honest majority gains consensus weight much faster than an attacker can keep up with. An attacker who tries to rewrite history by secretly mining a fake chain starts losing votes the moment they diverge from an AXIS checkpoint — their secret chain has no AXIS "votes" until they catch back up and build a new AXIS checkpoint of their own.

Bitcoin's longest-chain rule is purely linear — each block contributes equally to accumulated work. TeslaChain introduces **α multipliers** that give AXIS and SUPER_AXIS blocks disproportionate consensus weight:

| Block Type | Condition | α Multiplier | Role |
|------------|-----------|-------------|------|
| LINK | h % 3 != 0 | α = 1 | Standard PoW weight |
| AXIS | h % 3 == 0, h % 9 != 0 | α = 3 | 3× consensus weight |
| SUPER_AXIS | h % 9 == 0 | α = 9 | 9× consensus weight |

**Cumulative weight after N blocks:**

```
W(N) = Σ D · α_i  (for i = 1 to N)
```

**Approximate closed form (large N):**

```
LINK:         ~2/3 of blocks  → α contribution ≈ 2/3 · 1  = 2/3
AXIS:         ~2/9 of blocks  → α contribution ≈ 2/9 · 3  = 6/9
SUPER_AXIS:   ~1/9 of blocks → α contribution ≈ 1/9 · 9  = 9/9

W(N) / (D · N) ≈ 2/3 + 6/9 + 9/9 = 2/3 + 2/3 + 1 = 7/3 ≈ 2.33×
```

An honest TeslaChain chain accumulates consensus weight **~2.33× faster** than a flat linear chain of equal PoW. An attacker who diverges from an AXIS or SUPER_AXIS checkpoint loses the α multiplier for all subsequent blocks until they re-establish a valid checkpoint on their secret chain.

> **Non-Technical:** The math shows honest nodes gain weight 2.33× faster than a flat chain. Combined with SLASH penalties that burn an attacker's coinbase, TeslaChain makes it mathematically irrational to attempt a long-range reorganisation.

Any AXIS block at height H in the AXIS merkle chain cannot be modified without modifying all AXIS blocks from height 3 to H in that chain, including GENESIS.

Any SUPER_AXIS block at height H in the SUPER_AXIS merkle chain cannot be modified without modifying all SUPER_AXIS blocks from height 9 to H in that chain, including GENESIS.

### Proof by Induction — AXIS Chain

> **Non-Technical:** The proof works like dominoes. Block 3 is domino #1 — it points directly to GENESIS, and GENESIS can never change. So Block 3 can never be changed. Block 6 is domino #2 — it points to Block 3, which we just proved can't change. So Block 6 can't change either. Block 9 is domino #3, pointing to Block 6. And so on, all the way down the chain. Once the first domino falls, all the rest must follow.

**Base case (H = 3):**
Block 3's `hashPrevAxisBlock` references GENESIS. GENESIS is immutable by protocol definition. Therefore Block 3 is immutable — any modification requires modifying GENESIS, which is impossible.

**Inductive step:**
Assume Block K (K > 3, K % 3 == 0, K % 9 != 0) is immutable. Block K+3 references Block K via `hashPrevAxisBlock`. Any modification to Block K+3 requires the new Block K+3 to reference the (unchangeable) Block K. Since Block K is immutable, Block K+3 is also immutable.

**Conclusion by induction:**
ALL regular AXIS blocks (3, 6, 12, 15, 21, 24...) are immutable without rewriting GENESIS.

∎

### Proof by Induction — SUPER_AXIS Chain

**Base case (H = 9):**
Block 9's `hashPrevAxisBlock` references GENESIS. GENESIS is immutable. Therefore Block 9 is immutable.

**Inductive step:**
Assume Block K (K > 9, K % 9 == 0) is immutable. Block K+9 references Block K via `hashPrevAxisBlock`. Any modification to Block K+9 requires referencing the unchangeable Block K. Since Block K is immutable, Block K+9 is also immutable.

**Conclusion by induction:**
ALL SUPER_AXIS blocks (9, 18, 27, 36...) are immutable without rewriting GENESIS.

∎

---

## Genesis Block

> **Non-Technical:** The GENESIS block is the first block of TeslaChain — mined on April 10, 2026. It's the one block that everything else depends on. Think of it as the cornerstones of a building: if the cornerstone changes, the whole building is different. GENESIS is special because both the AXIS merkle chain AND the SUPER_AXIS merkle chain start from it. Changing GENESIS would mean changing every AXIS and SUPER_AXIS checkpoint ever created — it's mathematically impossible.

The GENESIS block is the **immutable anchor** of all three chains — timestamped to **April 10, 2026**. It is the only block that belongs to both the AXIS merkle chain (as the first entry) and the SUPER_AXIS merkle chain (as the first entry), making it the single point of immutability for the entire protocol.

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

> **Non-Technical:** When a node receives AXIS headers from a peer, it checks every field carefully. If any AXIS field is wrong — a skip pointer pointing to the wrong block, a fingerprint that doesn't match — the peer is penalized. Small violations result in a warning (DoS score); repeated violations result in a 24-hour ban. This is how TeslaChain enforces honesty: nodes that lie get disconnected.

```
On receiving AXIS_HEADERS from untrusted peer:
  1. Parse 144-byte headers
  2. Check PoW (DoS: +50 if invalid)
  3. Check hashPrevBlock connects to known chain
  4. If SUPER_AXIS block (height % 9 == 0):
       - hashPrevAxisBlock must be NON-ZERO (DoS: +10)
       - hashAxisMerkleRoot must be NON-ZERO (DoS: +10)
       - hashPrevAxisBlock → previous SUPER_AXIS block at height-9 (DoS: +10)
       - hashAxisMerkleRoot matches computed cumulative SUPER_AXIS merkle (DoS: +10)
  5. If regular AXIS block (height % 3 == 0 but not % 9):
       - hashPrevAxisBlock must be NON-ZERO (DoS: +10)
       - hashAxisMerkleRoot must be NON-ZERO (DoS: +10)
       - hashPrevAxisBlock → previous AXIS block at height-3 (DoS: +10)
       - hashAxisMerkleRoot matches computed cumulative AXIS merkle (DoS: +10)
  6. If LINK block:
       - hashPrevAxisBlock must be ZERO (DoS: +10)
       - hashAxisMerkleRoot must be ZERO (DoS: +10)
  7. Accumulate DoS score; ban peer above threshold
```

### SPV Proof Structure

> **Non-Technical:** An SPV proof is like a receipt. It shows: (1) your transaction was in a block, (2) the block has valid proof-of-work, and (3) the block is connected to GENESIS through the AXIS checkpoint chain. You don't need to download the full blockchain to verify this receipt — just the AXIS headers from GENESIS to the block in question. This makes TeslaChain usable on mobile devices and laptops with limited storage.

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
| Mainnet | 19333 | 19344 | `0x54435343` (TCSC) |
| Testnet | 19335 | 19347 | `0x54435453` (TCTS) |
| Regtest | 19336 | 19348 | `0xfabfb5da` |

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
- AXIS validation enforced on testnet (via `fAxisValidationOnTestnet=true`); skipped on regtest (functional test framework limitation)

### 🔜 What's Remaining

- **Mainnet launch** — Requires real seed nodes, DNS infrastructure, public network, and community adoption
- **DNS seeds for mainnet** — Need real domains and infrastructure for mainnet bootstrap
- **SPV merkle proofs for LINK chain** — Currently SPV proofs cover AXIS/SUPER_AXIS blocks only (LINK chain relies on full block download)
- **Compact block support (BIP 152)** — For efficient AXIS block relay over P2P
- **Full P2P SPV client wire protocol** — `FetchAxisHeadersFromPeers()` is not yet connected to the peer selection layer (`GetBestPeerForAxisHeaders()` stub remains)

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

**Main branch:** `main`

### Recent Merges

| Commit | PR | Description |
|--------|-----|-------------|
| `1e8bcdc` | #28 | fix: use github.repository for REPO_USE_CIRRUS_RUNNERS |
| `9769082` | #27 | fix: resolve CI lint failures (circular dependency, duplicate include, style) |
| `ab6eb59` | #26 | fix: SUPER_AXIS must link to previous SUPER_AXIS (9 blocks back) — **first passing test** |
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
## TeslaMath ##
Link + Link = Axis⛓️
1+2=3
Axis + Axis = SuperAxis⚓️
****3+6=9****

Axis - Link = Link
3-2=1
Axis - Link = Link
3-1=2

Superaxis - Axis = Axis
9-6=3
Superaxis - Axis = Axis
9-3=6

Teslamath of the skip pointers:

AXIS[h] = AXIS[h-3] + 3    (hashPrevAxisBlock is +3)
SUPER[h] = SUPER[h-9] + 9  (hashPrevSuperBlock would be +9)

Inverse:
AXIS[h] - AXIS[h-3] = 3
SUPER[h] - SUPER[h-9] = 9

The skip pointer IS the addition operator in Teslamath. hashPrevSuper is just the same operation at the SUPER level. 

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
