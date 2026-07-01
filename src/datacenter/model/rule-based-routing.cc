/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#include "rule-based-routing.h"

#include "routing-rule-manager.h"
#include "structured-address-directory.h"
#include "congestion-signal-provider.h"
#include "non-minimal-policy.h"
#include "non-minimal-routing-tag.h"

#include "ns3/boolean.h"
#include "ns3/channel.h"
#include "ns3/log.h"
#include "ns3/uinteger.h"
#include "ns3/ipv4-list-routing.h"
#include "ns3/point-to-point-channel.h"
#include "ns3/point-to-point-net-device.h"

#include <algorithm>
#include <iostream>
#include <unordered_set>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("RuleBasedRouting");

NS_OBJECT_ENSURE_REGISTERED(RuleBasedRouting);


namespace
{
bool
WithdrawTraceEnabled()
{
    static bool enabled = []() -> bool {
        const char* v = std::getenv("NVWA_WITHDRAW_TRACE");
        return (v != nullptr) && (v[0] != '\0') && (v[0] != '0');
    }();
    return enabled;
}

std::vector<uint32_t>
IntersectPortsPreserveLeft(const std::vector<uint32_t>& left, const std::vector<uint32_t>& right)
{
    if (left.empty() || right.empty())
    {
        return {};
    }
    std::unordered_set<uint32_t> rightSet(right.begin(), right.end());
    std::vector<uint32_t> out;
    out.reserve(std::min(left.size(), right.size()));
    for (uint32_t p : left)
    {
        if (rightSet.find(p) != rightSet.end())
        {
            out.push_back(p);
        }
    }
    return out;
}



struct PendingWithdrawal
{
    Ptr<RuleBasedRouting> target;
    RuleBasedRouting::RegionKey rk;
    std::string pfx;
    uint32_t originNodeId{0};
    uint64_t epoch{0};
    uint32_t fromNeighborNodeId{0};
};

std::vector<PendingWithdrawal> g_withdrawQueue;
bool g_withdrawDispatching = false;
} // namespace

// ------------------- RuleBasedRouting ---------------------
TypeId
RuleBasedRouting::GetTypeId()
{
    static TypeId tid = TypeId("ns3::RuleBasedRouting")
                            .SetParent<Ipv4RoutingProtocol>()
                            .SetGroupName("Datacenter")
                            .AddConstructor<RuleBasedRouting>();
    return tid;
}

RuleBasedRouting::RuleBasedRouting()
{
    NS_LOG_FUNCTION(this);
    m_congestionProvider = std::make_shared<NetDeviceQueueSignalProvider>();
}

RuleBasedRouting::~RuleBasedRouting()
{
    NS_LOG_FUNCTION(this);
}

void
RuleBasedRouting::SetIpv4(Ptr<Ipv4> ipv4)
{
    NS_LOG_FUNCTION(this << ipv4);
    m_ipv4 = ipv4;
    m_ingressReady = false;
    m_peerGatewayCacheReady = false;
    m_peerGatewayByIf.clear();
}

void
RuleBasedRouting::SetNonMinimalPolicy(Ptr<NonMinimalPolicy> policy)
{
    m_nonMinimalPolicy = policy;
}

Ptr<NonMinimalPolicy>
RuleBasedRouting::GetNonMinimalPolicy() const
{
    return m_nonMinimalPolicy;
}

void
RuleBasedRouting::SetCongestionSignalProvider(std::shared_ptr<CongestionSignalProvider> provider)
{
    m_congestionProvider = provider;
}

void
RuleBasedRouting::AddRule(Ptr<RoutingRule> rule)
{
    NS_ASSERT(rule);
    RoutingRuleManager* ruleManager = RoutingRuleManager::GetInstance();
    ruleManager->AddRule(m_levelId, rule);
}

void
RuleBasedRouting::Freeze()
{
    RoutingRuleManager* ruleManager = RoutingRuleManager::GetInstance();
    ruleManager->FreezeLevel(m_levelId);
}

void
RuleBasedRouting::SetPortSet(const PortSet& ports)
{
    m_ports = ports;
    m_ingressReady = false;
}

uint64_t
RuleBasedRouting::ComputeFlowHash(const Ipv4Header& h, Ptr<const Packet> p, uint32_t seed)
{
    // Extract 5-tuple for flow identification (similar to switch-node.cc:90-106)
    union {
        uint8_t u8[12];  // 12 bytes: SIP(4) + DIP(4) + Sport(2) + Dport(2)
        uint32_t u32[3];
    } buf;

    buf.u32[0] = h.GetSource().Get();       // Source IP
    buf.u32[1] = h.GetDestination().Get();  // Destination IP

    // Extract ports from TCP/UDP header
    uint16_t sport = 0;
    uint16_t dport = 0;

    if (p && (h.GetProtocol() == 6 || h.GetProtocol() == 17)) {  // TCP or UDP
        // CopyData() is const and does not allocate a Packet copy; the transport header is at the
        // beginning of the packet payload seen by the routing layer.
        if (p->GetSize() >= 4) {
            uint8_t portBuf[4];
            uint32_t copied = p->CopyData(portBuf, 4);
            if (copied >= 4) {
                sport = (portBuf[0] << 8) | portBuf[1];  // Big-endian
                dport = (portBuf[2] << 8) | portBuf[3];
            }
        }
    }

    buf.u32[2] = sport | ((uint32_t)dport << 16);  // Pack ports into one uint32_t

    static int hash_count = 0;
    bool do_log = (hash_count < 3);

    if (do_log) {
        NS_LOG_UNCOND("ComputeFlowHash (5-tuple): src=" << h.GetSource()
                      << " dst=" << h.GetDestination()
                      << " proto=" << static_cast<int>(h.GetProtocol())
                      << " sport=" << sport << " dport=" << dport);
    }

    // MurmurHash3-inspired hash (reference: switch-node.cc:179-215)
    uint32_t hash = seed;  // Use provided seed (typically node ID for per-node diversity)

    // Process 32-bit blocks
    for (int i = 0; i < 3; i++) {
        uint32_t k = buf.u32[i];
        k *= 0xcc9e2d51;
        k = (k << 15) | (k >> 17);
        k *= 0x1b873593;
        hash ^= k;
        hash = (hash << 13) | (hash >> 19);
        hash = hash * 5 + 0xe6546b64;
    }

    // Finalization - ensures good bit distribution
    hash ^= 12;  // length
    hash ^= hash >> 16;
    hash *= 0x85ebca6b;
    hash ^= hash >> 13;
    hash *= 0xc2b2ae35;
    hash ^= hash >> 16;

    if (do_log) {
        NS_LOG_UNCOND("  Final hash: " << hash << " (hash%2=" << (hash%2) << ")");
        hash_count++;
    }

    return hash;
}

