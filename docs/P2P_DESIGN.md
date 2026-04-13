# TeslaChain P2P Networking Design — 3-6-9 Protocol

> **Status:** Design Document · Not Yet Implemented
> **Author:** TeslaChain Core Team
> **Branch:** `tesla369/p2p-design` (from `tesla369/main`)
> **Target:** `tesla369/main`
> **Last Updated:** 2026-04-13

---

## 1. Background & Motivation

### 1.1 The Problem

TeslaChain's 3-6-9 protocol introduces **AXIS blocks** at heights 3, 6, 9, 12... Each AXIS block carries two extra fields in its 144-byte header:

- `hashPrevAxisBlock` — pointer to the previous AXIS block (skip-chain link)
- `hashAxisMerkleRoot` — cumulative merkle root of all AXIS blocks from GENESIS → current AXIS

The current implementation works via **RPC only**. RPC is a local control interface — it is **not a blockchain network**. Without P2P networking, TeslaChain is a local ledger simulator.

### 1.2 The 144-Byte Header Problem

Bitcoin's P2P protocol hardcodes **80-byte block headers**. Every wire format, serialization logic, and message handler assumes 80 bytes. TeslaChain has **144-byte headers** (80 + 64 bytes for AXIS fields).

We cannot simply send 144-byte headers over the existing `headers` message — peers would interpret 64 bytes of data as transaction count or other fields, corrupting the protocol.

### 1.3 Design Goals

1. **Full P2P networking** for AXIS block headers and blocks
2. **Backward compatibility** with Bitcoin's P2P protocol (SPV clients, existing tooling)
3. **AXIS-first sync** — lightweight header-only sync for SPV clients
4. **DoS resilience** — validate AXIS fields before trusting peers
5. **Minimal protocol pollution** — don't break existing Bitcoin P2P message parsing

---

## 2. Current Architecture

### 2.1 What Exists

| Component | File | Status |
|-----------|------|--------|
| Extended Header (144B) | `src/primitives/block.h` `CBlockHeader` | ✅ Implemented |
| AXIS Validation | `src/validation.cpp` ~line 4122 | ✅ Implemented |
| RPC Interface | `src/rpc/` | ✅ Implemented |
| Standard P2P (80B headers) | `src/net_processing.cpp`, `src/protocol.cpp` | ✅ Works for LINK blocks |

### 2.2 What Is Missing

- No P2P messages for requesting/relaying AXIS headers
- No AXIS-specific node discovery
- No AXIS header validation at P2P layer (only in full validation)
- No SPV proof support for AXIS

---

## 3. P2P Message Design

### 3.1 New AXIS-Specific Messages

We introduce **4 new P2P messages** in the `NetMsgType` namespace. All messages use the standard Bitcoin P2P framing (4-byte message start, 12-byte type, 4-byte length, 4-byte checksum).

#### 3.1.1 `GET_AXIS_HEADERS`

**Purpose:** Request AXIS block headers from a peer. Analogous to Bitcoin's `getheaders`, but **AXIS blocks only**.

```
Direction:  → (peer)
Serializer: CBlockLocator (hashes of LINK blocks) + uint256 (stop hash)
```

**Wire Format:**
```
[4 bytes]  message start (network magic)
[12 bytes] "getaxishead" (padded to 12)
[4 bytes]  payload length (little-endian uint32)
[4 bytes]  checksum (sha256d first 4 bytes)
Payload:
  [compactSize] number of block locators (max 101)
  [n × 32 bytes] block locator hashes (LINK chain tip → GENESIS)
  [32 bytes]     stop hash (uint256, zero = no stop)
```

**Client Behavior:**
- Constructs a `CBlockLocator` from the **LINK chain** (not AXIS chain). This is intentional — the peer will find the first AXIS block after the locator's latest common block.
- Sends `uint256(0)` as `hashStop` to receive all AXIS headers from that point.
- Maximum 2000 AXIS headers per response (configurable).

**Server Behavior:**
- Find the first AXIS block whose `hashPrevBlock` connects to the locator.
- Respond with AXIS headers sequentially from that point.

> **Design Note:** We use LINK chain locators because SPV clients know only the LINK chain. They cannot build AXIS chain locators without first syncing the AXIS chain — a chicken-and-egg problem. By using LINK locators, SPV clients can still request AXIS headers by referencing their best-known LINK block.

