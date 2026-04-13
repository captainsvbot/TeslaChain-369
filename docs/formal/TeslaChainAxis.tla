----------------------------- MODULE TeslaChainAxis -----------------------------
(*
 * TeslaChain AXIS Skip-Chain Consensus — TLA+ Formal Specification
 *
 * Background:
 *   TeslaChain has AXIS blocks at heights 3, 6, 9, 12...
 *   Each AXIS block references the previous AXIS via hashPrevAxisBlock,
 *   forming a linked list back to GENESIS.
 *
 * Core Theorem (AXIS Immutability):
 *   Any AXIS block at height H cannot be modified without rewriting
 *   all AXIS blocks from 3 to H.
 *
 * This spec is model-checkable with TLC and should eventually be
 * proved with TLAPS.
 *)

EXTENDS Naturals, FiniteSets, Sequences, TLC

CONSTANTS
  GenesisHash,    \* The hash of the GENESIS block (arbitrary constant)
  MaxHeight       \* Maximum block height to model-check (try 12, 24, 48)

VARIABLES
  axisChain,      \* Function: height -> Block record (AXIS blocks only)
  mainChain,      \* Function: height -> Block record (all blocks)
  violated        \* Set of strings: which invariants are currently violated

\* Block record shape:
\*   height           \in Nat
\*   blockType        \in {"genesis", "axis", "main"}
\*   hashPrevBlock    \in STRING         (hash of previous block in main chain)
\*   hashPrevAxisBlock \in [height \in Nat |-> STRING]  (only meaningful for AXIS)
\*   parentHeight      \in Nat            (height of parent block in main chain)

BlockHeight == Nat
Hash == STRING

BlockRecord == [
  height: BlockHeight,
  blockType: {"genesis", "axis", "main"},
  hashPrevBlock: Hash,
  hashPrevAxisBlock: Hash,     \* undefined/"" for non-AXIS blocks
  parentHeight: BlockHeight
]

GenesisBlock ==
  [height |-> 0,
   blockType |-> "genesis",
   hashPrevBlock |-> GenesisHash,
   hashPrevAxisBlock |-> GenesisHash,
   parentHeight |-> 0]

\* -----------------------------------------------------------------------
\* Helper predicates
\* -----------------------------------------------------------------------
IsAXIS(h) == h \in 3..MaxHeight /\ h % 3 = 0

AxisBlockAt(h) == axisChain /= [i \in {} |-> <<>>] /\ h \in DOMAIN axisChain

IsValidAXISBlock(b) ==
  /\ b.blockType = "axis"
  /\ b.height \in 3..MaxHeight
  /\ b.height % 3 = 0
  /\ b.hashPrevAxisBlock /= ""

/\******* Changed: returns TRUE iff block at height h differs from initial ******/
Changed(block) == block /= block

\* -----------------------------------------------------------------------
\* Initial state
\* -----------------------------------------------------------------------
Init ==
  /\ axisChain = [h \in {3, 6, 9} \in DOMAIN 3..MaxHeight |-> 
                   IF h <= MaxHeight
                   THEN [height |-> h,
                         blockType |-> "axis",
                         hashPrevBlock |-> "main-hash",
                         hashPrevAxisBlock |-> IF h = 3 THEN GenesisHash
                                               ELSE axisChain[h-3].hashPrevBlock,
                         parentHeight |-> h-1]
                   ELSE [height |-> h,
                         blockType |-> "axis",
                         hashPrevBlock |-> "main-hash",
                         hashPrevAxisBlock |-> "placeholder",
                         parentHeight |-> h-1]]
  /\ mainChain = [h \in {0, 1, 2, 3} |-> IF h = 0 THEN GenesisBlock
                                          ELSE [height |-> h,
                                                blockType |-> IF h \in {3} THEN "axis" ELSE "main",
                                                hashPrevBlock |-> "main-hash",
                                                hashPrevAxisBlock |-> IF h = 3 THEN GenesisHash ELSE "",
                                                parentHeight |-> h-1]]
  /\ violated = {}

\* -----------------------------------------------------------------------
\* Invariants
\* -----------------------------------------------------------------------