void
RuleBasedRouting::NotifyInterfaceUp(uint32_t interface)
{
    if (!m_ipv4)
    {
        return;
    }
    if (interface == 0 || interface >= m_ipv4->GetNInterfaces())
    {
        return;
    }
    // Idempotent: if already up, do nothing.
    if (!m_ports.IsPortDown(interface))
    {
        return;
    }

    // Track impacted buckets (across directions) that contain this interface.
    struct Impact
    {
        Direction dir;
        uint32_t dim;
        std::string bucketKey;
    };

    auto containsPort = [](const std::vector<uint32_t>& v, uint32_t p) -> bool {
        return std::find(v.begin(), v.end(), p) != v.end();
    };

    std::vector<Impact> impacted;
    for (const auto& k : m_ports.GetDownwardKeysByPort(interface))
    {
        impacted.push_back({Direction::Downward, 0, k});
    }
    for (const auto& kv : m_ports.GetUpwardBuckets())
    {
        if (containsPort(kv.second, interface))
        {
            impacted.push_back({Direction::Upward, 0, kv.first});
        }
    }
    const auto& sameLevelBuckets = m_ports.GetSameLevelBuckets();
    for (uint32_t dim = 0; dim < sameLevelBuckets.size(); ++dim)
    {
        for (const auto& kv : sameLevelBuckets[dim])
        {
            if (containsPort(kv.second, interface))
            {
                impacted.push_back({Direction::SameLevel, dim, kv.first});
            }
        }
    }

    // Apply recovery locally.
    m_ports.MarkPortUp(interface);

    // We update/remove exceptions for impacted regions. We intentionally do NOT emit withdrawals on UP.
    if (!m_topo)
    {
        return;
    }

    // ---------- RegionKey builders ----------
    const bool haveDownwardMeta = (m_levelId > 0);
    int levelStart = haveDownwardMeta ? m_topo->GetLevelAddrBit(m_levelId) : -1;
    int childStart = haveDownwardMeta ? m_topo->GetLevelAddrBit(m_levelId - 1) : -1;

    const size_t addrSize = m_src.Size();
    const bool srcOk = (addrSize != 0 && m_levelId <= addrSize);

    const size_t currentPrefixLen = srcOk ? (addrSize - m_levelId) : 0;
    const size_t expectedDownSegLen =
        (haveDownwardMeta && levelStart >= 0 && childStart >= 0 && childStart < levelStart)
            ? static_cast<size_t>(levelStart - childStart)
            : 0;

    auto buildDownwardRegionForBucketKey =
        [&](const std::string& bucketKey) -> std::optional<RegionKey> {
            if (!haveDownwardMeta || !srcOk || expectedDownSegLen == 0)
            {
                return std::nullopt;
            }
            std::vector<uint32_t> seg = ParseKeyFields(bucketKey);
            if (seg.size() != expectedDownSegLen)
            {
                return std::nullopt;
            }

            RegionKey rk;
            const size_t prefixLen = currentPrefixLen + seg.size();
            rk.prefixLen = static_cast<uint32_t>(prefixLen);
            rk.prefixFields.resize(prefixLen);

            for (size_t i = 0; i < currentPrefixLen; ++i)
            {
                size_t lsbIdx = (addrSize - 1) - i;
                rk.prefixFields[i] = m_src[lsbIdx];
            }
            for (size_t i = 0; i < seg.size(); ++i)
            {
                rk.prefixFields[currentPrefixLen + i] = seg[i];
            }
            return rk;
        };

    auto buildPrefixRegionFromKey =
        [&](const std::string& bucketKey) -> std::optional<RegionKey> {
            std::vector<uint32_t> seg = ParseKeyFields(bucketKey);
            if (seg.empty())
            {
                return std::nullopt;
            }
            RegionKey rk;
            rk.prefixLen = static_cast<uint32_t>(seg.size());
            rk.prefixFields.assign(seg.begin(), seg.end());
            return rk;
        };

    auto encodeRegionPfx = [&](const RegionKey& rk) -> std::string {
        if (rk.prefixLen == 0)
        {
            return std::string();
        }
        std::ostringstream oss;
        for (uint32_t j = 0; j < rk.prefixLen && j < rk.prefixFields.size(); ++j)
        {
            if (j)
            {
                oss << ".";
            }
            oss << rk.prefixFields[j];
        }
        return oss.str();
    };

    auto buildUpwardRegionsForEmptyKey = [&]() -> std::vector<RegionKey> {
        std::vector<RegionKey> regions;
        if (!m_topo)
        {
            return regions;
        }

        const int levelAddrBit = m_topo->GetLevelAddrBit(m_levelId);
        const size_t addrSize = m_src.Size();
        if (levelAddrBit < 0 || addrSize == 0)
        {
            return regions;
        }
        const size_t startPos = static_cast<size_t>(levelAddrBit + 1);
        if (startPos >= addrSize)
        {
            return regions;
        }
        const size_t prefixLen = addrSize - startPos;
        if (prefixLen == 0)
        {
            return regions;
        }

        auto makeRegionFromAddr = [&](const StructuredAddress& addr) -> std::optional<RegionKey> {
            if (addr.Size() < prefixLen)
            {
                return std::nullopt;
            }
            RegionKey rk;
            rk.prefixLen = static_cast<uint32_t>(prefixLen);
            rk.prefixFields.resize(prefixLen);
            const size_t size = addr.Size();
            for (size_t i = 0; i < prefixLen; ++i)
            {
                size_t idx = (size - 1) - i;
                rk.prefixFields[i] = addr[idx];
            }
            return rk;
        };

        std::unordered_set<std::string> seen;
        auto localRk = makeRegionFromAddr(m_src);
        const std::string localKey = localRk ? encodeRegionPfx(*localRk) : std::string();

        const auto& allAddrs = m_topo->GetStructuredAddrs();
        if (allAddrs.empty())
        {
            return regions;
        }
        const auto& hostAddrs = allAddrs[0];
        for (const auto& addr : hostAddrs)
        {
            auto rkOpt = makeRegionFromAddr(addr);
            if (!rkOpt.has_value())
            {
                continue;
            }
            const std::string key = encodeRegionPfx(*rkOpt);
            if (key == localKey)
            {
                continue;
            }
            if (seen.insert(key).second)
            {
                regions.push_back(std::move(*rkOpt));
            }
        }
        return regions;
    };

    auto getAvailCopy = [&](const Impact& imp) -> std::vector<uint32_t> {
        switch (imp.dir)
        {
        case Direction::Downward:
            return m_ports.GetAvailableDownward(imp.bucketKey);
        case Direction::Upward:
            return m_ports.GetAvailableUpward(imp.bucketKey);
        case Direction::SameLevel:
            return m_ports.GetAvailableSameLevel(imp.dim, imp.bucketKey);
        }
        return {};
    };

    std::optional<std::vector<RegionKey>> cachedUpwardRegions;
    for (const auto& imp : impacted)
    {
        // Base (all) vs available (post-recovery).
        std::vector<uint32_t> base;
        switch (imp.dir)
        {
        case Direction::Downward:
            base = m_ports.GetDownward(imp.bucketKey);
            break;
        case Direction::Upward:
            base = m_ports.GetUpward(imp.bucketKey);
            break;
        case Direction::SameLevel:
            base = m_ports.GetSameLevel(imp.dim, imp.bucketKey);
            break;
        }

        const std::vector<uint32_t> avail = getAvailCopy(imp);

        std::vector<RegionKey> regions;
        if (imp.dir == Direction::Downward)
        {
            if (auto maybeRk = buildDownwardRegionForBucketKey(imp.bucketKey); maybeRk.has_value())
            {
                regions.push_back(*maybeRk);
            }
        }
        else if (imp.dir == Direction::Upward && imp.bucketKey.empty())
        {
            if (!cachedUpwardRegions.has_value())
            {
                cachedUpwardRegions = buildUpwardRegionsForEmptyKey();
            }
            regions = *cachedUpwardRegions;
        }
        else
        {
            if (auto maybeRk = buildPrefixRegionFromKey(imp.bucketKey); maybeRk.has_value())
            {
                regions.push_back(*maybeRk);
            }
        }
        if (regions.empty())
        {
            continue;
        }

        if (base == avail)
        {
            // Fully recovered for this region: remove exception.
            for (const auto& rk : regions)
            {
                RemoveExceptionEntry(rk);
            }
        }
        else
        {
            // Still impacted by other failures: keep exception updated.
            for (const auto& rk : regions)
            {
                const std::string pfx = encodeRegionPfx(rk);
                SetExceptionEntry(rk, pfx, avail, std::nullopt);
            }
        }
    }

    // Debug: print local exception table after processing this interface-up event.
    {
        std::ostringstream r;
        r << "NotifyInterfaceUp if=" << interface;
        DumpExceptionTable(r.str());
    }
}