**Error Handling:**
- If locator is invalid or points to unknown block → respond with empty `AXIS_HEADERS`.
- If peer is far behind → respond with empty `AXIS_HEADERS` (no punishment).

#### 3.1.2 `AXIS_HEADERS`

**Purpose:** Respond to `GET_AXIS_HEADERS` with AXIS block headers.

```
Direction:  ← (peer)
Payload:    std::vector<CBlockHeader> — but only AXIS blocks (144 bytes each)
```

**Wire Format:**
```
[compactSize]  count of AXIS headers (max 2000)
[n × 144 bytes] AXIS block headers (full 144 bytes each)
```

> **Critical:** This is **not** the same as Bitcoin's `headers` message. Bitcoin's `headers` uses 80-byte headers and a trailing compactSize tx count (usually 0 for header-only responses). Our `AXIS_HEADERS` sends **pure 144-byte headers** with **no tx count suffix** — this avoids ambiguity with Bitcoin's wire format.

**Why 144 bytes matters:**
```
Bitcoin headers message (for reference):
  [compactSize] count
  [n × 80 bytes] block headers
  [n × compactSize] tx count (usually 0)

Our AXIS_HEADERS message:
  [compactSize] count
  [n × 144 bytes] block headers  ← No trailing tx count!
```

Without the tx count suffix, 144-byte headers **cannot** be mistaken for Bitcoin headers by a Bitcoin node (which would try to read an 80-byte header, then fail to parse the next bytes correctly). This is intentional protocol separation — our AXIS messages will simply be **ignored** by Bitcoin nodes (unknown message type) and vice versa.

#### 3.1.3 `GET_AXIS_BLOCKS`

**Purpose:** Request full AXIS blocks by header hash (for nodes that need full block data, not just headers).

```
Direction:  → (peer)
Payload:    std::vector<uint256> block hashes + std::vector<uint256> expected merkle roots (optional)
```

**Wire Format:**
```
[compactSize]  count of requested blocks
[n × 32 bytes] block hashes
// Optionally, for merkle proof verification:
[compactSize]  count of merkle roots
[n × 32 bytes] expected merkle roots
```

> **Design Note:** Unlike `getdata` which can request mixed tx/block types, `GET_AXIS_BLOCKS` is **AXIS-specific**. It never returns LINK blocks.

#### 3.1.4 `AXIS_BLOCKS`

**Purpose:** Respond to `GET_AXIS_BLOCKS` with full AXIS blocks.

```
Direction:  ← (peer)
Payload:    std::vector<CBlock> — full serialized blocks
```

**Wire Format:**
```
[compactSize]  count of blocks
[n × variable] serialized CBlock objects
```

### 3.2 Updated Existing Messages

#### 3.2.1 `getheaders` / `headers` — AXIS Field Validation

Bitcoin's standard `getheaders`/`headers` messages work on LINK blocks. We must **extend validation** when processing these messages:

**On receiving `headers` from untrusted peer:**

1. **Standard Bitcoin checks** (unchanged):
   - Header parses correctly
   - Hash meets PoW target
   - `hashPrevBlock` connects to known chain
   - Block is not duplicate

2. **New AXIS checks** (added):
   ```
   if (height % 3 == 0) {  // AXIS block
       if (hashPrevAxisBlock.IsNull()) → REJECT (DoS: +1)
       if (hashAxisMerkleRoot.IsNull()) → REJECT (DoS: +1)
       
       // Validate hashPrevAxisBlock points to previous AXIS block
       prevAxis = FindAxisBlockByHeight(height - 3)
       if (hashPrevAxisBlock != prevAxis.hash) → REJECT (DoS: +10)
       
       // Validate hashAxisMerkleRoot
       computedMerkle = BuildAxisMerkleChain(height)
       if (hashAxisMerkleRoot != computedMerkle) → REJECT (DoS: +10)
   } else {  // LINK block
       if (!hashPrevAxisBlock.IsNull()) → REJECT (DoS: +1)
       if (!hashAxisMerkleRoot.IsNull()) → REJECT (DoS: +1)
   }
   ```

