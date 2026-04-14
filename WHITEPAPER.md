# TeslaChain: A Digital Currency with Deterministic Finality via the 3-6-9 Skip-Chain Protocol

**CaptainBSV**


---

## Abstract

A peer-to-peer electronic cash system allows online payments to be sent directly from one party to another without the burdens of a financial institution. Bitcoin, introduced by Nakamoto in 2008, provides this capability but relies on probabilistic finality—the certainty of a transaction increases with confirmations but never reaches absolute certainty. This paper introduces TeslaChain, a Bitcoin Core fork implementing the Triadic Consensus Protocol (3-6-9), which provides *deterministic* finality for AXIS blocks through a mathematical proof that they cannot be reorganized without rewriting the GENESIS block. We demonstrate that within the proof-of-work framework, certain blocks can achieve absolute immutability.

---

## 1. Introduction

Commerce on the Internet has come to rely almost exclusively on financial institutions serving as trusted third parties to process electronic payments. While this model works well for most transactions, it suffers from the inherent weaknesses of the trust-based model.

The cost of mediation increases transaction costs, limits minimum practical transaction sizes, and removes the possibility of non-reversible transactions for non-reversible services. With the possibility of reversal, the need for trust spreads. Merchants must be wary of their customers, hassling them for more information than they would otherwise need.

What is needed is an electronic payment system based on cryptographic proof instead of trust, allowing any two willing parties to transact directly without a trusted third party.

Bitcoin, introduced by Nakamoto in 2008, provides this capability through proof-of-work and the longest-chain-wins consensus mechanism. However, Bitcoin's security model relies on probabilistic finality: a transaction's finality increases with confirmations but never reaches absolute certainty.

This paper introduces TeslaChain, which extends Bitcoin's proof-of-work framework with the Triadic Consensus Protocol (3-6-9), providing deterministic finality for a defined class of blocks (AXIS blocks) while maintaining backward compatibility with Bitcoin's architecture.

---

## 2. Transactions

We define an electronic coin as a chain of digital signatures. Each owner transfers the coin to the next by digitally signing a hash of the previous transaction and the public key of the next owner, and adding these to the end of the coin. A payee can verify the signatures to verify the chain of ownership.

The problem with this model is that the payee cannot verify that one of the owners did not double-spend the coin. A common solution is to introduce a trusted central authority, or mint, that checks every transaction for double-spending. After each transaction, the coin must be returned to the mint to issue a new coin, and only coins issued directly from the mint are trusted to not be double-spent.

The only way to confirm the absence of a transaction is to be aware of all transactions. In the mint model, the mint is aware of all transactions and decides which arrived first. To accomplish this without a trusted party, transactions must be publicly announced [1], and we need a system for participants to agree on a single history of the order in which they were received.

---

## 3. Timestamp Server

The solution we propose begins with a timestamp server. A timestamp server works by taking a hash of a block of items to be timestamped and widely publishing the hash, similar to publishing a notice in a newspaper [2]. The timestamp proves that the data must have existed at the time, obviously, in order to get into the hash.

In Bitcoin, each block's header contains the hash of the previous block, forming a chain. Modifying a block in the past would require redoing all subsequent blocks' work, creating a computational burden that secures the chain.

TeslaChain extends this model by designating certain blocks (AXIS blocks) as forming an immutable sub-chain anchored to the GENESIS block. This creates a mathematical guarantee that AXIS blocks cannot be modified without rewriting the entire chain back to GENESIS.

---

## 4. Proof-of-Work

The proof-of-work mechanism used by Bitcoin and TeslaChain is essentially one-CPU-one-vote. The innovation of proof-of-work is that it makes double-spending attacks computationally expensive. An attacker must expend resources (electricity, hardware) to rewrite history, creating an economic disincentive.

The proof-of-work involves scanning for a value that when hashed (e.g., with SHA-256) produces a result with a certain number of leading zero bits. The required work is exponential in the number of zero bits required.

For TeslaChain, proof-of-work secures the LINK blocks (1, 2, 4, 5, 7, 8...) exactly as in Bitcoin. For AXIS blocks (3, 6, 9, 12...), proof-of-work is supplemented by the mathematical structure of the skip-chain, creating deterministic finality.

---

## 5. The Network

TeslaChain's P2P network extends Bitcoin's protocol with four new messages purpose-built for AXIS header and block relay. The network maintains backward compatibility with Bitcoin P2P peers while enabling full AXIS synchronization between TeslaChain nodes.