void
RuleBasedRouting::NotifyInterfaceDown(uint32_t interface)
{
    if (!m_ipv4)
    {
        return;
    }
    if (interface == 0 || interface >= m_ipv4->GetNInterfaces())
    {
        return;
    }
    // Idempotent: if already down, do nothing.
    if (m_ports.IsPortDown(interface))
    {
        return;
    }

    // Track impacted buckets (across directions) that contain this interface.
    struct Impact
    {
        Direction dir;
        uint32_t dim;            // only used for SameLevel
        std::string bucketKey;   // PortSet bucket key
    };

    auto containsPort = [](const std::vector<uint32_t>& v, uint32_t p) -> bool {
        return std::find(v.begin(), v.end(), p) != v.end();
    };

    std::vector<Impact> impacted;
    // Downward: we have a reverse index for performance.
    for (const auto& k : m_ports.GetDownwardKeysByPort(interface))
    {
        impacted.push_back({Direction::Downward, 0, k});
    }
    // Upward: scan buckets (no reverse index yet).
    for (const auto& kv : m_ports.GetUpwardBuckets())
    {
        if (containsPort(kv.second, interface))
        {
            impacted.push_back({Direction::Upward, 0, kv.first});
        }
    }
    // Same-level: scan buckets (no reverse index yet).
    const auto& sameLevelBuckets = m_ports.GetSameLevelBuckets();
    for (uint32_t dim = 0; dim < sameLevelBuckets.size(); ++dim)
    {
        for (const auto& kv : sameLevelBuckets[dim])
        {
            if (containsPort(kv.second, interface))
            {
                impacted.push_back({Direction::SameLevel, dim, kv.first});
            }
        }
    }

    // Capture pre-failure available candidates per impacted bucket (to detect transitions to empty).
    std::vector<std::vector<uint32_t>> preAvail;
    preAvail.reserve(impacted.size());

    auto getAvailCopy = [&](const Impact& imp) -> std::vector<uint32_t> {
        switch (imp.dir)
        {
        case Direction::Downward:
            return m_ports.GetAvailableDownward(imp.bucketKey);
        case Direction::Upward:
            return m_ports.GetAvailableUpward(imp.bucketKey);
        case Direction::SameLevel:
            return m_ports.GetAvailableSameLevel(imp.dim, imp.bucketKey);
        }
        return {};
    };

    for (const auto& imp : impacted)
    {
        preAvail.push_back(getAvailCopy(imp));
    }

    // Apply the failure locally.
    m_ports.MarkPortDown(interface);

    // Without topology/level metadata we can still locally mask failures, but cannot build region keys.
    if (!m_topo)
    {
        return;
    }

    // ---------- RegionKey builders ----------
    // Downward region builder: level-aligned, depends on local node's prefix at this level.
    const bool haveDownwardMeta = (m_levelId > 0);
    int levelStart = haveDownwardMeta ? m_topo->GetLevelAddrBit(m_levelId) : -1;
    int childStart = haveDownwardMeta ? m_topo->GetLevelAddrBit(m_levelId - 1) : -1;

    const size_t addrSize = m_src.Size();
    const bool srcOk = (addrSize != 0 && m_levelId <= addrSize);

    const size_t currentPrefixLen = srcOk ? (addrSize - m_levelId) : 0;
    const size_t expectedDownSegLen =
        (haveDownwardMeta && levelStart >= 0 && childStart >= 0 && childStart < levelStart)
            ? static_cast<size_t>(levelStart - childStart)
            : 0;

    auto buildDownwardRegionForBucketKey =
        [&](const std::string& bucketKey) -> std::optional<RegionKey> {
            if (!haveDownwardMeta || !srcOk || expectedDownSegLen == 0)
            {
                return std::nullopt;
            }
            std::vector<uint32_t> seg = ParseKeyFields(bucketKey);
            if (seg.size() != expectedDownSegLen)
            {
                return std::nullopt;
            }

            RegionKey rk;
            const size_t prefixLen = currentPrefixLen + seg.size();
            rk.prefixLen = static_cast<uint32_t>(prefixLen);
            rk.prefixFields.resize(prefixLen);

            // current layer prefix (MSB-first; derived from local address, consistent with EncodePrefix/Matches)
            for (size_t i = 0; i < currentPrefixLen; ++i)
            {
                size_t lsbIdx = (addrSize - 1) - i;
                rk.prefixFields[i] = m_src[lsbIdx];
            }
            // append segment fields (already MSB-first in string)
            for (size_t i = 0; i < seg.size(); ++i)
            {
                rk.prefixFields[currentPrefixLen + i] = seg[i];
            }
            return rk;
        };

    // Non-downward region builder: treat bucketKey itself as an MSB-first destination prefix.
    auto buildPrefixRegionFromKey =
        [&](const std::string& bucketKey) -> std::optional<RegionKey> {
            std::vector<uint32_t> seg = ParseKeyFields(bucketKey);
            if (seg.empty())
            {
                // Empty key is destination-agnostic; cannot form a prefix-region key.
                return std::nullopt;
            }
            RegionKey rk;
            rk.prefixLen = static_cast<uint32_t>(seg.size());
            rk.prefixFields.assign(seg.begin(), seg.end());
            return rk;
        };

    auto encodeRegionPfx = [&](const RegionKey& rk) -> std::string {
        // "<d0>.<d1>..." (no prefixLen), consistent with EncodePrefix()
        if (rk.prefixLen == 0)
        {
            return std::string();
        }
        std::ostringstream oss;
        for (uint32_t j = 0; j < rk.prefixLen && j < rk.prefixFields.size(); ++j)
        {
            if (j)
            {
                oss << ".";
            }
            oss << rk.prefixFields[j];
        }
        return oss.str();
    };

    auto buildUpwardRegionsForEmptyKey = [&]() -> std::vector<RegionKey> {
        std::vector<RegionKey> regions;
        if (!m_topo)
        {
            return regions;
        }

        const int levelAddrBit = m_topo->GetLevelAddrBit(m_levelId);
        const size_t addrSize = m_src.Size();
        if (levelAddrBit < 0 || addrSize == 0)
        {
            return regions;
        }
        const size_t startPos = static_cast<size_t>(levelAddrBit + 1);
        if (startPos >= addrSize)
        {
            return regions;
        }
        const size_t prefixLen = addrSize - startPos;
        if (prefixLen == 0)
        {
            return regions;
        }

        auto makeRegionFromAddr = [&](const StructuredAddress& addr) -> std::optional<RegionKey> {
            if (addr.Size() < prefixLen)
            {
                return std::nullopt;
            }
            RegionKey rk;
            rk.prefixLen = static_cast<uint32_t>(prefixLen);
            rk.prefixFields.resize(prefixLen);
            const size_t size = addr.Size();
            for (size_t i = 0; i < prefixLen; ++i)
            {
                size_t idx = (size - 1) - i;
                rk.prefixFields[i] = addr[idx];
            }
            return rk;
        };

        std::unordered_set<std::string> seen;
        auto localRk = makeRegionFromAddr(m_src);
        const std::string localKey = localRk ? encodeRegionPfx(*localRk) : std::string();

        const auto& allAddrs = m_topo->GetStructuredAddrs();
        if (allAddrs.empty())
        {
            return regions;
        }
        const auto& hostAddrs = allAddrs[0];
        for (const auto& addr : hostAddrs)
        {
            auto rkOpt = makeRegionFromAddr(addr);
            if (!rkOpt.has_value())
            {
                continue;
            }
            const std::string key = encodeRegionPfx(*rkOpt);
            if (key == localKey)
            {
                continue;
            }
            if (seen.insert(key).second)
            {
                regions.push_back(std::move(*rkOpt));
            }
        }
        return regions;
    };

    // ---------- Apply exception updates + withdrawal triggers ----------
    Ptr<Node> selfNode = m_ipv4->GetObject<Node>();
    uint32_t selfId = selfNode ? selfNode->GetId() : 0;
    std::unordered_set<std::string> withdrawTriggered;
    std::optional<uint32_t> excludeNeighborId;
    {
        Ptr<NetDevice> dev = m_ipv4->GetNetDevice(interface);
        Ptr<PointToPointNetDevice> p2p = DynamicCast<PointToPointNetDevice>(dev);
        Ptr<PointToPointChannel> ch = p2p ? DynamicCast<PointToPointChannel>(p2p->GetChannel())
                                          : nullptr;
        if (ch)
        {
            for (uint32_t k = 0; k < ch->GetNDevices(); ++k)
            {
                Ptr<NetDevice> peerDev = ch->GetDevice(k);
                if (!peerDev || peerDev == dev)
                {
                    continue;
                }
                Ptr<Node> peerNode = peerDev->GetNode();
                if (peerNode)
                {
                    excludeNeighborId = peerNode->GetId();
                }
                break;
            }
        }
    }

    std::optional<std::vector<RegionKey>> cachedUpwardRegions;
    for (size_t idx = 0; idx < impacted.size(); ++idx)
    {
        const Impact& imp = impacted[idx];

        // Base candidates (including failed ones) vs post-failure available candidates.
        std::vector<uint32_t> base;
        switch (imp.dir)
        {
        case Direction::Downward:
            base = m_ports.GetDownward(imp.bucketKey);
            break;
        case Direction::Upward:
            base = m_ports.GetUpward(imp.bucketKey);
            break;
        case Direction::SameLevel:
            base = m_ports.GetSameLevel(imp.dim, imp.bucketKey);
            break;
        }

        const std::vector<uint32_t> avail = getAvailCopy(imp);

        std::vector<RegionKey> regions;
        if (imp.dir == Direction::Downward)
        {
            if (auto maybeRk = buildDownwardRegionForBucketKey(imp.bucketKey); maybeRk.has_value())
            {
                regions.push_back(*maybeRk);
            }
        }
        else if (imp.dir == Direction::Upward && imp.bucketKey.empty())
        {
            if (!cachedUpwardRegions.has_value())
            {
                cachedUpwardRegions = buildUpwardRegionsForEmptyKey();
            }
            regions = *cachedUpwardRegions;
        }
        else
        {
            if (auto maybeRk = buildPrefixRegionFromKey(imp.bucketKey); maybeRk.has_value())
            {
                regions.push_back(*maybeRk);
            }
        }
        if (regions.empty())
        {
            continue;
        }

        // Update exception table only if the available set differs from base.
        if (base == avail)
        {
            for (const auto& rk : regions)
            {
                RemoveExceptionEntry(rk);
            }
            continue;
        }

        for (const auto& rk : regions)
        {
            const std::string pfx = encodeRegionPfx(rk);
            auto emptied = UpdateExceptionWithPrefixIntersections(rk, pfx, avail, &preAvail[idx]);
            for (const auto& e : emptied)
            {
                const std::string dedupKey = std::to_string(e.key.prefixLen) + "|" + e.pfx;
                if (!withdrawTriggered.insert(dedupKey).second)
                {
                    continue;
                }

                const uint64_t epoch = ++m_withdrawEpochCounter;
                const std::string regionKey =
                    std::to_string(selfId) + "|" + std::to_string(e.key.prefixLen) + "|" + e.pfx;
                m_lastEpochSeen[regionKey] = epoch;

                // Broadcast to all directly-connected neighbors (general control-plane "upstream").
                SendWithdrawalToNeighbors(e.key, e.pfx, selfId, epoch, excludeNeighborId);

                // NS_LOG_INFO(Simulator::Now().GetSeconds()
                //             << " WITHDRAW_TRIGGER self=" << selfId
                //             << " epoch=" << epoch
                //             << " plen=" << e.key.prefixLen
                //             << " pfx=" << e.pfx);
            }
        }
    }

    // Debug: print local exception table after processing this interface-down event.
    {
        std::ostringstream r;
        r << "NotifyInterfaceDown if=" << interface;
        DumpExceptionTable(r.str());
    }
}