These checks are **lightweight** — they only verify the AXIS fields are present and internally consistent. Full AXIS merkle root computation (which requires reading all prior AXIS blocks) happens only when the full block is received.

#### 3.2.2 `inv` / `getdata` — AXIS Inventory Types

We add new inventory message types:

```cpp
// In protocol.h — extended GetDataMsg enum
enum GetDataMsg : uint32_t {
    // ... existing values ...
    
    // TeslaChain AXIS types
    MSG_AXIS_BLOCK      = 6,  // Full AXIS block
    MSG_AXIS_HEADER     = 7,  // AXIS block header only (144 bytes)
};
```

New `CInv` patterns:
```cpp
// Helper methods in CInv class
bool IsMsgAxisBlk() const { return type == MSG_AXIS_BLOCK; }
bool IsMsgAxisHdr() const { return type == MSG_AXIS_HEADER; }
```

**`inv` message behavior:**
- Nodes **MAY** announce new AXIS blocks via `inv(MSG_AXIS_BLOCK)` just like regular blocks.
- SPV clients can announce via `inv(MSG_AXIS_HEADER)` for lighter announcements.

---

## 4. Node Discovery

### 4.1 Bootstrap Strategy

For launch, we use a **tiered bootstrap approach**:

#### Tier 1: Hardcoded Seed Nodes

```cpp
// In chainparamsseeds.h or chainparams.cpp
static const char* const tesla369_mainnet_seed_nodes[] = {
    "seed.teslachain.org",
    "seed2.teslachain.org",
    "35.187.242.18",      // Example IP
    "104.248.142.92",
};
```

#### Tier 2: DNS Seeds

Similar to Bitcoin's DNS seed mechanism:

```
# DNS seed records (configurable)
tesla-dnsseed.teslachain.org  →  returns A/AAAA records of seed nodes
```

**DNS Seed Provider Implementation:**
```cpp
// In net.cpp — CConnman::Start()
void CConnman::StartExtraBlockRelayPeers() {
    // Existing: starts block relay peers
    
    // NEW: Start AXIS relay peers
    if (m_chainparams DNSSeedsAXIS().size() > 0) {
        // Query AXIS DNS seeds for initial AXIS peer discovery
    }
}
```

#### Tier 3: Permanent Seed List (Backup)

```cpp
// In chainparams.cpp
const std::vector<std::string> CChainParams::strDNSSeedsAXIS = {
    "axis-seed.teslachain.org",
};
```

### 4.2 Peer Address Gossip

Standard Bitcoin `addr`/`addrv2` messages work for AXIS peers — no changes needed. Nodes gossip their `ServiceFlags` which will include:

```cpp
// New service flag for AXIS
enum ServiceFlags : uint64_t {
    // ... existing flags ...
    
    // TeslaChain: Node supports AXIS protocol
    NODE_AXIS = (1 << 12),
};
```

Nodes with `NODE_AXIS` set will be preferred for AXIS-related requests.

### 4.3 SPV Client Discovery

SPV clients connecting to the network need to find AXIS-capable peers. Flow:

```
1. SPV Client → connects to any peer (via DNS seed or hardcoded)
2. SPV Client → sends VERSION (advertises NODE_AXIS if desired)
3. SPV Client → sends GETADDR
4. Peer → responds with ADDR containing NODE_AXIS peers
5. SPV Client → connects to AXIS-capable peers
6. SPV Client → sends GET_AXIS_HEADERS with LINK locator
```

---

## 5. Header Synchronization

### 5.1 AXIS Header Sync Flow (Sequence Diagram)