### 5.1 P2P Message Protocol

TeslaChain introduces four new P2P message types for AXIS data, all using the standard Bitcoin wire framing (4-byte message start, 12-byte command, 4-byte length, 4-byte checksum). These messages are **AXIS-specific** and are silently ignored by Bitcoin nodes, ensuring protocol separation.

#### GETAXISHEADERS / AXISHEADERS

SPV clients and full nodes use `GETAXISHEADERS` to request AXIS block headers from peers. The message carries a **LINK-chain block locator** (hashes of LINK blocks from tip toward GENESIS) plus an optional stop hash. The responding peer finds the first AXIS block that connects to the locator and returns AXIS headers sequentially from that point.

```
GETAXISHEADERS Payload:
  [compactSize]   locator count (max 101)
  [n × 32 bytes]  LINK block locator hashes
  [32 bytes]      stop hash (uint256::ZERO = no stop limit)
```

`AXISHEADERS` responds with a vector of 144-byte AXIS block headers. Unlike Bitcoin's `headers` message (which appends a per-header tx count suffix), `AXISHEADERS` sends **pure 144-byte headers with no trailing tx count** — this prevents ambiguity with Bitcoin's 80-byte wire format and ensures Bitcoin nodes simply ignore the unknown message type.

```
AXISHEADERS Payload:
  [compactSize]   header count (max 2000)
  [n × 144 bytes] AXIS block headers
```

#### GETAXISBLOCKS / AXISBLOCKS

For full block retrieval, `GETAXISBLOCKS` requests full AXIS blocks by header hash. `AXISBLOCKS` responds with the complete serialized blocks.

```
GETAXISBLOCKS Payload:
  [compactSize]   block hash count
  [n × 32 bytes]  block hashes

AXISBLOCKS Payload:
  [compactSize]   block count
  [n × variable]  serialized CBlock objects
```

### 5.2 NODE_AXIS Service Flag

Nodes that support the AXIS protocol advertise this capability via the `NODE_AXIS` service flag in the version handshake:

```cpp
enum ServiceFlags : uint64_t {
    // ... existing flags ...
    NODE_AXIS = (1 << 12),  // Node supports AXIS P2P protocol
};
```

During the version handshake, nodes exchange `VERSION` messages advertising their capabilities. AXIS-capable nodes prefer connecting to other AXIS-capable peers for header and block relay. SPV clients use `GETADDR` to discover AXIS-capable peers, then filter responses to nodes advertising `NODE_AXIS`.

### 5.3 SPV over P2P

SPV clients that do not store full blocks use `GETAXISHEADERS` to build a lightweight AXIS header chain. The SPV client implementation (`spv_p2p.cpp`) provides two key components:

- **AxisHeaderCache**: Stores and manages a local cache of AXIS headers, supporting efficient lookup by height or hash.
- **SPVP2PClient**: Manages P2P connections, issues `GETAXISHEADERS` requests, and processes `AXISHEADERS` responses.

The SPV header sync flow:

1. SPV client connects to an AXIS-capable peer (via DNS seed or hardcoded seed).
2. SPV client sends `GETAXISHEADERS` with a LINK-chain locator pointing to GENESIS.
3. Peer responds with `AXISHEADERS` containing AXIS block #3 (first AXIS block).
4. SPV client validates PoW, `hashPrevAxisBlock == 0` (GENESIS anchor), and `hashAxisMerkleRoot`.
5. SPV client sends subsequent `GETAXISHEADERS` with the newly received AXIS block hash as locator.
6. Peer responds with the next batch of AXIS headers (up to 2000 per response).
7. Repeat until the SPV client reaches the chain tip.

After 100,000 total blocks, the AXIS header chain totals approximately **4.8 MB** (33,333 AXIS headers × 144 bytes), compared to ~7.4 MB for all headers — making SPV verification highly storage-efficient.

### 5.4 Node Discovery

TeslaChain uses a tiered bootstrap strategy:

1. **Hardcoded seed nodes**: Pre-configured IP addresses for initial connectivity.
2. **DNS seeds**: DNS servers returning A/AAAA records of active nodes, configured via `-dnsseedaxis`.
3. **Permanent seed list**: Fallback hardcoded IP列表 for environments where DNS is unavailable.
4. **Loopback fallback**: For regtest mode, connections fall back to `127.0.0.1` when no seeds are configured.