std::vector<RuleBasedRouting::EmptyExceptionUpdate>
RuleBasedRouting::UpdateExceptionWithPrefixIntersections(const RegionKey& rk,
                                                         const std::string& pfx,
                                                         const std::vector<uint32_t>& candidate,
                                                         const std::vector<uint32_t>* prevCandidateFallback)
{
    std::vector<EmptyExceptionUpdate> emptied;
    if (rk.prefixFields.size() < rk.prefixLen)
    {
        return emptied;
    }

    auto vecToStr = [](const std::vector<uint32_t>& v) -> std::string {
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < v.size(); ++i)
        {
            if (i)
            {
                oss << ",";
            }
            oss << v[i];
        }
        oss << "]";
        return oss.str();
    };

    NS_LOG_INFO("EXC_INSERT plen=" << rk.prefixLen << " pfx=\"" << pfx
                                   << "\" cand=" << vecToStr(candidate));

    std::vector<uint32_t> newCand = candidate;
    ExceptionTrieNode* target = FindOrCreateExceptionNode(rk);
    if (!target)
    {
        return emptied;
    }

    // Intersect with existing ancestor entries (prefix containment).
    ExceptionTrieNode* node = &m_exceptionTrieRoot;
    if (node->entry && rk.prefixLen > 0)
    {
        std::vector<uint32_t> before = newCand;
        newCand = IntersectPortsPreserveLeft(newCand, node->entry->candidateIfs);
        if (newCand != before)
        {
            NS_LOG_INFO("EXC_ANCESTOR_INTERSECT depth=0 ancestor_pfx=\"" << node->entry->pfx
                                                                         << "\" before=" << vecToStr(before)
                                                                         << " ancestor=" << vecToStr(node->entry->candidateIfs)
                                                                         << " after=" << vecToStr(newCand));
        }
    }
    for (uint32_t i = 0; i < rk.prefixLen && !newCand.empty(); ++i)
    {
        const StructuredAddress::Field f = rk.prefixFields[i];
        auto it = node->children.find(f);
        if (it == node->children.end())
        {
            break;
        }
        node = it->second.get();
        if ((i + 1) == rk.prefixLen)
        {
            break;
        }
        if (node->entry)
        {
            std::vector<uint32_t> before = newCand;
            newCand = IntersectPortsPreserveLeft(newCand, node->entry->candidateIfs);
            if (newCand != before)
            {
                NS_LOG_INFO("EXC_ANCESTOR_INTERSECT depth=" << (i + 1)
                                                           << " ancestor_pfx=\"" << node->entry->pfx
                                                           << "\" before=" << vecToStr(before)
                                                           << " ancestor=" << vecToStr(node->entry->candidateIfs)
                                                           << " after=" << vecToStr(newCand));
            }
        }
    }

    // Intersect with existing entry at the same key (if any) to accumulate constraints.
    if (target->entry && !newCand.empty())
    {
        std::vector<uint32_t> before = newCand;
        newCand = IntersectPortsPreserveLeft(newCand, target->entry->candidateIfs);
        if (newCand != before)
        {
            NS_LOG_INFO("EXC_SELF_INTERSECT pfx=\"" << target->entry->pfx
                                                   << "\" before=" << vecToStr(before)
                                                   << " self=" << vecToStr(target->entry->candidateIfs)
                                                   << " after=" << vecToStr(newCand));
        }
    }

    // Previous candidate set for this key (existing exception or fallback).
    std::vector<uint32_t> prevCand;
    bool hasPrev = false;
    if (target->entry)
    {
        prevCand = target->entry->candidateIfs;
        hasPrev = true;
    }
    if (!hasPrev && prevCandidateFallback)
    {
        prevCand = *prevCandidateFallback;
        hasPrev = true;
    }

    const bool isNewEntry = !target->entry.has_value();
    ExceptionEntry exNew;
    exNew.key = rk;
    exNew.candidateIfs = newCand;
    exNew.gw = std::nullopt;
    exNew.pfx = pfx;
    target->entry = std::move(exNew);
    if (isNewEntry)
    {
        ++m_exceptionEntryCount;
    }

    if (hasPrev && !prevCand.empty() && newCand.empty())
    {
        emptied.push_back({rk, pfx});
        NS_LOG_INFO("EXC_EMPTY plen=" << rk.prefixLen << " pfx=\"" << pfx
                                      << "\" prev=" << vecToStr(prevCand)
                                      << " after=" << vecToStr(newCand));
    }

    // Intersect descendant entries with C(k).
    auto updateDescendants = [&](auto&& self, ExceptionTrieNode* cur) -> void {
        for (auto& childKv : cur->children)
        {
            ExceptionTrieNode* child = childKv.second.get();
            if (child->entry)
            {
                const std::vector<uint32_t>& before = child->entry->candidateIfs;
                std::vector<uint32_t> after = IntersectPortsPreserveLeft(before, candidate);
                if (after != before)
                {
                    NS_LOG_INFO("EXC_DESC_INTERSECT child_pfx=\"" << child->entry->pfx
                                                                  << "\" before=" << vecToStr(before)
                                                                  << " parent=" << vecToStr(candidate)
                                                                  << " after=" << vecToStr(after));
                    bool wasNonEmpty = !before.empty();
                    child->entry->candidateIfs = std::move(after);
                    if (wasNonEmpty && child->entry->candidateIfs.empty())
                    {
                        emptied.push_back({child->entry->key, child->entry->pfx});
                        NS_LOG_INFO("EXC_EMPTY desc_pfx=\"" << child->entry->pfx << "\"");
                    }
                }
            }
            self(self, child);
        }
    };

    updateDescendants(updateDescendants, target);

    return emptied;
}

RuleBasedRouting::ExceptionTrieNode*
RuleBasedRouting::FindOrCreateExceptionNode(const RegionKey& rk)
{
    if (rk.prefixFields.size() < rk.prefixLen)
    {
        return nullptr;
    }
    ExceptionTrieNode* node = &m_exceptionTrieRoot;
    for (uint32_t i = 0; i < rk.prefixLen; ++i)
    {
        const StructuredAddress::Field f = rk.prefixFields[i];
        auto& child = node->children[f];
        if (!child)
        {
            child = std::make_unique<ExceptionTrieNode>();
        }
        node = child.get();
    }
    return node;
}

RuleBasedRouting::ExceptionTrieNode*
RuleBasedRouting::FindExceptionNode(const RegionKey& rk)
{
    if (rk.prefixFields.size() < rk.prefixLen)
    {
        return nullptr;
    }
    ExceptionTrieNode* node = &m_exceptionTrieRoot;
    for (uint32_t i = 0; i < rk.prefixLen; ++i)
    {
        const StructuredAddress::Field f = rk.prefixFields[i];
        auto it = node->children.find(f);
        if (it == node->children.end())
        {
            return nullptr;
        }
        node = it->second.get();
    }
    return node;
}

const RuleBasedRouting::ExceptionTrieNode*
RuleBasedRouting::FindExceptionNode(const RegionKey& rk) const
{
    if (rk.prefixFields.size() < rk.prefixLen)
    {
        return nullptr;
    }
    const ExceptionTrieNode* node = &m_exceptionTrieRoot;
    for (uint32_t i = 0; i < rk.prefixLen; ++i)
    {
        const StructuredAddress::Field f = rk.prefixFields[i];
        auto it = node->children.find(f);
        if (it == node->children.end())
        {
            return nullptr;
        }
        node = it->second.get();
    }
    return node;
}

RuleBasedRouting::ExceptionEntry*
RuleBasedRouting::FindExactException(const RegionKey& rk)
{
    ExceptionTrieNode* node = FindExceptionNode(rk);
    if (!node || !node->entry)
    {
        return nullptr;
    }
    return &(*node->entry);
}

const RuleBasedRouting::ExceptionEntry*
RuleBasedRouting::FindExactException(const RegionKey& rk) const
{
    const ExceptionTrieNode* node = FindExceptionNode(rk);
    if (!node || !node->entry)
    {
        return nullptr;
    }
    return &(*node->entry);
}

bool
RuleBasedRouting::RemoveExceptionEntry(const RegionKey& rk)
{
    if (rk.prefixFields.size() < rk.prefixLen)
    {
        return false;
    }
    if (rk.prefixLen == 0)
    {
        if (!m_exceptionTrieRoot.entry)
        {
            return false;
        }
        m_exceptionTrieRoot.entry.reset();
        if (m_exceptionEntryCount > 0)
        {
            --m_exceptionEntryCount;
        }
        return true;
    }

    std::vector<std::pair<ExceptionTrieNode*, StructuredAddress::Field>> path;
    path.reserve(rk.prefixLen);
    ExceptionTrieNode* node = &m_exceptionTrieRoot;
    for (uint32_t i = 0; i < rk.prefixLen; ++i)
    {
        const StructuredAddress::Field f = rk.prefixFields[i];
        auto it = node->children.find(f);
        if (it == node->children.end())
        {
            return false;
        }
        path.push_back({node, f});
        node = it->second.get();
    }

    if (!node->entry)
    {
        return false;
    }
    node->entry.reset();
    if (m_exceptionEntryCount > 0)
    {
        --m_exceptionEntryCount;
    }

    for (int i = static_cast<int>(path.size()) - 1; i >= 0; --i)
    {
        ExceptionTrieNode* parent = path[static_cast<size_t>(i)].first;
        const StructuredAddress::Field f = path[static_cast<size_t>(i)].second;
        auto it = parent->children.find(f);
        if (it == parent->children.end())
        {
            continue;
        }
        ExceptionTrieNode* child = it->second.get();
        if (child->entry || !child->children.empty())
        {
            break;
        }
        parent->children.erase(it);
    }

    return true;
}

void
RuleBasedRouting::SetExceptionEntry(const RegionKey& rk,
                                    const std::string& pfx,
                                    const std::vector<uint32_t>& candidate,
                                    std::optional<Ipv4Address> gw)
{
    ExceptionTrieNode* node = FindOrCreateExceptionNode(rk);
    if (!node)
    {
        return;
    }
    const bool isNewEntry = !node->entry.has_value();
    ExceptionEntry ex;
    ex.key = rk;
    ex.candidateIfs = candidate;
    ex.gw = gw;
    ex.pfx = pfx;
    node->entry = std::move(ex);
    if (isNewEntry)
    {
        ++m_exceptionEntryCount;
    }
}