```
┌─────────────┐                    ┌─────────────┐
│  Full Node  │                    │  SPV Client │
└──────┬──────┘                    └──────┬──────┘
       │                                  │
       │  ← TCP Connection (version/verack) →
       │                                  │
       │                                  │  1. GET_AXIS_HEADERS
       │                                  │     locator=[LINK hash at height 0]
       │                                  │     hashStop=0
       │  ───────────────────────────────→│
       │                                  │
       │  2. AXIS_HEADERS                 │
       │     count=1                      │
       │     header[0]=AXIS block #3      │
       │  ←───────────────────────────────│
       │                                  │
       │  3. Validate AXIS #3:            │
       │     - Check PoW                  │
       │     - Check hashPrevAxisBlock=0 │
       │     - Check hashAxisMerkleRoot   │
       │     - Store in block index       │
       │                                  │
       │  4. GET_AXIS_HEADERS             │
       │     locator=[hash of AXIS #3]    │
       │  ←───────────────────────────────│
       │                                  │
       │  5. AXIS_HEADERS                 │
       │     count=N (up to 2000)         │
       │     headers[AXIS #6, #9, #12...]│
       │  ←───────────────────────────────│
       │                                  │
       │  6. Validate each AXIS:          │
       │     - Check PoW                  │
       │     - Check hashPrevAxisBlock   │
       │     - Check hashAxisMerkleRoot   │
       │     - Accumulate DoS score for   │
       │       invalid headers            │
       │                                  │
       │  7. (Continue until synced)     │
       │                                  │
```

### 5.2 SPV Header-Only Sync

SPV clients only need AXIS headers — **not full blocks**. This is efficient because:

- AXIS headers are 144 bytes each
- After 100,000 blocks, total AXIS header chain = ~4.8 MB
- LINK block headers are NOT needed for AXIS verification

```
Sync timeline:
  - Total blocks:     100,000
  - AXIS blocks:      33,333 (every 3rd)
  - AXIS headers:     ~4.8 MB (33,333 × 144 bytes)
  - LINK headers:     ~2.6 MB (66,666 × 80 bytes... but SPV doesn't need these)
  
  Total SPV storage: ~4.8 MB (vs ~7.4 MB for full headers)
```

### 5.3 Full Node Header Sync (Headers-First)

Full nodes use AXIS headers sync as the **first phase** of block sync:

```
Phase 1: AXIS Headers Sync
  GET_AXIS_HEADERS → AXIS_HEADERS → validate → store in block index

Phase 2: Block Sync (parallel)
  GET_AXIS_BLOCKS → AXIS_BLOCKS → full validation → connect transactions
```

This is analogous to Bitcoin's headers-first sync (BIP 130).

---

## 6. Peer AXIS Validation (DoS Protection)

### 6.1 Validation Rules

When receiving AXIS data via P2P, validate **before trusting**:

#### On `GET_AXIS_HEADERS` ( outbound):

| Check | Action |
|-------|--------|
| Locator size > 101 | Disconnect (no DoS score) |
| Node is still loading blocks | Return empty response |
| Node has insufficient chainwork | Return empty response |

#### On `AXIS_HEADERS` ( inbound):

| Violation | DoS Score | Reason |
|-----------|-----------|--------|
| Header has invalid PoW | +50 | Proof-of-work is computationally expensive to fake |
| Header hashPrevBlock not in chain | +1 | Minor: may be due to sync lag |
| AXIS header: hashPrevAxisBlock is null | +10 | AXIS blocks MUST have this field |
| AXIS header: hashAxisMerkleRoot is null | +10 | AXIS blocks MUST have this field |
| AXIS header: hashPrevAxisBlock points to wrong AXIS | +10 | Skip-chain broken |
| AXIS header: hashAxisMerkleRoot doesn't match computed | +10 | AXIS merkle trail broken |
| LINK header: hashPrevAxisBlock NOT null | +10 | LINK blocks MUST NOT have AXIS fields |
| Duplicate AXIS header | +1 | Minor: peer may be lagging |
| Headers exceed 2000 count | +1 | Minor: peer may be misconfigured |

#### On `AXIS_BLOCKS` (inbound):

| Violation | DoS Score | Reason |
|-----------|-----------|--------|
| Block doesn't match requested hash | +10 | Peer sending wrong block |
| Block has invalid AXIS fields | +50 | Full validation will catch this |
| Block merkle root mismatch | +50 | Transaction data corruption |

### 6.2 Validation Ordering

```
1. Parse header bytes → if fails, disconnect peer (DoS: +100)
2. Check PoW → if fails, disconnect (DoS: +50)
3. Check hashPrevBlock connects → if not, store as orphan
4. Check AXIS-specific fields:
   a. If AXIS height: validate hashPrevAxisBlock, hashAxisMerkleRoot
   b. If LINK height: validate both fields are zero
5. If all pass → add to block index (or orphan map)
6. If full block received → full validation
```

### 6.3 DoS Score Thresholds