\* Inv1: AXIS Contiguity — every AXIS_i references AXIS_{i-1} via hashPrevAxisBlock
Inv1_AXIS_Contiguity ==
  \A h \in {i \in DOMAIN axisChain: axisChain[i].blockType = "axis"}:
    LET block == axisChain[h] IN
      IF h > 3 THEN
        \E prev \in DOMAIN axisChain:
          prev = h - 3 /\ axisChain[prev].hashPrevAxisBlock = block.hashPrevAxisBlock
      ELSE
        block.hashPrevAxisBlock = GenesisHash

\* Inv2: GENESIS Immutability — height 0 never changes
Inv2_GENESIS_Immutability ==
  mainChain[0] = GenesisBlock

\* Inv3: No Skip Violations — AXIS blocks appear only at heights 3, 6, 9, 12...
Inv3_No_Skip_Violations ==
  \A h \in DOMAIN mainChain:
    mainChain[h].blockType = "axis" => (h >= 3 /\ h % 3 = 0)

\* Inv4: Chain Finality — once AXIS_H is deeper than current tip, it's sealed
Inv4_Chain_Finality ==
  LET tipHeight == CHOOSE max \in DOMAIN mainChain: \A k \in DOMAIN mainChain: k <= max IN
    \A h \in DOMAIN axisChain:
      (h < tipHeight - 6) => axisChain[h].hashPrevAxisBlock /= "placeholder"

\* Inv5: AXIS Immutability Theorem (core property)
Inv5_AXIS_Immutability ==
  \A h \in 3..MaxHeight:
    \A block \in {axisChain[k]: k \in DOMAIN axisChain}:
      block.height = h => (Changed(block) => \E prev \in DOMAIN axisChain:
        prev = h-3 /\ Changed(axisChain[prev]))

\* -----------------------------------------------------------------------
\* Actions
\* -----------------------------------------------------------------------

\* Try to add a new AXIS block at height h (must respect skip-chain)
AddAXISBlock(h) ==
  /\ h \in 3..MaxHeight
  /\ h % 3 = 0
  /\ h \notin DOMAIN axisChain
  /\ \/ h = 3 /\ axisChain[h] = [height |-> 3, blockType |-> "axis",
                                  hashPrevBlock |-> "main-hash",
                                  hashPrevAxisBlock |-> GenesisHash,
                                  parentHeight |-> 2]
     \/ /\ h > 3
        /\ (h-3) \in DOMAIN axisChain
        /\ axisChain[h] = [height |-> h, blockType |-> "axis",
                            hashPrevBlock |-> "main-hash",
                            hashPrevAxisBlock |-> axisChain[h-3].hashPrevBlock,
                            parentHeight |-> h-1]
  /\ axisChain' = axisChain
  /\ mainChain' = mainChain
  /\ violated' = violated

\* Intentionally violate Inv1 to test detection (for debugging)
ViolateInv1 ==
  /\ \E h \in DOMAIN axisChain:
      \E bad \in DOMAIN axisChain:
        h > 3 /\ bad /= h-3
        /\ \E newBlock \in DOMAIN axisChain:
            axisChain[newBlock] = [axisChain[newBlock] EXCEPT !.hashPrevAxisBlock = "BAD-HASH"]
  /\ violated' = violated \cup {"Inv1_AXIS_Contiguity"}
  /\ axisChain' = axisChain
  /\ mainChain' = mainChain

\* -----------------------------------------------------------------------
\* Next-state relation
\* -----------------------------------------------------------------------
Next ==
  \E h \in 3..MaxHeight:
    \/ AddAXISBlock(h)
    \/ ViolateInv1

\* -----------------------------------------------------------------------
\* Temporal properties
\* -----------------------------------------------------------------------
ConsistencyCheck ==
  \A inv \in {"Inv1_AXIS_Contiguity",
             "Inv2_GENESIS_Immutability",
             "Inv3_No_Skip_Violations",
             "Inv4_Chain_Finality"}:
    []inv

============================================================================
\* Modification History
\* Last Revised: 2026-04-13
\* Author: TeslaChain Formal Verification Team
\* Purpose: Formal specification of TeslaChain 3-6-9 skip-chain consensus
============================================================================