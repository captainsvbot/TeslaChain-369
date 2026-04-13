// Copyright (c) 2024 TeslaChain contributors
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <spv_p2p.h>
#include <logging.h>
#include <net_processing.h>
#include <util/strencodings.h>

#include <algorithm>

namespace spv {

// ---------------------------------------------------------------------
// AxisHeaderCache implementation
// ---------------------------------------------------------------------

void AxisHeaderCache::AddHeaders(int peer_id, const std::vector<AxisHeader>& headers)
{
    LOCK(m_cs);
    for (const AxisHeader& ah : headers) {
        m_headers[ah.hash] = ah;
        m_height_to_hash[ah.nHeight] = ah.hash;
        // Track in peer state
        m_peer_states[peer_id].axis_headers_by_hash[ah.hash] = ah.nHeight;
        if (ah.nHeight > m_peer_states[peer_id].highest_axis_height) {
            m_peer_states[peer_id].highest_axis_height = ah.nHeight;
        }
    }
    LogDebug(BCLog::NET, "AxisHeaderCache: Added %zu headers from peer %d (heights %d to %d, total cached: %zu)\n",
             headers.size(), peer_id,
             headers.empty() ? -1 : headers.front().nHeight,
             headers.empty() ? -1 : headers.back().nHeight,
             m_headers.size());
}

std::optional<AxisHeader> AxisHeaderCache::GetHeader(const uint256& hash) const
{
    LOCK(m_cs);
    auto it = m_headers.find(hash);
    if (it != m_headers.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<AxisHeader> AxisHeaderCache::GetHeaderByHeight(int height) const
{
    LOCK(m_cs);
    auto it = m_height_to_hash.find(height);
    if (it != m_height_to_hash.end()) {
        return GetHeader(it->second);
    }
    return std::nullopt;
}

std::vector<AxisHeader> AxisHeaderCache::GetHeaderChain(int start_height, int count) const
{
    LOCK(m_cs);
    std::vector<AxisHeader> result;
    result.reserve(count);
    for (int i = 0; i < count; ++i) {
        int h = start_height + i;
        auto hit = m_height_to_hash.find(h);
        if (hit == m_height_to_hash.end()) break;
        auto lit = m_headers.find(hit->second);
        if (lit == m_headers.end()) break;
        result.push_back(lit->second);
    }
    return result;
}

int AxisHeaderCache::GetBestHeight() const
{
    LOCK(m_cs);
    if (m_height_to_hash.empty()) return 0;
    return m_height_to_hash.rbegin()->first;
}

int AxisHeaderCache::GetBestHeightFromPeer(int peer_id) const
{
    LOCK(m_cs);
    auto it = m_peer_states.find(peer_id);
    if (it != m_peer_states.end()) {
        return it->second.highest_axis_height;
    }
    return 0;
}

bool AxisHeaderCache::HasHeadersFrom(int start_height, int count) const
{
    LOCK(m_cs);
    for (int i = 0; i < count; ++i) {
        if (!m_height_to_hash.count(start_height + i)) {
            return false;
        }
    }
    return true;
}

std::vector<uint256> AxisHeaderCache::GetCachedHashes() const
{
    LOCK(m_cs);
    std::vector<uint256> hashes;
    hashes.reserve(m_headers.size());
    for (const auto& [hash, _] : m_headers) {
        hashes.push_back(hash);
    }
    return hashes;
}

PeerAxisHeaderState* AxisHeaderCache::GetPeerState(int peer_id)
{
    LOCK(m_cs);
    return &m_peer_states[peer_id];
}

void AxisHeaderCache::UpdatePeerState(int peer_id, const PeerAxisHeaderState& state)
{
    LOCK(m_cs);
    m_peer_states[peer_id] = state;
}

std::optional<PeerAxisHeaderState> AxisHeaderCache::GetPeerState(int peer_id) const
{
    LOCK(m_cs);
    auto it = m_peer_states.find(peer_id);
    if (it != m_peer_states.end()) {
        return it->second;
    }
    return std::nullopt;
}

void AxisHeaderCache::Prune(int min_height)
{
    LOCK(m_cs);
    // Remove headers below min_height
    for (auto it = m_height_to_hash.begin(); it != m_height_to_hash.end(); ) {
        if (it->first < min_height) {
            m_headers.erase(it->second);
            it = m_height_to_hash.erase(it);
        } else {
            ++it;
        }
    }
    LogDebug(BCLog::NET, "AxisHeaderCache: Pruned headers below height %d, %zu headers remain\n",
             min_height, m_headers.size());
}

void AxisHeaderCache::Clear()
{
    LOCK(m_cs);
    m_headers.clear();
    m_height_to_hash.clear();
    m_peer_states.clear();
}

size_t AxisHeaderCache::Size() const
{
    LOCK(m_cs);
    return m_headers.size();
}

// ---------------------------------------------------------------------
// SPVP2PClient implementation
// ---------------------------------------------------------------------

SPVP2PClient::SPVP2PClient() {}

void SPVP2PClient::Init()
{
    LogInfo("SPVP2PClient: initialized\n");
}

std::variant<size_t, std::string> SPVP2PClient::FetchAxisHeadersFromPeers(
    const std::vector<NodeId>& peers,
    const CBlockLocator& locator,
    const uint256& hash_stop)
{
    if (peers.empty()) {
        return std::string("No peers specified for AXIS header fetch");
    }

    LogDebug(BCLog::NET, "SPVP2PClient: FetchAxisHeadersFromPeers called for %zu peers\n", peers.size());

    // TODO: In a follow-up, implement actual peer-based header fetching
    // via PeerManager. For now, this is a placeholder that allows the
    // RPC `getaxisproof use_peer=1` to gracefully report an error.
    return std::string("Peer-based AXIS header fetching not yet implemented — use local RPC mode");
}

int SPVP2PClient::GetBestCachedHeight() const
{
    return m_cache.GetBestHeight();
}

std::vector<AxisHeader> SPVP2PClient::GetCachedHeaders(int start_height, int count) const
{
    return m_cache.GetHeaderChain(start_height, count);
}

std::optional<AxisHeader> SPVP2PClient::GetCachedHeader(const uint256& hash) const
{
    return m_cache.GetHeader(hash);
}

void SPVP2PClient::UpdatePeerAxisState(int peer_id, const PeerAxisHeaderState& state)
{
    m_cache.UpdatePeerState(peer_id, state);
}

std::optional<PeerAxisHeaderState> SPVP2PClient::GetPeerAxisState(int peer_id) const
{
    return m_cache.GetPeerState(peer_id);
}

bool SPVP2PClient::HasCompleteChain(int from_height, int to_height) const
{
    if (to_height < from_height) return false;
    int count = to_height - from_height + 1;
    return m_cache.HasHeadersFrom(from_height, count);
}

std::vector<AxisHeader> SPVP2PClient::BuildAxisChain(int target_height) const
{
    std::vector<AxisHeader> chain;
    int current_height = target_height;

    while (current_height > 0) {
        auto hdr_opt = m_cache.GetHeaderByHeight(current_height);
        if (!hdr_opt) {
            LogDebug(BCLog::NET, "SPVP2PClient: Missing AXIS header at height %d while building chain\n", current_height);
            break;
        }
        chain.push_back(*hdr_opt);

        // Walk hashPrevAxisBlock chain backwards
        if (hdr_opt->hashPrevAxis.IsNull()) {
            // Genesis AXIS block — stop here
            break;
        }

        // Find previous AXIS header by hashPrevAxis
        auto prev_opt = m_cache.GetHeader(hdr_opt->hashPrevAxis);
        if (!prev_opt) {
            // hashPrevAxis not in cache — can't go further back
            LogDebug(BCLog::NET, "SPVP2PClient: hashPrevAxis %s not in cache, stopping at height %d\n",
                     hdr_opt->hashPrevAxis.ToString(), current_height);
            break;
        }
        current_height = prev_opt->nHeight;
    }

    // Reverse so chain is from GENESIS to target
    std::reverse(chain.begin(), chain.end());
    return chain;
}

// ---------------------------------------------------------------------
// Global instance
// ---------------------------------------------------------------------

std::unique_ptr<SPVP2PClient> g_spv_p2p_client;

void InitSPVP2PClient()
{
    if (!g_spv_p2p_client) {
        g_spv_p2p_client = std::make_unique<SPVP2PClient>();
        g_spv_p2p_client->Init();
    }
}

NodeId GetBestPeerForAxisHeaders()
{
    // TODO: Query PeerManager for a peer advertising NODE_AXIS.
    // For now, return NO_NODE. The caller should fall back to
    // local RPC mode or wait for NODE_AXIS peer discovery.
    return -1;
}

} // namespace spv