### 5.5 Network Operation

The steps to run the network are as follows:

1. New transactions are broadcast to all nodes.
2. Each node collects new transactions into a block.
3. Each node works on finding a difficult proof-of-work for its block.
4. When a node finds a proof-of-work, it broadcasts the block to all nodes.
5. Nodes accept the block only if all transactions in it are valid and not already spent.
6. Nodes express their acceptance of the block by working on creating the next block in the chain, using the hash of the accepted block as the previous hash.

Nodes always consider the longest chain to be the correct one and will keep working on extending it. If two nodes broadcast different versions of the next block simultaneously, some nodes may receive one or the other first. In that case, they work on the first one they received, but save the other branch in case it becomes longer. The tie is broken when the next proof-of-work is found and one branch becomes longer; the nodes that were working on the other branch will then switch to the longer one.

---

## 6. The Triadic Consensus Protocol (3-6-9)

### 6.1 Overview

The Triadic Consensus Protocol introduces a secondary block classification. Every third block (height % 3 == 0) is designated as an **AXIS block**. AXIS blocks form an immutable sub-chain anchored to the GENESIS block.

### 6.2 Block Types

| Type | Heights | Description | Header Size |
|------|---------|-------------|-------------|
| GENESIS | 0 | Protocol-defined root, immutable | 80 bytes |
| LINK | 1, 2, 4, 5, 7, 8... | Standard proof-of-work blocks | 80 bytes |
| AXIS | 3, 6, 12, 15, 18, 21... | Immutable by construction | **144 bytes** |
| SUPER AXIS | 9, 18, 27, 36... | AXIS blocks at heights divisible by 9 | **144 bytes** |

AXIS block headers extend Bitcoin's 80-byte block header with **64 additional bytes** containing two AXIS-specific fields:

- **`hashPrevAxisBlock`** (32 bytes): A pointer to the previous AXIS block in the skip-chain, forming a transitive integrity chain from each AXIS block back to GENESIS. For the first AXIS block (height 3), this field is zero (anchored directly to GENESIS).
- **`hashAxisMerkleRoot`** (32 bytes): A cumulative merkle root computed over all AXIS blocks from GENESIS to the current AXIS block, in skip-chain order. This provides a verifiable commitment to the entire AXIS history without requiring the full LINK chain.

LINK blocks (non-AXIS heights) carry zero values for both `hashPrevAxisBlock` and `hashAxisMerkleRoot` — these fields must be null for non-AXIS blocks, enforced by DoS scoring on P2P messages.

### 6.3 The Skip-Chain Structure

AXIS blocks reference not only their immediate predecessor, but also form a chain among themselves:

```
GENESIS → Block 1 → Block 2 → Block 3 → Block 4 → Block 5 → Block 6 → ...
                           ↑                               ↑
                           └─────── Skip-Chain ──────────────┘
```

This creates a transitive commitment: Block 6's skip-chain commitment references Block 3, which references GENESIS.

### 6.4 Mathematical Immutability

**Theorem**: Any AXIS block at height *H* cannot be modified without modifying all AXIS blocks from height 3 to height *H*, including GENESIS.

**Proof**: By induction on the AXIS block height.

*Base case (H = 3)*: Block 3's skip-chain references GENESIS. Since GENESIS is immutable by protocol definition, Block 3 is immutable.

*Inductive step*: Assume Block *k* (k > 3, k % 3 == 0) is immutable. Block *k+3* references Block *k* via skip-chain. Any modification to Block *k+3* requires modification to Block *k*, contradicting the induction hypothesis. Therefore, Block *k+3* is immutable.

By induction, all AXIS blocks are immutable. ∎

---

## 7. Incentive

By convention, the first transaction in a block is a special transaction that starts a new coin owned by the creator of the block. This adds an incentive for nodes to support the network and provides a way to initially distribute coins into circulation.

TeslaChain follows this model for LINK blocks. For AXIS blocks, the skip-chain structure creates an additional security incentive: including an invalid AXIS reference would invalidate the block, wasting the proof-of-work expenditure.

---

## 8. Reclaiming Disk Space

Once the latest transaction in a coin is buried under enough blocks, the spent transactions before it can be discarded to save disk space. TeslaChain reclaims disk space exactly as Bitcoin does, with pruning.

---

## 9. Simplified Payment Verification