```cpp
// In net_processing.cpp
static const int AXIS_DOS_THRESHOLD = 100;  // Ban peer above this score

// Misbehaving() examples:
Misbehaving(peer, "invalid-axis-header", 10);  // Minor AXIS violation
Misbehaving(peer, "axis-pow-failure", 50);     // PoW failure
Misbehaving(peer, "axis-merkle-mismatch", 10); // AXIS merkle trail broken
```

---

## 7. SPV Client Design

### 7.1 SPV over P2P

SPV clients use `GET_AXIS_HEADERS` to build a **lightweight AXIS header chain**:

```
SPV Client needs:
  1. AXIS headers (chain of 144-byte headers)
  2. Merkle proof for specific AXIS transactions (future)
  
SPV Client does NOT need:
  1. LINK block headers
  2. Full blocks
  3. Transaction indexing
```

### 7.2 SPV Proofs (Future Work)

> **Note:** Full SPV proof support (Merkle mountain ranges for AXIS) is out of scope for this design. This section outlines the path for future implementation.

For SPV proofs, we need the **AXIS Merkle Path**:

```
AXIS Merkle Root at block #N includes:
  merkle_root(N) = hash(merkle_root(N-3) + hash(AXIS_block_N))
  
Where merkle_root(N-3) is the previous AXIS block's merkle root.
```

This creates a **linked merkle chain** where:

1. Each AXIS block's `hashAxisMerkleRoot` commits to all prior AXIS blocks
2. SPV clients can verify any AXIS transaction by:
   a. Downloading AXIS header N
   b. Verifying header N's `hashAxisMerkleRoot` includes TX
   c. Verifying header N's `hashPrevAxisBlock` points to header N-3
   d. Verifying header N-3's `hashAxisMerkleRoot` includes TX (via merkle proof)

### 7.3 SPV Connection Flow

```
SPV Client                          Full Node
    │                                   │
    │──── GET_AXIS_HEADERS ────────────→│
    │     locator=[genesis_hash]        │
    │                                   │
    │←─── AXIS_HEADERS ─────────────────│
    │     [AXIS #3 header, 144 bytes]   │
    │                                   │
    │──── GET_AXIS_HEADERS ────────────→│
    │     locator=[AXIS #3 hash]        │
    │                                   │
    │←─── AXIS_HEADERS ─────────────────│
    │     [AXIS #6, #9, #12, ...]       │
    │                                   │
    │  (Repeat until caught up)          │
    │                                   │
    │──── GET_AXIS_BLOCKS ─────────────→│
    │     hash=AXIS_block_N_hash         │
    │     (only if user requests TX data)│
    │                                   │
    │←─── AXIS_BLOCKS ─────────────────│
    │     [full block N]                │
    │                                   │
```

---

## 8. Implementation Sketch

### 8.1 Files to Modify

| File | Changes |
|------|---------|
| `src/protocol.h` | Add `NetMsgType::GET_AXIS_HEADERS`, `NetMsgType::AXIS_HEADERS`, `NetMsgType::GET_AXIS_BLOCKS`, `NetMsgType::AXIS_BLOCKS`. Add `MSG_AXIS_BLOCK`, `MSG_AXIS_HEADER` to `GetDataMsg`. Add `NODE_AXIS` to `ServiceFlags`. |
| `src/protocol.cpp` | Add string constants for new message types. Add to `ALL_NET_MESSAGE_TYPES`. |
| `src/net_processing.h` | Add `ProcessGetAxisHeaders()`, `ProcessAxisHeaders()`, `ProcessGetAxisBlocks()`, `ProcessAxisBlocks()` declarations. |
| `src/net_processing.cpp` | Implement all 4 new message handlers. Add AXIS validation to `ProcessHeadersMessage`. Add `MaybeSendGetAxisHeaders()`. |
| `src/net.h` | Add `CNode::m_supports_axis` flag. Add `fAxisCapable` to `CConnman::Start()`. |
| `src/net.cpp` | Add AXIS DNS seeds. Add AXIS peer selection logic in `OpenNetworkConnection()`. |
| `src/chainparams.cpp` | Add `DNSSeedsAXIS()`, `FixedSeedsAXIS()`. Add `mapFixedSeedsAXIS`. |
| `src/chainparamsbase.cpp` | Add `-dnsseedaxis` flag. |
| `src/validation.cpp` | (Already has AXIS validation ~line 4122). May need minor adjustments for P2P-layer early rejection. |
| `src/blockencodings.cpp` | Add `AXISBlockHeader` partial serialization (144 bytes). |
| `src/validation.h` | Add `BlockValidationResult::BLOCK_AXIS_*` variants if needed. |
| `src/primitives/block.h` | No changes needed — `CBlockHeader` already has 144-byte layout. Add `IsAxisBlock()` helper if desired. |