std::vector<uint32_t>
RuleBasedRouting::ParseKeyFields(const std::string& key)
{
    // key is like "1.4.0" (no brackets).
    std::vector<uint32_t> out;
    if (key.empty())
    {
        return out;
    }
    std::istringstream iss(key);
    std::string tok;
    while (std::getline(iss, tok, '.'))
    {
        if (tok.empty())
        {
            continue;
        }
        out.push_back(static_cast<uint32_t>(std::stoul(tok)));
    }
    return out;
}

void
RuleBasedRouting::DumpExceptionTable(const std::string& reason) const
{
    if (!WithdrawTraceEnabled())
    {
        return;
    }
    if (!m_ipv4)
    {
        return;
    }

    Ptr<Node> selfNode = m_ipv4->GetObject<Node>();
    const uint32_t selfId = selfNode ? selfNode->GetId() : 0;

    struct OrderedEntry
    {
        uint32_t plen{0};
        std::string pfx;
        const ExceptionEntry* ex{nullptr};
    };

    std::vector<OrderedEntry> entries;
    auto collect = [&](auto&& self, const ExceptionTrieNode* node) -> void {
        if (node->entry)
        {
            OrderedEntry e;
            e.plen = node->entry->key.prefixLen;
            e.pfx = node->entry->pfx;
            e.ex = &(*node->entry);
            entries.push_back(std::move(e));
        }
        for (const auto& kv : node->children)
        {
            self(self, kv.second.get());
        }
    };
    collect(collect, &m_exceptionTrieRoot);

    std::cout << Simulator::Now().GetSeconds()
              << " EXC_TABLE self=" << selfId
              << " reason=\"" << reason << "\""
              << " entries=" << entries.size()
              << std::endl;

    if (entries.empty())
    {
        std::cout << "  (empty)" << std::endl;
        return;
    }

    std::sort(entries.begin(), entries.end(), [](const OrderedEntry& a, const OrderedEntry& b) {
        if (a.plen != b.plen)
        {
            return a.plen < b.plen;
        }
        return a.pfx < b.pfx;
    });

    for (const auto& entry : entries)
    {
        const ExceptionEntry* ex = entry.ex;
        std::cout << "  plen=" << entry.plen << " pfx=\"" << entry.pfx << "\" cand=[";
        for (size_t i = 0; i < ex->candidateIfs.size(); ++i)
        {
            if (i)
            {
                std::cout << ",";
            }
            std::cout << ex->candidateIfs[i];
        }
        std::cout << "]";
        if (ex->gw.has_value())
        {
            std::cout << " gw=" << *ex->gw;
        }
        std::cout << std::endl;
    }
}


// std::vector<RuleBasedRouting::RegionKey>
// RuleBasedRouting::BuildImpactedRegionsForDownwardPort(uint32_t interface) const
// {
//     std::vector<RegionKey> regions;

//     if (!m_topo)
//     {
//         return regions;
//     }
//     if (m_levelId == 0)
//     {
//         // level 0 has no downward in a strict hierarchy
//         return regions;
//     }

//     const int levelStart = m_topo->GetLevelAddrBit(m_levelId);
//     const int childStart = m_topo->GetLevelAddrBit(m_levelId - 1);
//     if (levelStart < 0 || childStart >= levelStart)
//     {
//         return regions;
//     }

//     const size_t addrSize = m_src.Size();
//     if (addrSize == 0 || m_levelId > addrSize)
//     {
//         NS_LOG_WARN("m_src too short for levelId=" << m_levelId << " srcSize=" << addrSize);
//         return regions;
//     }
//     const size_t currentPrefixLen = addrSize - m_levelId;

//     const auto& keys = m_ports.GetDownwardKeysByPort(interface);
//     const size_t expectedSegLen = static_cast<size_t>(levelStart - childStart);

//     for (const std::string& bucketKey : keys)
//     {
//         std::vector<uint32_t> seg = ParseKeyFields(bucketKey);
//         if (seg.size() != expectedSegLen)
//         {
//             NS_LOG_WARN("Downward bucketKey '" << bucketKey << "' segLen=" << seg.size()
//                                               << " expected=" << expectedSegLen
//                                               << " (levelStart=" << levelStart
//                                               << ", childStart=" << childStart << ")");
//             continue;
//         }

//         RegionKey rk;
//         const size_t prefixLen = currentPrefixLen + seg.size();
//         rk.prefixLen = static_cast<uint32_t>(prefixLen);
//         rk.prefixFields.resize(prefixLen);

//         // current layer prefix (MSB-first)
//         for (size_t i = 0; i < currentPrefixLen; ++i)
//         {
//             size_t lsbIdx = (addrSize - 1) - i;
//             rk.prefixFields[i] = m_src[lsbIdx];
//         }
//         // append downward bucket key (already MSB-first in string)
//         for (size_t i = 0; i < seg.size(); ++i)
//         {
//             rk.prefixFields[currentPrefixLen + i] = seg[i];
//         }

//         regions.push_back(std::move(rk));
//     }

//     return regions;
// }

std::optional<RuleBasedRouting::Decision>
RuleBasedRouting::Evaluate(const StructuredAddress& dst, const RoutingContext& ctx) const
{
    RoutingRuleManager* ruleManager = RoutingRuleManager::GetInstance();

    if (!ruleManager->HasRules(m_levelId))
    {
        return std::nullopt;
    }

    const auto& rules = ruleManager->GetRules(m_levelId);
    if (rules.empty())
    {
        return std::nullopt;
    }

    // Exception table (priority/LPM) has precedence over the default hierarchical evaluation.
    if (m_exceptionEntryCount > 0)
    {
        const ExceptionEntry* ex = LookupException(dst);
        if (ex)
        {
            auto maybeOutIf = ex->PickEgress(ctx.GetFlowHash());
            // Exception overrides default evaluation; empty candidate => unreachable at this node for that region.
            if (!maybeOutIf.has_value())
            {
                return std::nullopt;
            }

            uint32_t outIf = *maybeOutIf;
            if (m_ipv4 && outIf < m_ipv4->GetNInterfaces())
            {
                Decision d;
                d.outIf = outIf;
                d.gw = ex->gw;
                return d;
            }
            NS_LOG_WARN("Exception selected interface index " << outIf << " is invalid on this node");
            return std::nullopt;
        }
    }

    const StructuredAddress& src = ctx.GetSrc();

    // Iterate rules by priority (desc), stop at first that yields an egress
    for (const auto& rule : rules)
    {
        if (!rule->Match(src, dst, ctx))
        {
            continue;
        }
        auto maybePort = rule->Action(m_ports, src, dst, ctx);
        if (!maybePort.has_value())
        {
            continue;
        }

        uint32_t outIf = *maybePort;
        if (m_ipv4 && outIf >= m_ipv4->GetNInterfaces())
        {
            NS_LOG_WARN("Selected interface index " << outIf << " is invalid on this node");
            continue;
        }

        Decision d;
        d.outIf = outIf;
        // Gateway logic: treat as on-link by default (L2 resolves via ARP/ND).
        // If your rule encodes a remote next-hop, extend Action() to also return gw.
        d.gw = std::nullopt;
        return d;
    }
    return std::nullopt;
}

RoutingContext
RuleBasedRouting::BuildRoutingContext(uint64_t flowHash) const
{
    RoutingContext ctx;
    ctx.levelId = m_levelId;
    ctx.localIdx = m_localIdx;
    ctx.SetFlowHash(flowHash);
    ctx.SetSrcRef(m_src);
    ctx.topo = m_topo;
    return ctx;
}

IngressInfo
RuleBasedRouting::BuildIngressInfo(Ptr<const NetDevice> idev, bool isInject)
{
    if (isInject)
    {
        return IngressInfo::Inject();
    }
    if (!idev || !m_ipv4)
    {
        return IngressInfo::Unknown();
    }
    EnsureIngressClassifier();
    int32_t iif = m_ipv4->GetInterfaceForDevice(idev);
    if (iif < 0)
    {
        return IngressInfo::Unknown();
    }
    return m_ingressClassifier.Get(static_cast<uint32_t>(iif));
}

std::optional<uint32_t>
RuleBasedRouting::GetIncomingPeerNodeIdOwningIp(Ptr<const NetDevice> idev, Ipv4Address ip) const
{
    if (!idev)
    {
        return std::nullopt;
    }
    Ptr<Channel> ch = idev->GetChannel();
    if (!ch)
    {
        return std::nullopt;
    }

    for (uint32_t k = 0; k < ch->GetNDevices(); ++k)
    {
        Ptr<NetDevice> peerDev = ch->GetDevice(k);
        if (!peerDev || PeekPointer(peerDev) == PeekPointer(idev))
        {
            continue;
        }
        Ptr<Node> peerNode = peerDev->GetNode();
        Ptr<Ipv4> peerIpv4 = peerNode ? peerNode->GetObject<Ipv4>() : nullptr;
        if (!peerIpv4)
        {
            continue;
        }
        for (uint32_t ifIdx = 0; ifIdx < peerIpv4->GetNInterfaces(); ++ifIdx)
        {
            const uint32_t nAddr = peerIpv4->GetNAddresses(ifIdx);
            for (uint32_t addrIdx = 0; addrIdx < nAddr; ++addrIdx)
            {
                if (peerIpv4->GetAddress(ifIdx, addrIdx).GetLocal() == ip)
                {
                    return peerNode->GetId();
                }
            }
        }
    }
    return std::nullopt;
}