It is possible to verify payments without running a full network node. A user needs only keep a copy of the block headers of the longest proof-of-work chain, which can be obtained by querying network nodes until convinced the longest chain is known, then verify that the transaction appears in a block whose header is part of this chain.

### 9.1 AxisSPVProof Structure

For TeslaChain SPV verification, we introduce the **AxisSPVProof** structure, which provides a compact, self-contained proof of AXIS transaction inclusion:

```
AxisSPVProof {
    CBlockHeader  header       // 144-byte AXIS block header
    uint256       txHash       // Hash of the transaction to verify
    uint256[]     merkleBranch // Merkle proof path from tx to block merkle root
    uint32        txIndex      // Transaction position in block's merkle tree
}
```

The `header` field contains the full 144-byte AXIS block header including `hashPrevAxisBlock` and `hashAxisMerkleRoot`. The `merkleBranch` and `txIndex` fields provide cryptographic proof that the transaction appears in the block's merkle tree, rooted at the standard `hashMerkleRoot` field.

### 9.2 Three-Part SPV Verification

TeslaChain SPV verification requires validating three independent, interlocking cryptographic constraints:

1. **Merkle Proof Verification**: Using `merkleBranch` and `txIndex`, recompute the merkle root from the transaction hash and verify it matches `header.hashMerkleRoot`. This proves the transaction is contained in the block.

2. **Proof-of-Work Verification**: Verify that `header.GetHash() < header.nBits` — the block header's hash satisfies the difficulty target encoded in `nBits`. This proves the block was genuinely produced by computational work.

3. **hashPrevAxisBlock Chain Verification**: Starting from the given AXIS header, traverse the skip-chain by following `hashPrevAxisBlock` links backward. For each AXIS header in the chain, verify its PoW and `hashAxisMerkleRoot` consistency. Terminate when reaching AXIS block #3, whose `hashPrevAxisBlock` must equal the GENESIS hash (zero). This transitive chain ensures the AXIS block is anchored to GENESIS and has not been tampered with.

These three checks are **mutually reinforcing**: the merkle proof proves transaction inclusion, the PoW proves the header is authentic Nakamoto consensus work, and the skip-chain proves the block cannot be reorganized without rewriting GENESIS. A fraudulent header would fail at least one of these checks.

### 9.3 SPV over P2P

The SPV client implementation in `spv_p2p.cpp` provides `AxisHeaderCache` for storing and looking up AXIS headers by height or hash, and `SPVP2PClient` for managing P2P connections and fetching headers via `GETAXISHEADERS` messages. SPV clients connect to AXIS-capable peers (those advertising `NODE_AXIS`), request AXIS headers using LINK-chain locators, and validate received headers against the three-part verification scheme before storing them in the local cache.

For TeslaChain, this extends naturally to AXIS blocks: once a transaction is confirmed in an AXIS block, it is mathematically guaranteed to be final if the AXIS proof is verified.

---

## 10. Combining and Splitting Value

To allow value to be split and combined, transactions contain multiple inputs and outputs. Normally there will be either a single input from a larger previous transaction or multiple inputs combining smaller amounts, and at most two outputs: one for the payment, one for returning change, if any.

---

## 11. Privacy

The traditional banking model achieves a level of privacy by limiting access to information to the parties involved and the trusted third party. TeslaChain, like Bitcoin, achieves privacy through public key anonymity by keeping public keys anonymous. The public can see someone is sending an amount to someone else, but without information linking the transaction to anyone.

---

## 12. Calculations

We consider the scenario of an attacker trying to generate an alternate chain faster than the honest chain. Even if this is accomplished, it does not allow arbitrary values to be created from nothing or take money that never belonged to the attacker, as nodes will not accept an invalid transaction as payment.

The race between the honest chain and an attacker chain can be characterized as a Binomial Random Walk. The event that the honest chain wins is the probability that the honest chain eventually overtakes the attacker.

For LINK blocks, TeslaChain maintains Bitcoin's security model. For AXIS blocks, the skip-chain creates an additional constraint: an attacker must also rewrite the entire AXIS chain, making the attack exponentially more difficult.

The probability of an attacker catching up diminishes exponentially as subsequent blocks are added. For AXIS blocks specifically, catching up is not merely difficult—it is mathematically impossible without rewriting GENESIS.

---

## 13. SLASH Penalties

