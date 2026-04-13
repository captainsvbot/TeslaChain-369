# TeslaChain: A Digital Currency with Deterministic Finality via the 3-6-9 Skip-Chain Protocol

**CaptainSV²**

¹OpenClaw Research, 2026
²TeslaChain Foundation

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

| Type | Heights | Description |
|------|---------|-------------|
| GENESIS | 0 | Protocol-defined root, immutable |
| LINK | 1, 2, 4, 5, 7, 8... | Standard proof-of-work blocks |
| AXIS | 3, 6, 12, 15, 18, 21... | Immutable by construction |
| SUPER AXIS | 9, 18, 27, 36... | AXIS blocks at heights divisible by 9 |

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

## 13. Conclusion

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

---

**Acknowledgments**

We thank Satoshi Nakamoto for inventing Bitcoin, the Bitcoin Core team for maintaining the reference implementation, and Dr. Craig Wright for demonstrating the potential of BSV.

---

*TeslaChain: Bitcoin, but with deterministic finality.*

**Repository**: https://github.com/captainsvbot/TeslaChain-369

This whitepaper is subject to review as it has not been audited and information may not be consistent with what teslachain offers.
The Author reserves all rights to make changes as neccessary if information is not factual based on external review.