void
RuleBasedRouting::EnsureIngressClassifier()
{
    if (m_ingressReady || !m_ipv4)
    {
        return;
    }
    uint32_t nIfs = m_ipv4->GetNInterfaces();
    if (nIfs == 0)
    {
        return;
    }
    m_ingressClassifier.Build(m_ports, nIfs);
    m_ingressReady = true;
}

bool
RuleBasedRouting::RulesRequireFlowHash() const
{
    if (!m_rulesRequireFlowHashCached)
    {
        m_rulesRequireFlowHash =
            RoutingRuleManager::GetInstance()->LevelRequiresFlowHash(m_levelId);
        m_rulesRequireFlowHashCached = true;
    }
    return m_rulesRequireFlowHash;
}

// ------------------- RegionKey / ExceptionEntry helpers ---------------------

bool
RuleBasedRouting::RegionKey::Matches(const StructuredAddress& dst) const
{
    if (prefixLen == 0)
    {
        return true;
    }
    if (dst.Size() < prefixLen || prefixFields.size() < prefixLen)
    {
        return false;
    }
    const size_t n = dst.Size();
    for (uint32_t i = 0; i < prefixLen; ++i)
    {
        size_t dstIdx = (n - 1) - static_cast<size_t>(i);
        if (dst[dstIdx] != prefixFields[i])
        {
            return false;
        }
    }
    return true;
}

std::string
RuleBasedRouting::RegionKey::Encode() const
{
    // Encode as: "<prefixLen>|<d0>.<d1>..." (no brackets)
    std::ostringstream oss;
    oss << prefixLen << "|";
    for (uint32_t i = 0; i < prefixLen && i < prefixFields.size(); ++i)
    {
        if (i)
        {
            oss << ".";
        }
        oss << prefixFields[i];
    }
    return oss.str();
}

std::optional<uint32_t>
RuleBasedRouting::ExceptionEntry::PickEgress(uint64_t flowHash) const
{
    if (candidateIfs.empty())
    {
        return std::nullopt;
    }
    uint64_t idx = flowHash % candidateIfs.size();
    return candidateIfs[static_cast<size_t>(idx)];
}

std::string
RuleBasedRouting::EncodePrefix(const StructuredAddress& dst, uint32_t prefixLen)
{
    // Encode MSB-first prefix fields: "<d0>.<d1>..." (no prefixLen)
    if (prefixLen == 0)
    {
        return std::string();
    }
    std::ostringstream oss;
    uint32_t n = std::min<uint32_t>(prefixLen, static_cast<uint32_t>(dst.Size()));
    const size_t size = dst.Size();
    for (uint32_t i = 0; i < n; ++i)
    {
        if (i)
        {
            oss << ".";
        }
        size_t dstIdx = (size - 1) - static_cast<size_t>(i);
        oss << dst[dstIdx];
    }
    return oss.str();
}

const RuleBasedRouting::ExceptionEntry*
RuleBasedRouting::LookupException(const StructuredAddress& dst) const
{
    const ExceptionEntry* best = nullptr;
    const ExceptionTrieNode* node = &m_exceptionTrieRoot;
    if (node->entry)
    {
        best = &(*node->entry);
    }

    const size_t size = dst.Size();
    for (size_t i = 0; i < size; ++i)
    {
        const StructuredAddress::Field f = dst[(size - 1) - i];
        auto it = node->children.find(f);
        if (it == node->children.end())
        {
            break;
        }
        node = it->second.get();
        if (node->entry)
        {
            best = &(*node->entry);
        }
    }

    return best;
}

std::optional<Ipv4Address>
RuleBasedRouting::GetPeerGateway(uint32_t outIf) const
{
    if (!m_ipv4)
    {
        return std::nullopt;
    }

    const uint32_t nIf = m_ipv4->GetNInterfaces();
    if (!m_peerGatewayCacheReady)
    {
        m_peerGatewayByIf.assign(nIf, std::nullopt);
        for (uint32_t ifIndex = 0; ifIndex < nIf; ++ifIndex)
        {
            Ptr<NetDevice> outDevice = m_ipv4->GetNetDevice(ifIndex);
            if (!outDevice)
            {
                continue;
            }
            Ptr<PointToPointChannel> channel = DynamicCast<PointToPointChannel>(outDevice->GetChannel());
            if (!channel)
            {
                continue;
            }
            Ptr<NetDevice> remoteDevice = nullptr;
            for (uint32_t i = 0; i < channel->GetNDevices(); ++i)
            {
                if (channel->GetDevice(i) != outDevice)
                {
                    remoteDevice = channel->GetDevice(i);
                    break;
                }
            }
            if (!remoteDevice || !remoteDevice->GetNode())
            {
                continue;
            }
            Ptr<Ipv4> remoteIpv4 = remoteDevice->GetNode()->GetObject<Ipv4>();
            if (!remoteIpv4)
            {
                continue;
            }
            int32_t remoteIf = remoteIpv4->GetInterfaceForDevice(remoteDevice);
            if (remoteIf < 0 || remoteIpv4->GetNAddresses(static_cast<uint32_t>(remoteIf)) == 0)
            {
                continue;
            }
            m_peerGatewayByIf[ifIndex] =
                remoteIpv4->GetAddress(static_cast<uint32_t>(remoteIf), 0).GetLocal();
        }
        m_peerGatewayCacheReady = true;
    }

    if (outIf >= m_peerGatewayByIf.size())
    {
        return std::nullopt;
    }
    return m_peerGatewayByIf[outIf];
}

Ptr<Ipv4Route>
RuleBasedRouting::Lookup(const Ipv4Header& header,
                         Ptr<const Packet> packet,
                         Ptr<NetDevice> oif,
                         Ptr<const NetDevice> idev,
                         bool isInject)
{
    NS_LOG_FUNCTION(this << header << oif);

    // Compute a flow hash for consistent selection (5-tuple)
    // Use node ID as seed for per-node path diversity
    uint32_t seed = m_ipv4 ? m_ipv4->GetObject<Node>()->GetId() : 0;
    RoutingContext ctx = BuildRoutingContext(0);
    if (m_nonMinimalPolicy || m_exceptionEntryCount > 0 || RulesRequireFlowHash())
    {
        ctx.SetFlowHashProvider(header, packet, seed, &RuleBasedRouting::ComputeFlowHash);
    }
    const StructuredAddress& finalDst =
        StructuredAddressDirectory::Get()->LookupRef(header.GetDestination());
    const StructuredAddress* effectiveDst = &finalDst;
    StructuredAddress nonMinimalDst;

    if (m_nonMinimalPolicy)
    {
        NonMinimalRoutingTag tag;
        bool hasTag = false;
        if (packet)
        {
            hasTag = packet->PeekPacketTag(tag);
        }
        if (!hasTag)
        {
            tag.Reset();
        }

        IngressInfo ingress = BuildIngressInfo(idev, isInject);
        if (isInject && m_levelId > 0)
        {
            ingress.meta["sourceRouter"] = 1;
            ingress.meta["packetSourceNodeId"] = seed;
            ingress.meta["packetSourceFlowHash"] = ctx.GetFlowHash();
        }
        auto sourcePeerNodeId =
            isInject ? std::optional<uint32_t>{}
                     : GetIncomingPeerNodeIdOwningIp(idev, header.GetSource());
        if (sourcePeerNodeId.has_value())
        {
            ingress.meta["fromPacketSource"] = 1;
            if (m_levelId > 0)
            {
                ingress.meta["sourceRouter"] = 1;
            }
            ingress.meta["packetSourceNodeId"] = *sourcePeerNodeId;
            ingress.meta["packetSourceFlowHash"] =
                ComputeFlowHash(header, packet, *sourcePeerNodeId);
        }

        NonMinimalPolicy::ProbeCallback probe =
            [this, &ctx](const StructuredAddress& dst,
                         PortSelectPolicy policyOverride) -> NonMinimalPolicy::ProbeResult {
            (void)policyOverride;
            NonMinimalPolicy::ProbeResult res;
            auto decision = Evaluate(dst, ctx);
            if (!decision)
            {
                return res;
            }
            res.outIf = decision->outIf;
            if (m_congestionProvider && m_ipv4 && decision->outIf < m_ipv4->GetNInterfaces())
            {
                Ptr<NetDevice> dev = m_ipv4->GetNetDevice(decision->outIf);
                res.metric = m_congestionProvider->GetMetric(dev);
            }
            return res;
        };

        auto nmDecision =
            m_nonMinimalPolicy->Decide(ctx, finalDst, ingress, tag, probe);
        nonMinimalDst = std::move(nmDecision.effectiveDst);
        effectiveDst = &nonMinimalDst;

        if (nmDecision.tagUpdated && packet)
        {
            Ptr<Packet> mutablePacket = ConstCast<Packet>(packet);
            NonMinimalRoutingTag oldTag;
            if (mutablePacket->PeekPacketTag(oldTag))
            {
                mutablePacket->RemovePacketTag(oldTag);
            }
            mutablePacket->AddPacketTag(nmDecision.tag);
        }
    }

    auto decision = Evaluate(*effectiveDst, ctx);
    if (!decision)
    {
        return nullptr;
    }

    // If oif is constrained by upper layer, honor it
    if (oif != nullptr)
    {
        if (m_ipv4->GetNetDevice(decision->outIf) != oif)
        {
            // Different from requested OIF; reject
            return nullptr;
        }
    }

    // Build route

    Ptr<Ipv4Route> rt = Create<Ipv4Route>();
    rt->SetDestination(header.GetDestination());

    // Source address: pick the primary address on the selected interface
    const Ipv4InterfaceAddress ifAddr = m_ipv4->GetAddress(decision->outIf, 0);
    rt->SetSource(ifAddr.GetLocal());

    rt->SetOutputDevice(m_ipv4->GetNetDevice(decision->outIf));
    if (decision->gw.has_value())
    {
        rt->SetGateway(*decision->gw);
    }
    else
    {
        rt->SetGateway(GetPeerGateway(decision->outIf).value_or(header.GetDestination()));
    }

    return rt;
}

