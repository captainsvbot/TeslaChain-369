# TeslaChain AXIS Skip-Chain — Formal Verification

Formal specification and verification of TeslaChain's 3-6-9 AXIS skip-chain consensus protocol using TLA+ and TLAPS.

## Quick Start

### 1. Install TLA+ Tools

**macOS (Homebrew):**
```bash
brew install tla-plus
```

**Manual download:**
```bash
# Download from lamport.azurewebsites.net/tla/toolbox.html
# Or use the JAR directly:
java -jar tla2tools.jar
```

**TLAPS (proof system):**
```bash
# Download from https://tla.msr-inria.inria.fr/tlaps/content/Home.html
```

### 2. Run TLC Model Checker

```bash
cd /path/to/teslachain-core/docs/formal

# Check if TLC is installed:
java -cp /path/to/tla2tools.jar tlc.TLC

# Run with the toolbox model:
# (Open TLA+ Toolbox → File → Open Spec → TeslaChainAxis.tla
#  Then create a model with MaxHeight=12, run the model checker)

# From command line (example):
java -cp tla2tools.jar tlc.TLC TeslaChainAxis \
  -constants GenesisHash="GENESIS",MaxHeight=12 \
  -deadlock \
  -workers 4
```

### 3. Run TLAPS Proof Checker

```bash
# Prove theorems in AXIS_IMMUTABILITY_THEOREM.tla
cd docs/formal
tlaps AXIS_IMMUTABILITY_THEOREM.tla
```

---

## Overview

### What is AXIS?

AXIS (Anchor-linked EXtensible Integrity System) is TeslaChain's skip-chain mechanism. Every 3rd block (heights 3, 6, 9, 12, ...) is an **AXIS block** that:

1. Links to the previous AXIS block via `hashPrevAxisBlock`
2. Forms an immutable linked list back to GENESIS (height 0)
3. Provides chain finality guarantees beyond regular Nakamoto consensus

### Key Invariants

| Invariant | Description |
|-----------|-------------|
| `Inv1_AXIS_Contiguity` | Every AXIS_i references AXIS_{i-1} via `hashPrevAxisBlock` |
| `Inv2_GENESIS_Immutability` | The GENESIS block (height 0) never changes |
| `Inv3_No_Skip_Violations` | AXIS blocks only appear at heights 3, 6, 9, 12... |
| `Inv4_Chain_Finality` | Once an AXIS block is deep enough, it cannot be modified |

---

## File Structure

```
docs/formal/
├── TeslaChainAxis.tla            # Main TLA+ specification
├── AXIS_IMMUTABILITY_THEOREM.tla # Core theorem + proofs
├── README_FORMAL.md              # This file
├── TLA_CHECKLIST.md              # Verification checklist
└── TeslaChainAxis.toolbox/       # TLA+ Toolbox project files
    ├── .project                  # Toolbox project config
    └── TeslaChainAxis_12/        # Model config for MaxHeight=12
        ├── MC.tla               # Model-specific constants
        └── MC.cfg               # TLC configuration
```

---

## The Core Theorem: AXIS Immutability

```
Changed(AXIS_H) => Changed(AXIS_{H-3})
```

**Informal statement:** If you modify an AXIS block at height H, you must also modify the AXIS block at height H-3.

**Proof strategy:**
- **Base case (H=3):** AXIS_3 references GENESIS. GENESIS is immutable, so AXIS_3 cannot change.
- **Inductive step:** Assume changing AXIS_H requires changing AXIS_{H-3}. Show that changing AXIS_{H+3} requires changing AXIS_H.

**Consequences:**
1. Modifying AXIS_H requires modifying all AXIS blocks from 3 to H
2. The entire AXIS chain is as immutable as GENESIS itself
3. The skip-chain provides similar finality to long chain reorganizations but with fewer messages

---

## How to Verify

### Model Checking (TLC)

Model checking exhaustively explores all possible states up to `MaxHeight`.

```bash
# Start with small models:
MaxHeight = 3   # genesis + 1 AXIS
MaxHeight = 6   # genesis + 2 AXIS
MaxHeight = 9   # genesis + 3 AXIS
MaxHeight = 12  # genesis + 4 AXIS (recommended first run)
```

### Proving (TLAPS)

For mathematically rigorous proofs:

```bash
# Prove the AXIS immutability theorem
tlaps AXIS_IMMUTABILITY_THEOREM.tla

# Prove all invariants
tlaps TeslaChainAxis.tla
```

---

## Detecting Bugs

The spec includes a `ViolateInv1` action to test detection. To test:

1. Enable `ViolateInv1` in `Next`
2. Run TLC
3. Verify it reports `Inv1_AXIS_Contiguity` violation

Example bugs to test:
- Creating AXIS at non-multiple-of-3 height → Inv3 violation
- AXIS block not referencing previous AXIS → Inv1 violation
- Modifying GENESIS → Inv2 violation

---

## Extending the Spec

### Adding New Invariants

Add to `TeslaChainAxis.tla`:

```tla
Inv_N_NewInvariant ==
  \* your invariant definition here

\* Add to Next and ConsistencyCheck
```

### Modeling Network Behavior

Add variables:
- `nodeState` — state of each node
- `messagesInFlight` — network message queue
- `forkHeight` — height at which a fork occurred

### Modeling Byzantine Faults

Add to `Next`:
```tla
\/ ByzantineAddInvalidAXIS
\/ ByzantineWithholdAXIS
\/ ByzantinePublishOldBlock
```

---

## Similar Projects

- **Bitcoin** — Nakamoto consensus (no skip-chains)
  - TLA+ spec: https://github.com/plor/tla-bitcoin

- **Ethereum 2.0** — Beacon chain finality gadget
  - TLA+ spec: https://github.com/ethereum/formal-verification

- **Tendermint** — BFT consensus with similar finality concepts
  - TLA+ spec: https://github.com/tendermint/tendermint/tree/master/spec

- **Avalanche** — Snow* family consensus
  - TLA+ spec: https://github.com/avalanche-foundation/avalanche-network-runner

---

## Troubleshooting

### "tlasm: command not found"
Install via Homebrew: `brew install tla-plus`
Or download JAR from https://lamport.azurewebsites.net/tla/toolbox.html

### TLC runs out of memory
Reduce `MaxHeight` or limit workers: `-workers 2 -maxheap 256`

### TLAPS can't find proof steps
TLAPS requires Isabelle. See: https://tla.msr-inria.inria.fr/tlaps/content/Download/index.html

---

## References

- Nakamoto, S. (2008). Bitcoin: A Peer-to-Peer Electronic Cash System
- Leslie Lamport's "Specifying Systems" (free PDF): https://lamport.azurewebsites.net/tla/specing-systems.html
- TLA+ expressiveness: https://en.wikipedia.org/wiki/TLA%2B
- PlusCal: https://lamport.azurewebsites.net/tla/pluscal.html

---

## License

Same as TeslaChain core (MIT License)

## Authors

TeslaChain Formal Verification Team — 2026-04-13