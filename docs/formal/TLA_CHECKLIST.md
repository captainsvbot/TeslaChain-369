# TLA+ Verification Checklist — TeslaChain AXIS Skip-Chain

## Model Checking (TLC)

Run with:
```bash
cd docs/formal/TeslaChainAxis.toolbox
# Or use: java -cp /path/to/tla2tools.jar tlc.TLC TeslaChainAxis
```

### Small Scale (Development/Debugging)

- [ ] **Model check for n=3** (genesis + 1 AXIS at height 3)
  - MaxHeight = 3
  - Expected: All invariants pass
  - Command: `tlc TeslaChainAxis -maxheight 3`

- [ ] **Model check for n=6** (genesis + 2 AXIS at heights 3, 6)
  - MaxHeight = 6
  - Expected: All invariants pass
  - Command: `tlc TeslaChainAxis -maxheight 6`

- [ ] **Model check for n=9** (genesis + 3 AXIS at heights 3, 6, 9)
  - MaxHeight = 9
  - Expected: All invariants pass
  - Command: `tlc TeslaChainAxis -maxheight 9`

- [ ] **Model check for n=12** (genesis + 4 AXIS at heights 3, 6, 9, 12)
  - MaxHeight = 12
  - Expected: All invariants pass
  - Command: `tlc TeslaChainAxis -maxheight 12`

- [ ] **Model check for n=24** (genesis + 8 AXIS blocks)
  - MaxHeight = 24
  - For nightly CI if TLC runs < 5 min

### Invariant Tests (Intentional Violation Detection)

- [ ] **Test Inv1 detection** — introduce a skip-chain violation, verify TLC catches it
  - Enable `ViolateInv1` action
  - Expected: TLC reports Inv1_AXIS_Contiguity violated

- [ ] **Test Inv2 detection** — try to modify GENESIS block
  - Expected: TLC reports Inv2_GENESIS_Immutability violated

- [ ] **Test Inv3 detection** — create AXIS block at non-multiple-of-3 height
  - Expected: TLC reports Inv3_No_Skip_Violations violated

### Performance Benchmarks

- [ ] Measure TLC runtime for n=12, n=24, n=48
- [ ] Record state space size for each
- [ ] Set up CI gate: n=12 must complete in < 10 min

---

## TLAPS Proof Verification

### Phase 1: Proven Invariants

- [ ] **TLAPS proof of AXIS contiguity** (Inv1)
  - File: `docs/formal/proofs/Inv1_AXIS_Contiguity.tla`
  - Goal: Prove that every AXIS_i references AXIS_{i-1}

- [ ] **TLAPS proof of GENESIS immutability** (Inv2)
  - File: `docs/formal/proofs/Inv2_GENESIS_Immutability.tla`
  - Goal: Prove that height 0 never changes in any execution

- [ ] **TLAPS proof of skip-chain linked-list rule** (Inv3)
  - File: `docs/formal/proofs/Inv3_No_Skip_Violations.tla`
  - Goal: Prove that AXIS blocks only exist at heights 3, 6, 9, 12...

- [ ] **TLAPS proof of chain finality** (Inv4)
  - File: `docs/formal/proofs/Inv4_Chain_Finality.tla`
  - Goal: Prove that sealed AXIS blocks cannot be modified

### Phase 2: The Core Theorem

- [ ] **TLAPS proof of AXIS immutability theorem**
  - File: `docs/formal/AXIS_IMMUTABILITY_THEOREM.tla`
  - Goal: Prove that changing AXIS_H implies changing AXIS_{H-3}

- [ ] **Inductive proof of AXIS immutability theorem**
  - Use structural induction on the AXIS chain
  - Base case: H = 3 (AXIS_3 anchored to GENESIS)
  - Inductive step: H → H+3

### Phase 3: Corollaries

- [ ] **Proof that AXIS chain is anchored to GENESIS**
  - Corollary: Changed(AXIS_H) => Changed(AXIS_3)

- [ ] **Proof that no AXIS block can be modified in isolation**
  - Corollary: Modifying AXIS_H requires modifying all AXIS blocks from 3 to H

---

## Extensions & Future Work

- [ ] **PlusCal spec** — translate to PlusCal for more readable algorithm specs
- [ ] **Config spec** — model TeslaChain node config, network partition scenarios
- [ ] ** Byzantine fault tolerance test** — simulate up to f byzantine nodes
- [ ] **Compare against Bitcoin** — model both chains, show AXIS advantage
- [ ] **Property-based testing** — generate random valid/invalid AXIS chains

---

## CI Integration

- [ ] GitHub Actions workflow for TLC model checking
- [ ] Nightly proof verification with TLAPS
- [ ] Badge: "TLA+ Verified" in README

---

## Resources

- TLA+ Toolbox: https://lamport.azurewebsites.net/tla/toolbox.html
- TLAPS: https://tla.msr-inria.inria.fr/tlaps/content/Home.html
- Main TLA+ site: https://lamport.azurewebsites.net/tla/tla.html
- "Specifying Systems" (free PDF): https://lamport.azurewebsites.net/tla/specing-systems.html