### 8.2 Implementation Phases

#### Phase 1: Foundation (Minimal Viable P2P)

- [ ] Add `GET_AXIS_HEADERS` / `AXIS_HEADERS` message types
- [ ] Implement `ProcessGetAxisHeaders()` — serve AXIS headers to peers
- [ ] Implement `ProcessAxisHeaders()` — receive and validate AXIS headers
- [ ] Add AXIS field validation to `ProcessHeadersMessage()` (for mixed headers)
- [ ] Test: Two nodes can sync AXIS headers over P2P

#### Phase 2: Full Block Relay

- [ ] Add `GET_AXIS_BLOCKS` / `AXIS_BLOCKS` message types
- [ ] Implement `ProcessGetAxisBlocks()` — serve full AXIS blocks
- [ ] Implement `ProcessAxisBlocks()` — receive and validate full blocks
- [ ] Add `MSG_AXIS_BLOCK` inventory type
- [ ] Test: Full AXIS block sync works

#### Phase 3: SPV & Discovery

- [ ] Add `NODE_AXIS` service flag
- [ ] Add AXIS DNS seeds
- [ ] Implement SPV header-only sync via `GET_AXIS_HEADERS`
- [ ] Test: SPV client can sync AXIS headers only

#### Phase 4: Hardening

- [ ] Full DoS protection tuning
- [ ] Performance testing (header sync speed)
- [ ] Compatibility with Bitcoin Core 27+ P2P protocol

---

## 9. Backward Compatibility

### 9.1 Bitcoin Nodes

Bitcoin nodes will see `GET_AXIS_HEADERS` as an **unknown message type**. Per Bitcoin P2P protocol, unknown messages are silently ignored (no response, no error). This means:

