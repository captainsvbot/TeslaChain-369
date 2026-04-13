// Copyright (c) 2024 TeslaChain contributors
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SPV_P2P_H
#define BITCOIN_SPV_P2P_H

#include <net.h>
#include <netaddress.h>
#include <primitives/block.h>
#include <spv.h>
#include <sync.h>
#include <uint256.h>
#include <vector>

#include <chrono>
#include <map>
#include <memory>
#include <optional>

namespace spv {

// Forward declarations
class PeerManager;
class CChain;

// Maximum AXIS headers per GET_AXIS_HEADERS request (matches MAX_HEADERS_RESULTS)
static constexpr unsigned int MAX_AXIS_HEADERS_PER_REQUEST = 2000;

// How long to wait for AXIS_HEADERS response before timing out
static constexpr auto AXIS_HEADERS_REQUEST_TIMEOUT = std::chrono::seconds{60};

// Maximum age of cached AXIS headers before requiring refresh
static constexpr auto AXIS_HEADER_CACHE_MAX_AGE = std::chrono::minutes{5};

/**
 * Cached AXIS headers from a specific peer.
 * Stores headers along with metadata for validation and cache management.
 */
struct PeerAxisHeaderState {
    // AXIS block hashes this peer has announced/served, mapped to height
    std::map<uint256, int> axis_headers_by_hash;

    // Highest AXIS height this peer has communicated
    int highest_axis_height = 0;

    // Last time we received AXIS headers from this peer
    std::chrono::seconds last_axis_headers_received{0};

    // Whether this peer has responded to our GET_AXIS_HEADERS
    bool responded_to_request = false;
};

/**
 * Thread-safe cache of AXIS headers fetched from peers.
 * Used by SPV clients to store and verify AXIS headers without running a full node.
 */
class AxisHeaderCache {
public:
    AxisHeaderCache() = default;

    // Add a batch of AXIS headers from a peer
    void AddHeaders(int peer_id, const std::vector<AxisHeader>& headers);

    // Get a header by hash
    std::optional<AxisHeader> GetHeader(const uint256& hash) const;

    // Get a header by height
    std::optional<AxisHeader> GetHeaderByHeight(int height) const;

    // Get the chain of AXIS headers from start_height up to count headers
    std::vector<AxisHeader> GetHeaderChain(int start_height, int count) const;

    // Get the best (highest) AXIS height we have cached
    int GetBestHeight() const;

    // Get the best cached height from a specific peer
    int GetBestHeightFromPeer(int peer_id) const;

    // Check if we have headers from a specific height onwards
    bool HasHeadersFrom(int start_height, int count) const;

    // Get all cached header hashes
    std::vector<uint256> GetCachedHashes() const;

    // Get peer states
    PeerAxisHeaderState* GetPeerState(int peer_id);
    void UpdatePeerState(int peer_id, const PeerAxisHeaderState& state);
    std::optional<PeerAxisHeaderState> GetPeerState(int peer_id) const;

    // Prune headers older than a certain height (for memory management)
    void Prune(int min_height);

    // Clear all cached data
    void Clear();

    // Get total cached header count
    size_t Size() const;

private:
    mutable RecursiveMutex m_cs;
    // Map of hash -> AxisHeader
    std::map<uint256, AxisHeader> m_headers GUARDED_BY(m_cs);
    // Map of height -> hash (for fast lookup by height)
    std::map<int, uint256> m_height_to_hash GUARDED_BY(m_cs);
    // Per-peer state tracking
    std::map<int, PeerAxisHeaderState> m_peer_states GUARDED_BY(m_cs);
};

/**
 * P2P SPV client for fetching AXIS headers and constructing SPV proofs.
 *
 * This class provides an interface for lightweight (SPV) clients to:
 * - Fetch AXIS headers from connected peers over P2P
 * - Cache headers for verification
 * - Construct SPV proofs for transactions
 *
 * Unlike the RPC-based SPV which requires a local full node, this client
 * fetches headers directly from peer nodes over the Bitcoin P2P network.
 */
class SPVP2PClient {
public:
    SPVP2PClient();

    /**
     * Initialize the P2P SPV client.
     */
    void Init();

    /**
     * Fetch AXIS headers from multiple peers for redundancy.
     * Uses the LINK chain locator to find the starting point.
     *
     * @param peers List of peer IDs to try
     * @param locator The LINK chain locator
     * @param hash_stop Stop at this hash (uint256::ZERO = no stop)
     * @return Number of headers received, or error string
     */
    std::variant<size_t, std::string> FetchAxisHeadersFromPeers(
        const std::vector<NodeId>& peers,
        const CBlockLocator& locator,
        const uint256& hash_stop = uint256::ZERO);

    /**
     * Get the current best cached AXIS height.
     */
    int GetBestCachedHeight() const;

    /**
     * Get AXIS headers from cache.
     */
    std::vector<AxisHeader> GetCachedHeaders(int start_height, int count) const;

    /**
     * Get a specific header from cache.
     */
    std::optional<AxisHeader> GetCachedHeader(const uint256& hash) const;

    /**
     * Get the header cache for direct access.
     */
    AxisHeaderCache& GetCache() { return m_cache; }
    const AxisHeaderCache& GetCache() const { return m_cache; }

    /**
     * Update peer state when we receive AXIS headers from them.
     */
    void UpdatePeerAxisState(int peer_id, const PeerAxisHeaderState& state);

    /**
     * Get peer state.
     */
    std::optional<PeerAxisHeaderState> GetPeerAxisState(int peer_id) const;

    /**
     * Check if we have enough headers to verify an AXIS chain.
     */
    bool HasCompleteChain(int from_height, int to_height) const;

    /**
     * Build AXIS chain from cache going backwards from target_height to GENESIS.
     */
    std::vector<AxisHeader> BuildAxisChain(int target_height) const;

private:
    AxisHeaderCache m_cache;
    mutable RecursiveMutex m_cs;
};

/**
 * Global SPV P2P client instance (for use by net_processing and RPC).
 * Initialized during node startup.
 */
extern std::unique_ptr<SPVP2PClient> g_spv_p2p_client;

/**
 * Initialize the global SPV P2P client.
 * Called during node initialization.
 */
void InitSPVP2PClient();

/**
 * Get a suitable peer for AXIS header requests.
 * Prefers peers that have advertised NODE_AXIS capability.
 */
NodeId GetBestPeerForAxisHeaders();

} // namespace spv

#endif // BITCOIN_SPV_P2P_H