Ptr<Ipv4Route>
RuleBasedRouting::RouteOutput(Ptr<Packet> p,
                              const Ipv4Header& header,
                              Ptr<NetDevice> oif,
                              Socket::SocketErrno& sockerr)
{
    NS_LOG_FUNCTION(this << header << oif);

    if (!m_ipv4)
    {
        NS_LOG_ERROR("Ipv4 is not set");
        return nullptr;
    }

    Ptr<Ipv4Route> rt = Lookup(header, p, oif, nullptr, true);

    if (rt)
    {
        sockerr = Socket::ERROR_NOTERROR;
    }
    else
    {
        sockerr = Socket::ERROR_NOROUTETOHOST;
    }
    return rt;
}

bool
RuleBasedRouting::RouteInput(Ptr<const Packet> p,
                             const Ipv4Header& header,
                             Ptr<const NetDevice> idev,
                             const UnicastForwardCallback& ucb,
                             const MulticastForwardCallback& mcb,
                             const LocalDeliverCallback& lcb,
                             const ErrorCallback& ecb)
{
    NS_LOG_FUNCTION(this << header << idev);

    if (!m_ipv4)
    {
        NS_LOG_ERROR("Ipv4 is not set");
        return false;
    }

    NS_ASSERT(m_ipv4->GetInterfaceForDevice(idev) >= 0);
    uint32_t iif = m_ipv4->GetInterfaceForDevice(idev);

    if (m_ipv4->IsDestinationAddress(header.GetDestination(), iif))
    {
        if (!lcb.IsNull())
        {
            NS_LOG_LOGIC("Local delivery to " << header.GetDestination());
            lcb(p, header, iif);
            return true;
        }
        else
        {
            // The local delivery callback is null.  This may be a multicast
            // or broadcast packet, so return false so that another
            // multicast routing protocol can handle it.  It should be possible
            // to extend this to explicitly check whether it is a unicast
            // packet, and invoke the error callback if so
            return false;
        }
    }

    // Check if input device supports IP forwarding
    if (!m_ipv4->IsForwarding(iif))
    {
        NS_LOG_LOGIC("Forwarding disabled for this interface");
        ecb(p, header, Socket::ERROR_NOROUTETOHOST);
        return true;
    }
    // Next, try to find a route
    NS_LOG_LOGIC("Unicast destination- looking up global route");
    Ptr<Ipv4Route> rtentry = Lookup(header, p, nullptr, idev, false);
    if (rtentry)
    {
        NS_LOG_LOGIC("Found unicast destination- calling unicast callback");
        ucb(rtentry, p, header);
        return true;
    }
    else
    {
        NS_LOG_LOGIC("Did not find unicast destination- returning false");
        return false; // Let other routing protocols try to handle this
                      // route request.
    }
}

// ------------------- Failure withdrawal propagation ---------------------

std::optional<uint32_t>
RuleBasedRouting::GetInterfaceToNeighbor(uint32_t neighborNodeId) const
{
    if (!m_ipv4)
    {
        return std::nullopt;
    }

    uint32_t nIf = m_ipv4->GetNInterfaces();
    for (uint32_t ifIdx = 1; ifIdx < nIf; ++ifIdx)
    {
        Ptr<NetDevice> dev = m_ipv4->GetNetDevice(ifIdx);
        if (!dev)
        {
            continue;
        }
        Ptr<Channel> ch = dev->GetChannel();
        if (!ch)
        {
            continue;
        }
        for (uint32_t k = 0; k < ch->GetNDevices(); ++k)
        {
            Ptr<NetDevice> peerDev = ch->GetDevice(k);
            if (!peerDev || peerDev == dev)
            {
                continue;
            }
            Ptr<Node> peerNode = peerDev->GetNode();
            if (peerNode && peerNode->GetId() == neighborNodeId)
            {
                return ifIdx;
            }
        }
    }
    return std::nullopt;
}

std::vector<uint32_t>
RuleBasedRouting::GetUpstreamNeighborNodeIds() const
{
    // NOTE: despite the name, this returns ALL directly-connected neighbor nodes.
    std::vector<uint32_t> out;
    if (!m_ipv4)
    {
        return out;
    }

    const uint32_t nIf = m_ipv4->GetNInterfaces();
    for (uint32_t ifIdx = 1; ifIdx < nIf; ++ifIdx)
    {
        Ptr<NetDevice> dev = m_ipv4->GetNetDevice(ifIdx);
        if (!dev)
        {
            continue;
        }
        Ptr<Channel> ch = dev->GetChannel();
        if (!ch)
        {
            continue;
        }
        for (uint32_t k = 0; k < ch->GetNDevices(); ++k)
        {
            Ptr<NetDevice> peerDev = ch->GetDevice(k);
            if (!peerDev || peerDev == dev)
            {
                continue;
            }
            Ptr<Node> peerNode = peerDev->GetNode();
            if (!peerNode)
            {
                continue;
            }
            uint32_t peerId = peerNode->GetId();
            if (std::find(out.begin(), out.end(), peerId) == out.end())
            {
                out.push_back(peerId);
            }
        }
    }
    return out;
}

std::optional<std::string>
RuleBasedRouting::ExtractDownwardBucketKeyFromRegion(const RegionKey& rk) const
{
    if (!m_topo || m_levelId == 0)
    {
        return std::nullopt;
    }
    const int levelStart = m_topo->GetLevelAddrBit(m_levelId);
    const int childStart = m_topo->GetLevelAddrBit(m_levelId - 1);
    if (levelStart < 0 || childStart >= levelStart)
    {
        return std::nullopt;
    }

    const size_t addrSize = m_src.Size();
    if (addrSize == 0 || m_levelId > addrSize)
    {
        return std::nullopt;
    }

    const size_t currentPrefixLen = addrSize - m_levelId;
    const size_t segLen = static_cast<size_t>(levelStart - childStart);
    const size_t needLen = currentPrefixLen + segLen;

    if (rk.prefixFields.size() < needLen || rk.prefixLen < needLen)
    {
        return std::nullopt;
    }

    std::ostringstream oss;
    for (size_t i = 0; i < segLen; ++i)
    {
        if (i)
        {
            oss << ".";
        }
        oss << rk.prefixFields[currentPrefixLen + i];
    }
    return oss.str();
}

// Helper: retrieve RuleBasedRouting from an Ipv4RoutingProtocol (handles Ipv4ListRouting wrapper).
static Ptr<RuleBasedRouting>
FindRuleBased(Ptr<Ipv4RoutingProtocol> rp)
{
    if (!rp)
    {
        return nullptr;
    }

    if (auto rbr = DynamicCast<RuleBasedRouting>(rp))
    {
        return rbr;
    }

    if (auto list = DynamicCast<Ipv4ListRouting>(rp))
    {
        for (uint32_t i = 0; i < list->GetNRoutingProtocols(); ++i)
        {
            int16_t priority = 0;
            Ptr<Ipv4RoutingProtocol> sub = list->GetRoutingProtocol(i, priority);
            if (auto rbr2 = DynamicCast<RuleBasedRouting>(sub))
            {
                return rbr2;
            }
        }
    }

    return nullptr;
}

void
RuleBasedRouting::SendWithdrawalToNeighbors(const RegionKey& rk,
                                            const std::string& pfx,
                                            uint32_t originNodeId,
                                            uint64_t epoch,
                                            std::optional<uint32_t> excludeNeighborId) const
{
    if (!m_ipv4)
    {
        return;
    }

    Ptr<Node> selfNode = m_ipv4->GetObject<Node>();
    uint32_t selfId = selfNode ? selfNode->GetId() : 0;

    const auto neighborIds = GetUpstreamNeighborNodeIds(); // returns all directly-connected neighbors
    std::unordered_set<uint32_t> uniqPeers(neighborIds.begin(), neighborIds.end());
    for (uint32_t peerId : uniqPeers)
    {
        if (excludeNeighborId.has_value() && peerId == *excludeNeighborId)
        {
            continue;
        }

        // Identify the local interface connected to peerId.
        auto maybeIf = GetInterfaceToNeighbor(peerId);
        if (!maybeIf.has_value())
        {
            continue;
        }
        uint32_t ifIdx = *maybeIf;

        Ptr<NetDevice> dev = m_ipv4->GetNetDevice(ifIdx);
        if (!dev)
        {
            continue;
        }
        Ptr<Channel> ch = dev->GetChannel();
        if (!ch)
        {
            continue;
        }

        Ptr<Node> peerNode;
        for (uint32_t k = 0; k < ch->GetNDevices(); ++k)
        {
            Ptr<NetDevice> peerDev = ch->GetDevice(k);
            if (!peerDev || peerDev == dev)
            {
                continue;
            }
            peerNode = peerDev->GetNode();
            if (peerNode && peerNode->GetId() == peerId)
            {
                break;
            }
        }
        if (!peerNode)
        {
            continue;
        }

        Ptr<Ipv4> peerIpv4 = peerNode->GetObject<Ipv4>();
        if (!peerIpv4)
        {
            continue;
        }

        Ptr<Ipv4RoutingProtocol> rp = peerIpv4->GetRoutingProtocol();
        Ptr<RuleBasedRouting> peerRbr = FindRuleBased(rp);
        if (!peerRbr)
        {
            // NS_LOG_WARN("Neighbor node " << peerId << " has no RuleBasedRouting; skip withdrawal");
            continue;
        }

        // if (WithdrawTraceEnabled())
        // {
        //     std::cout << Simulator::Now().GetSeconds()
        //               << " WITHDRAW_SEND"
        //               << " self=" << selfId
        //               << " -> peer=" << peerId
        //               << " origin=" << originNodeId
        //               << " epoch=" << epoch
        //               << " region(plen=" << rk.prefixLen << ",pfx=" << pfx << ")"
        //               << (excludeNeighborId ? (" exclude=" + std::to_string(*excludeNeighborId))
        //                                     : "")
        //               << std::endl;
        // }

        // Dispatch immediately to take effect before any same-time events.
        EnqueueWithdrawalImmediate(peerRbr, rk, pfx, originNodeId, epoch, selfId);
    }
}