- TeslaChain nodes can connect to Bitcoin nodes → no issues
- Bitcoin nodes can connect to TeslaChain nodes → they will ignore AXIS messages
- Mixed networks → Bitcoin nodes act as dead weight (they'll ignore AXIS traffic)

**This is by design** — TeslaChain's network is **separate** from Bitcoin's. You should only connect TeslaChain nodes to TeslaChain peers.

### 9.2 Version Handshake

```
1. Node A → Node B: VERSION (advertises NODE_AXIS if supported)
2. Node B → Node A: VERACK
3. Node A → Node B: VERACK
4. Both nodes know each other's capabilities
```

If a node connects without `NODE_AXIS`:
- It can still relay LINK blocks via standard P2P
- It will NOT receive `AXIS_HEADERS` messages
- It will NOT be selected as an AXIS sync peer

### 9.3 Protocol Version

TeslaChain P2P protocol version should be **different** from Bitcoin's:

```cpp
// In protocol.h or version.h
static const int PROTOCOL_VERSION_TESLACHAIN = 70017;  // Different from Bitcoin's 70015
```

This ensures:
- TeslaChain nodes never accidentally connect to Bitcoin nodes
- Bitcoin nodes never accidentally connect to TeslaChain nodes

---

## 10. Message Formats (Reference)

### 10.1 `GET_AXIS_HEADERS`

```
Size   Field               Type
-----  ------------------  ----------
4      message start       MessageStartChars
12     message type        "getaxishead" (padded)
4      payload length       uint32_t (little-endian)
4      checksum             uint32_t (first 4 bytes of sha256d)

Payload:
  ?       locator size       compactSize (max 101)
  ?×32    locator hashes    uint256[locator_size]
  32      stop hash          uint256 (uint256::ZERO = no stop)
```

### 10.2 `AXIS_HEADERS`

```
Size   Field               Type
-----  ------------------  ----------
4      message start       MessageStartChars
12     message type        "axisheads" (padded)
4      payload length       uint32_t
4      checksum             uint32_t

Payload:
  ?       header count      compactSize (max 2000)
  ?×144   AXIS headers      CBlockHeader[count] (full 144 bytes each)
```

### 10.3 `GET_AXIS_BLOCKS`

```
Size   Field               Type
-----  ------------------  ----------
4      message start       MessageStartChars
12     message type        "getaxisblk" (padded)
4      payload length       uint32_t
4      checksum             uint32_t

Payload:
  ?       hash count        compactSize
  ?×32    block hashes      uint256[count]
```

### 10.4 `AXIS_BLOCKS`

```
Size   Field               Type
-----  ------------------  ----------
4      message start       MessageStartChars
12     message type        "axisblocks" (padded)
4      payload length       uint32_t
4      checksum             uint32_t

Payload:
  ?       block count       compactSize
  ?       blocks            CBlock[count] (variable size)
```

---

## 11. Security Considerations

### 11.1 Eclipse Attacks

SPV clients are vulnerable to eclipse attacks if connected only to malicious peers. Mitigation:

- SPV clients should connect to **multiple** AXIS-capable peers
- SPV clients should use DNS seeds that return **diverse** IP ranges
- SPV clients should **prefer** peers with high uptime (tracked via addrman)

### 11.2 AXIS Chain Partitioning

If AXIS nodes become sufficiently isolated, the AXIS chain could fork. Mitigation:

- Hardcoded seed nodes should be **geographically distributed**
- DNS seeds should return **diverse** IP ranges
- Nodes should periodically **relay AXIS headers** even if not requested

### 11.3 DoS from Invalid Headers

An attacker could flood a node with invalid AXIS headers, causing:
- CPU waste on PoW verification
- Bandwidth waste on receiving headers
- Memory waste on storing orphan headers

Mitigation:
- DoS scores for invalid headers (see Section 6)
- Headers sync timeout (if peer doesn't respond, disconnect)
- Maximum headers per message (2000)

---

## 12. Open Questions

1. **Should we use a separate port for AXIS P2P?** Currently TeslaChain uses the same port (8333) as Bitcoin. Should we use a different port (e.g., 18333 for testnet, 9333 for mainnet)?

2. **How do we handle AXIS reorgs over P2P?** If an AXIS block is invalidated, we need to signal to peers to re-download the AXIS chain. Should we use `inv(MSG_AXIS_BLOCK)` announcements or a separate mechanism?

3. **Should SPV clients be able to request LINK block headers?** Currently, SPV clients can only request AXIS headers. Should we add `GET_LINK_HEADERS` for completeness?

4. **How do we handle AXIS block compression?** Full AXIS blocks are large. Should we add compact block support (`AXIS_CMPCTBLOCK`) similar to BIP 152?

5. **What is the minimum protocol version for AXIS support?** We need to decide when to advertise `NODE_AXIS`.

---

## 13. Glossary

| Term | Definition |
|------|------------|
| **LINK block** | Non-AXIS block (heights 1, 2, 4, 5, 7, 8, 10, 11...) |
| **AXIS block** | Block at height divisible by 3 (3, 6, 9, 12...) |
| **AXIS header** | 144-byte block header with `hashPrevAxisBlock` and `hashAxisMerkleRoot` |
| **hashPrevAxisBlock** | Pointer to previous AXIS block in skip-chain |
| **hashAxisMerkleRoot** | Cumulative merkle root of all AXIS blocks from GENESIS |
| **SPV** | Simplified Payment Verification — client that only stores headers |
| **DoS** | Denial of Service (anti-abuse scoring) |
| **DNS Seed** | DNS server that returns IP addresses of full nodes |

---

## 14. References

- Bitcoin P2P Protocol: [Bitcoin Developer Guide - Protocol](https://developer.bitcoin.org/reference/p2p_networking.html)
- Headers-First: [BIP 130](https://github.com/bitcoin/bips/blob/master/bip-0130.mediawiki)
- Compact Blocks: [BIP 152](https://github.com/bitcoin/bips/blob/master/bip-0152.mediawiki)
- Service Flags: [BIP 14](https://github.com/bitcoin/bips/blob/master/bip-0014.mediawiki)
- Bitcoin Protocol Version: 70015 (as of Bitcoin Core 27.0)