The Triadic Consensus Protocol enforces AXIS structural integrity through a penalty mechanism called **SLASH**, which punishes nodes that produce blocks with invalid AXIS fields. SLASH penalties are enforced at the consensus layer and result in proportional coin destruction.

### 13.1 SLASH V1 — hashPrevAxisBlock Violations

**Trigger**: An AXIS block's `hashPrevAxisBlock` field does not correctly reference the preceding AXIS block in the skip-chain.

**Penalty**: 50% burn of the block reward.

**Detection**: This violation is detected during both P2P header reception (via `AXISHEADERS` or `headers` messages) and during full block validation. When receiving AXIS headers from peers, the node verifies that `hashPrevAxisBlock` points to the correct prior AXIS block (height H−3). A mismatch results in DoS scoring (+10) and rejection of the invalid header. At the consensus level, blocks confirmed with invalid `hashPrevAxisBlock` are subject to SLASH V1 enforcement.

**Rationale**: The `hashPrevAxisBlock` field is the skip-chain's integrity link. If it is wrong, the AXIS block is not properly anchored to its predecessor, breaking the transitive immutability chain. Burning 50% of the reward makes it economically catastrophic to mine atop an invalid AXIS block.

### 13.2 SLASH V2 — hashAxisMerkleRoot Violations

**Trigger**: An AXIS block's `hashAxisMerkleRoot` does not match the correctly computed cumulative merkle root of all AXIS blocks from GENESIS to this block.

**Penalty**: 25% burn of the block reward.

**Detection**: During full block validation, the node recomputes the AXIS merkle chain up to the current height and compares the result against `hashAxisMerkleRoot`. A mismatch triggers SLASH V2. At the P2P layer, headers with null `hashAxisMerkleRoot` are rejected with DoS scoring (+10).

**Rationale**: The `hashAxisMerkleRoot` commits to the entire AXIS history. An incorrect value means the block's commitment to prior AXIS blocks is invalid, undermining the skip-chain's cumulative integrity. The 25% burn is proportional to the severity — the skip-chain link remains intact, but the merkle commitment is broken.

### 13.3 Penalty Enforcement Summary

| Penalty | Violation | Burn % | DoS Score (P2P) |
|---------|-----------|--------|-----------------|
| SLASH V1 | Invalid `hashPrevAxisBlock` | 50% | +10 |
| SLASH V2 | Invalid `hashAxisMerkleRoot` | 25% | +10 |

Both penalties are enforced during coinbase transaction creation and block reward distribution. Full nodes that detect these violations during P2P header sync immediately ban the offending peer upon reaching the DoS threshold.

---

## 14. Formal Verification

The AXIS skip-chain protocol has been formally specified and verified using **TLA+** (Temporal Logic of Actions), providing mathematical certainty about the protocol's correctness beyond what testing alone can offer.

### 14.1 TLA+ Specification

The formal specification is located at `docs/formal/TeslaChainAxis.tla`. It models the AXIS skip-chain as a state machine with the following core variables:

- `GenesisHash`: The immutable GENESIS block identifier.
- `AXIS[i]`: The AXIS block at skip-chain position `i` (height `3 × (i+1)`).
- `Changed(h)`: A predicate indicating whether AXIS block at height `h` has been modified.

The specification defines the AXIS block structure with fields for height, hash, `hashPrevBlock`, `hashPrevAxisBlock`, and `hashAxisMerkleRoot`, mirroring the 144-byte header layout used in the implementation.

### 14.2 Four Invariants

The TLA+ specification establishes four critical invariants that hold for all reachable states:

1. **Inv1_AXIS_Contiguity**: Every AXIS block `AXIS_i` references `AXIS_{i-1}` via `hashPrevAxisBlock`. This guarantees the skip-chain forms a proper linked list back to GENESIS — no AXIS block can skip over its immediate predecessor.

2. **Inv2_GENESIS_Immutability**: The GENESIS block (height 0) is never modified. This is the foundational axiom from which all AXIS immutability is derived.

3. **Inv3_No_Skip_Violations**: AXIS blocks appear only at heights 3, 6, 9, 12... (multiples of 3). No AXIS block exists at a non-multiple-of-3 height, ensuring the skip-chain spacing is always consistent.

4. **Inv4_Chain_Finality**: Once an AXIS block is confirmed at sufficient depth, it cannot be modified without invalidating the entire AXIS chain back to GENESIS. This is a direct consequence of Inv1 and Inv2.