void
RuleBasedRouting::EnqueueWithdrawalImmediate(Ptr<RuleBasedRouting> target,
                                             const RegionKey& rk,
                                             const std::string& pfx,
                                             uint32_t originNodeId,
                                             uint64_t epoch,
                                             uint32_t fromNeighborNodeId) const
{
    g_withdrawQueue.push_back(
        PendingWithdrawal{target, rk, pfx, originNodeId, epoch, fromNeighborNodeId});
    if (g_withdrawDispatching)
    {
        return;
    }

    g_withdrawDispatching = true;
    for (size_t i = 0; i < g_withdrawQueue.size(); ++i)
    {
        // Copy the item to avoid reference invalidation if nested withdrawals
        // push into the same vector and trigger reallocation.
        PendingWithdrawal item = g_withdrawQueue[i];
        if (item.target)
        {
            item.target->HandleWithdrawalFromNeighbor(item.rk,
                                                      item.pfx,
                                                      item.originNodeId,
                                                      item.epoch,
                                                      item.fromNeighborNodeId);
        }
    }
    g_withdrawQueue.clear();
    g_withdrawDispatching = false;
}

void
RuleBasedRouting::HandleWithdrawalFromNeighbor(const RegionKey& rk,
                                               const std::string& pfx,
                                               uint32_t originNodeId,
                                               uint64_t epoch,
                                               uint32_t fromNeighborNodeId)
{
    // if (WithdrawTraceEnabled())
    // {
    //     std::cout << Simulator::Now().GetSeconds()
    //               << " WITHDRAW_RECV"
    //               << " self=" << (m_ipv4->GetObject<Node>()->GetId())
    //               << " from=" << fromNeighborNodeId
    //               << " origin=" << originNodeId
    //               << " epoch=" << epoch
    //               << " plen=" << rk.prefixLen
    //               << " pfx=" << pfx
    //               << std::endl;
    // }

    // Per-sender dedup within a single failure update.
    // For the same (origin, epoch), do not accept multiple withdrawals from the same sender.
    // This prevents duplicate propagation artifacts where one neighbor repeatedly triggers
    // multiple withdraw notifications during the same link failure update.
    const std::string senderKey =
        std::to_string(fromNeighborNodeId) + "|" + std::to_string(originNodeId);
    auto itSender = m_lastEpochSeenFromSender.find(senderKey);
    if (itSender != m_lastEpochSeenFromSender.end() && epoch <= itSender->second)
    {
        return;
    }
    m_lastEpochSeenFromSender[senderKey] = epoch;


    // Versioning: ignore older or duplicate epochs for the same (origin, region).
    const std::string regionKey =
        std::to_string(originNodeId) + "|" + std::to_string(rk.prefixLen) + "|" + pfx;
    auto itSeen = m_lastEpochSeen.find(regionKey);
    // IMPORTANT:
    // Do NOT drop same-epoch withdrawals, because the same (origin, epoch, region)
    // may legitimately arrive from multiple different neighbors; each must remove
    // its own avoidIf from the candidate set.
    // The per-sender gate (m_lastEpochSeenFromSender) already prevents duplicates
    // from the same neighbor within the same failure update.
    if (itSeen != m_lastEpochSeen.end() && epoch < itSeen->second)
    {
        return;
    }
    m_lastEpochSeen[regionKey] = epoch;

    // Identify which local interface leads to the neighbor that is withdrawing.
    auto maybeAvoidIf = GetInterfaceToNeighbor(fromNeighborNodeId);
    if (!maybeAvoidIf.has_value())
    {
        return;
    }
    uint32_t avoidIf = *maybeAvoidIf;

    auto containsPort = [](const std::vector<uint32_t>& v, uint32_t p) -> bool {
        return std::find(v.begin(), v.end(), p) != v.end();
    };

    // Compute this node's candidate set for the region and remove avoidIf.
    std::vector<uint32_t> cand;

    // 1) If an exception already exists for this region, start from it.
    if (const ExceptionEntry* ex = FindExactException(rk))
    {
        cand = ex->candidateIfs;
    }

    // 2) Otherwise, derive from the appropriate available candidate pool that actually uses avoidIf.
    if (cand.empty())
    {
        // (a) Try downward bucket inferred from RegionKey (level-aligned case).
        if (auto maybeKey = ExtractDownwardBucketKeyFromRegion(rk); maybeKey.has_value())
        {
            const auto& downIfs = m_ports.GetAvailableDownward(*maybeKey);
            if (containsPort(downIfs, avoidIf))
            {
                cand = downIfs;
            }
        }

        // (b) Try upward buckets keyed by the region prefix string (pfx), then fallback to "".
        if (cand.empty())
        {
            const auto& upBuckets = m_ports.GetUpwardBuckets();
            auto itUp = upBuckets.find(pfx);
            if (itUp != upBuckets.end())
            {
                const auto& upIfs = m_ports.GetAvailableUpward(pfx);
                if (containsPort(upIfs, avoidIf))
                {
                    cand = upIfs;
                }
            }
            if (cand.empty())
            {
                const auto& upIfs = m_ports.GetAvailableUpward("");
                if (containsPort(upIfs, avoidIf))
                {
                    cand = upIfs;
                }
            }
        }

        // (c) Try same-level buckets keyed by pfx (dimension unknown in withdrawal; probe all).
        if (cand.empty())
        {
            const auto& sl = m_ports.GetSameLevelBuckets();
            for (uint32_t dim = 0; dim < sl.size(); ++dim)
            {
                auto itSl = sl[dim].find(pfx);
                if (itSl == sl[dim].end())
                {
                    continue;
                }
                const auto& slIfs = m_ports.GetAvailableSameLevel(dim, pfx);
                if (containsPort(slIfs, avoidIf))
                {
                    cand = slIfs;
                    break;
                }
            }
        }

        if (cand.empty())
        {
            // This node does not use the withdrawing neighbor for this region (or cannot map region -> bucket).
            return;
        }
    }

    // If this node never uses fromNeighbor for this region, no update nor propagation is needed.
    std::vector<uint32_t> candBeforeRemoval = cand;
    auto it = std::remove(cand.begin(), cand.end(), avoidIf);
    if (it == cand.end())
    {
        return;
    }
    cand.erase(it, cand.end());

    auto emptied = UpdateExceptionWithPrefixIntersections(rk, pfx, cand, &candBeforeRemoval);
    if (!emptied.empty())
    {
        std::unordered_set<std::string> withdrawTriggered;
        for (const auto& e : emptied)
        {
            const std::string dedupKey = std::to_string(e.key.prefixLen) + "|" + e.pfx;
            if (!withdrawTriggered.insert(dedupKey).second)
            {
                continue;
            }

            const std::string regionKey =
                std::to_string(originNodeId) + "|" + std::to_string(e.key.prefixLen) + "|" + e.pfx;
            m_lastEpochSeen[regionKey] = epoch;

            // This node is now unreachable for this region; propagate to all neighbors except the sender.
            SendWithdrawalToNeighbors(e.key, e.pfx, originNodeId, epoch, fromNeighborNodeId);
        }
    }

    // Debug: print local exception table after applying withdrawal update.
    // {
    //     std::ostringstream r;
    //     r << "HandleWithdrawalFromNeighbor from=" << fromNeighborNodeId
    //       << " origin=" << originNodeId
    //       << " epoch=" << epoch
    //       << " plen=" << rk.prefixLen
    //       << " pfx=" << pfx;
    //     DumpExceptionTable(r.str());
    // }
}



void
RuleBasedRouting::PrintRoutingTable(Ptr<OutputStreamWrapper> stream, Time::Unit unit) const
{
    std::ostream* os = stream->GetStream();
    *os << "RuleBasedRouting table @ " << Simulator::Now().As(unit) << std::endl;
    *os << "  interfaces: " << (m_ipv4 ? m_ipv4->GetNInterfaces() : 0) << std::endl;
    *os << "  rules: " << RoutingRuleManager::GetInstance()->GetRules(m_levelId).size()
        << " (sorted by priority desc)" << std::endl;
    *os << "  port buckets: (sizes)\n";

    // TODO: Print port buckets
}

} // namespace ns3