### 14.3 AXIS_IMMUTABILITY_THEOREM

The core theorem, proven in `docs/formal/AXIS_IMMUTABILITY_THEOREM.tla`, states:

```
Changed(AXIS_H) => Changed(AXIS_{H-3})
```

**Theorem (AXIS Immutability)**: If an AXIS block at height H is modified, then the AXIS block at height H−3 must also be modified.

**Proof strategy**:
- **Base case (H = 3)**: AXIS_3 references GENESIS via `hashPrevAxisBlock`. Since GENESIS is immutable (Inv2), AXIS_3 cannot change without also changing GENESIS. Therefore `Changed(AXIS_3) => Changed(GENESIS)`.
- **Inductive step**: Assume `Changed(AXIS_H) => Changed(AXIS_{H-3})` for all H up to some value. Consider AXIS_{H+3}: its `hashPrevAxisBlock` points to AXIS_H. If AXIS_{H+3} changes, its `hashPrevAxisBlock` must change to remain consistent with the new AXIS_H hash. But this means AXIS_H has changed — satisfying `Changed(AXIS_{H+3}) => Changed(AXIS_H)`. By the induction hypothesis, `Changed(AXIS_H) => Changed(AXIS_{H-3})`. Chaining these: `Changed(AXIS_{H+3}) => Changed(AXIS_H) => Changed(AXIS_{H-3})`.

By induction, modifying any AXIS block propagates all the way back to AXIS_3, which requires modifying GENESIS. This makes the entire AXIS chain **as immutable as GENESIS itself**.

### 14.4 Model Checking

The TLA+ specification is verified using **TLC** (the TLA+ model checker) and **TLAPS** (the TLA+ proof system). TLC exhaustively explores all reachable states up to a configurable `MaxHeight`, checking that all invariants hold. TLAPS provides machine-checked proofs for the theorems, eliminating the possibility of logical errors in the proof reasoning.

Verification checklist and model configurations are available in `docs/formal/TLA_CHECKLIST.md`.

---

## 15. Conclusion

We have proposed a system for electronic transactions without relying on trust. We started with the framework of coins made from digital signatures, which provides strong ownership control but is incomplete without a way to prevent double-spending.

To solve double-spending, we proposed a peer-to-peer network using proof-of-work. This network is robust in its simplicity: transactions are broadcast publicly, and nodes work on finding proof-of-work to extend the chain. The longest chain serves as proof of the most work and as evidence that it came from the largest pool of CPU power.

We extended this model with the Triadic Consensus Protocol (3-6-9), which designates every third block as an AXIS block forming an immutable sub-chain. We proved mathematically that AXIS blocks cannot be reorganized without rewriting the GENESIS block—making AXIS-finalized transactions immutable by pure mathematics, not merely economically irrational to attack.

The system is secure as long as honest nodes collectively control more CPU power than any cooperating group of attacker nodes.

---

## References

[1] W. Dai, "b-money," http://www.weidai.com/bmoney.txt, 1998.

[2] H. Massias, X.S. Avila, and J.-J. Quisquater, "Design of a Secure Timestamping Service with Minimal Trust Requirements," In 20th Symposium on Information Theory, 1999.

[3] S. Nakamoto, "Bitcoin: A Peer-to-Peer Electronic Cash System," https://bitcoin.org/bitcoin.pdf, 2008.

[4] Bitcoin Core Repository, https://github.com/bitcoin/bitcoin, 2024.

[5] V. Buterin and V. Griffith, "Casper the Friendly Finality Gadget," arXiv:1710.09437, 2017.

[6] TeslaChain Formal Verification Team, "TeslaChain AXIS Skip-Chain Formal Specification (TLA+)," `docs/formal/TeslaChainAxis.tla` and `docs/formal/AXIS_IMMUTABILITY_THEOREM.tla`, TeslaChain Core Repository, 2026.

---

**Acknowledgments**

We thank Satoshi Nakamoto for inventing Bitcoin, the Bitcoin Core team for maintaining the reference implementation, and Dr. Craig Wright for demonstrating the potential of BSV.

---

*TeslaChain: Bitcoin, but with deterministic finality.*

**Repository**: https://github.com/captainsvbot/TeslaChain-369

This whitepaper is subject to review as it has not been audited and information may not be consistent with what teslachain offers.
The Author reserves all rights to make changes as neccessary if information is not factual based on external review.
