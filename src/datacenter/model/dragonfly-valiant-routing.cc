/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "dragonfly-valiant-routing.h"

#include "non-minimal-routing-tag.h"
#include "structured-address-directory.h"

#include "ns3/enum.h"
#include "ns3/ipv4.h"
#include "ns3/log.h"
#include "ns3/net-device.h"
#include "ns3/node.h"
#include "ns3/point-to-point-channel.h"
#include "ns3/simulator.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <queue>
#include <sstream>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("DragonflyValiantRouting");

NS_OBJECT_ENSURE_REGISTERED(DragonflyValiantRouting);

TypeId
DragonflyValiantRouting::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::DragonflyValiantRouting")
            .SetParent<Ipv4RoutingProtocol>()
            .SetGroupName("Datacenter")
            .AddConstructor<DragonflyValiantRouting>()
            .AddAttribute("RouterLevel",
                          "Level id for router nodes in StructuredTopology.",
                          UintegerValue(1),
                          MakeUintegerAccessor(&DragonflyValiantRouting::SetRouterLevel),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("GroupDimId",
                          "Dimension id encoding group ID in StructuredAddress.",
                          UintegerValue(1),
                          MakeUintegerAccessor(&DragonflyValiantRouting::SetGroupDimId),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("Seed",
                          "Seed for deterministic Valiant group selection.",
                          UintegerValue(1),
                          MakeUintegerAccessor(&DragonflyValiantRouting::SetSeed),
                          MakeUintegerChecker<uint64_t>());
    return tid;
}

DragonflyValiantRouting::DragonflyValiantRouting()
    : m_ipv4(nullptr),
      m_node(nullptr),
      m_topo(nullptr)
{
    NS_LOG_FUNCTION(this);
}

DragonflyValiantRouting::DragonflyValiantRouting(Ptr<Node> node, Ptr<StructuredTopology> topo)
    : m_ipv4(nullptr),
      m_node(node),
      m_topo(topo)
{
    NS_LOG_FUNCTION(this);
    if (m_node)
    {
        m_nodeId = m_node->GetId();
    }
}

DragonflyValiantRouting::~DragonflyValiantRouting()
{
    NS_LOG_FUNCTION(this);
}

void
DragonflyValiantRouting::SetTopology(Ptr<StructuredTopology> topo)
{
    NS_LOG_FUNCTION(this << topo);
    m_topo = topo;
    m_cacheBuilt = false;
}

void
DragonflyValiantRouting::SetRouterLevel(uint32_t level)
{
    m_routerLevel = level;
    m_cacheBuilt = false;
}

void
DragonflyValiantRouting::SetGroupDimId(uint32_t dimId)
{
    m_groupDimId = dimId;
    m_cacheBuilt = false;
}

void
DragonflyValiantRouting::SetSeed(uint64_t seed)
{
    m_seed = seed;
}

void
DragonflyValiantRouting::DoDispose()
{
    NS_LOG_FUNCTION(this);
    m_topo = nullptr;
    m_ipv4 = nullptr;
    m_node = nullptr;
    Ipv4RoutingProtocol::DoDispose();
}

void
DragonflyValiantRouting::SetIpv4(Ptr<Ipv4> ipv4)
{
    NS_LOG_FUNCTION(this << ipv4);
    NS_ASSERT(!m_ipv4 && ipv4);
    m_ipv4 = ipv4;
    m_node = ipv4->GetObject<Node>();
    if (m_node)
    {
        m_nodeId = m_node->GetId();
    }
    m_cacheBuilt = false;
}

void
DragonflyValiantRouting::NotifyInterfaceUp(uint32_t interface)
{
    NS_LOG_FUNCTION(this << interface);
    m_cacheBuilt = false;
}

void
DragonflyValiantRouting::NotifyInterfaceDown(uint32_t interface)
{
    NS_LOG_FUNCTION(this << interface);
    m_cacheBuilt = false;
}

void
DragonflyValiantRouting::NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address)
{
    NS_LOG_FUNCTION(this << interface << address);
    m_cacheBuilt = false;
}

void
DragonflyValiantRouting::NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress address)
{
    NS_LOG_FUNCTION(this << interface << address);
    m_cacheBuilt = false;
}

void
DragonflyValiantRouting::PrintRoutingTable(Ptr<OutputStreamWrapper> stream, Time::Unit unit) const
{
    (void)stream;
    (void)unit;
}

void
DragonflyValiantRouting::SetTraceRouting(bool enable)
{
    m_traceRouting = enable;
}

void
DragonflyValiantRouting::SetTraceStream(std::ostream* os)
{
    m_traceStream = os;
}

void
DragonflyValiantRouting::SetTraceVerbose(bool enable)
{
    m_traceVerbose = enable;
}

void
DragonflyValiantRouting::DumpCaches(std::ostream& os)
{
    if (!EnsureCaches())
    {
        os << "DragonflyValiantRouting: cache not ready" << std::endl;
        return;
    }

    auto sortedKeys = [](const auto& m) {
        std::vector<uint32_t> keys;
        keys.reserve(m.size());
        for (const auto& kv : m)
        {
            keys.push_back(kv.first);
        }
        std::sort(keys.begin(), keys.end());
        return keys;
    };

    os << "\n=== DragonflyValiantRouting cache dump (node " << m_nodeId << ") ==="
       << std::endl;

    os << "m_groupByNode (nodeId -> groupId)" << std::endl;
    for (uint32_t id : sortedKeys(m_groupByNode))
    {
        os << "  " << id << " -> " << m_groupByNode.at(id) << std::endl;
    }

    os << "m_neighbors (nodeId -> [neighborId(if,nh), ...])" << std::endl;
    for (uint32_t id : sortedKeys(m_neighbors))
    {
        os << "  " << id << " -> ";
        const auto& vec = m_neighbors.at(id);
        if (vec.empty())
        {
            os << "[]" << std::endl;
            continue;
        }
        for (size_t i = 0; i < vec.size(); ++i)
        {
            if (i)
            {
                os << ", ";
            }
            os << vec[i].nodeId << "(if=" << vec[i].ifIndex << ",nh=" << vec[i].nextHop << ")";
        }
        os << std::endl;
    }

    os << "m_globalNeighborByGroup (routerId -> {groupId: neighborId})" << std::endl;
    for (uint32_t rid : sortedKeys(m_globalNeighborByGroup))
    {
        os << "  " << rid << " -> {";
        const auto& inner = m_globalNeighborByGroup.at(rid);
        std::vector<uint32_t> groups;
        groups.reserve(inner.size());
        for (const auto& kv : inner)
        {
            groups.push_back(kv.first);
        }
        std::sort(groups.begin(), groups.end());
        for (size_t i = 0; i < groups.size(); ++i)
        {
            if (i)
            {
                os << ", ";
            }
            uint32_t g = groups[i];
            os << g << ": " << inner.at(g);
        }
        os << "}" << std::endl;
    }

    os << "m_localNeighbors (routerId -> [neighborId, ...])" << std::endl;
    for (uint32_t rid : sortedKeys(m_localNeighbors))
    {
        os << "  " << rid << " -> ";
        const auto& vec = m_localNeighbors.at(rid);
        if (vec.empty())
        {
            os << "[]" << std::endl;
            continue;
        }
        for (size_t i = 0; i < vec.size(); ++i)
        {
            if (i)
            {
                os << ", ";
            }
            os << vec[i];
        }
        os << std::endl;
    }

    os << "m_hostToRouter (hostId -> routerId)" << std::endl;
    for (uint32_t hid : sortedKeys(m_hostToRouter))
    {
        os << "  " << hid << " -> " << m_hostToRouter.at(hid) << std::endl;
    }

    os << "=== end dump ===" << std::endl;
}

bool
DragonflyValiantRouting::EnsureCaches()
{
    if (m_cacheBuilt)
    {
        return true;
    }
    if (!m_topo || !m_ipv4 || !m_node)
    {
        return false;
    }
    BuildCaches();
    return m_cacheBuilt;
}

uint64_t
DragonflyValiantRouting::ComputeFlowHash(const Ipv4Header& h,
                                         Ptr<const Packet> p,
                                         uint32_t seed) const
{
    union
    {
        uint8_t u8[12];
        uint32_t u32[3];
    } buf;

    buf.u32[0] = h.GetSource().Get();
    buf.u32[1] = h.GetDestination().Get();

    uint16_t sport = 0;
    uint16_t dport = 0;
    if (p && (h.GetProtocol() == 6 || h.GetProtocol() == 17))
    {
        Ptr<Packet> pkt = p->Copy();
        if (pkt->GetSize() >= 4)
        {
            uint8_t portBuf[4];
            pkt->CopyData(portBuf, 4);
            sport = (portBuf[0] << 8) | portBuf[1];
            dport = (portBuf[2] << 8) | portBuf[3];
        }
    }
    buf.u32[2] = sport | (static_cast<uint32_t>(dport) << 16);

    uint32_t hash = seed;
    for (int i = 0; i < 3; ++i)
    {
        uint32_t k = buf.u32[i];
        k *= 0xcc9e2d51;
        k = (k << 15) | (k >> 17);
        k *= 0x1b873593;
        hash ^= k;
        hash = (hash << 13) | (hash >> 19);
        hash = hash * 5 + 0xe6546b64;
    }

    hash ^= 12;
    hash ^= hash >> 16;
    hash *= 0x85ebca6b;
    hash ^= hash >> 13;
    hash *= 0xc2b2ae35;
    hash ^= hash >> 16;
    return hash;
}

uint32_t
DragonflyValiantRouting::GetGroupFromAddr(const StructuredAddress& addr) const
{
    if (!m_topo || addr.Empty())
    {
        return 0;
    }
    int levelAddrBit = m_topo->GetLevelAddrBit(m_routerLevel);
    size_t idx = 0;
    if (levelAddrBit >= 0)
    {
        idx = static_cast<size_t>(levelAddrBit + m_groupDimId);
        if (idx >= addr.Size())
        {
            idx = addr.Size() - 1;
        }
    }
    else
    {
        idx = addr.Size() - 1;
    }
    return static_cast<uint32_t>(addr[idx]);
}

uint32_t
DragonflyValiantRouting::GetNodeIdForIp(Ipv4Address ip) const
{
    auto it = m_ipToNode.find(ip.Get());
    if (it == m_ipToNode.end())
    {
        return UINT32_MAX;
    }
    return it->second;
}

bool
DragonflyValiantRouting::IsHost(uint32_t nodeId) const
{
    return m_hostNodes.find(nodeId) != m_hostNodes.end();
}

bool
DragonflyValiantRouting::IsRouter(uint32_t nodeId) const
{
    return m_routerNodes.find(nodeId) != m_routerNodes.end();
}

std::optional<uint32_t>
DragonflyValiantRouting::FindNextHopToRouter(uint32_t srcRouterId,
                                             uint32_t dstRouterId) const
{
    if (srcRouterId == dstRouterId)
    {
        return std::nullopt;
    }

    std::queue<uint32_t> q;
    std::unordered_map<uint32_t, uint32_t> prev;
    q.push(srcRouterId);
    prev[srcRouterId] = srcRouterId;

    while (!q.empty())
    {
        uint32_t u = q.front();
        q.pop();
        if (u == dstRouterId)
        {
            break;
        }
        auto it = m_localNeighbors.find(u);
        if (it == m_localNeighbors.end())
        {
            continue;
        }
        for (uint32_t v : it->second)
        {
            if (prev.find(v) != prev.end())
            {
                continue;
            }
            prev[v] = u;
            q.push(v);
        }
    }

    if (prev.find(dstRouterId) == prev.end())
    {
        return std::nullopt;
    }

    uint32_t v = dstRouterId;
    while (prev[v] != srcRouterId && prev[v] != v)
    {
        v = prev[v];
    }
    return v;
}

std::optional<uint32_t>
DragonflyValiantRouting::FindNextHopToGroup(uint32_t srcRouterId,
                                            uint32_t targetGroup) const
{
    auto itGroup = m_groupByNode.find(srcRouterId);
    if (itGroup == m_groupByNode.end())
    {
        return std::nullopt;
    }
    uint32_t srcGroup = itGroup->second;

    if (m_groups > 0 && m_routersPerGroup > 0 && m_globalLinksPerRouter > 0 &&
        m_groups == m_routersPerGroup * m_globalLinksPerRouter + 1 &&
        srcGroup > 0 && targetGroup > 0 && srcGroup <= m_groups && targetGroup <= m_groups &&
        srcGroup != targetGroup)
    {
        uint32_t s0 = srcGroup - 1;
        uint32_t t0 = targetGroup - 1;
        uint32_t cGroup = std::min(s0, t0);
        uint32_t rGroup = std::max(s0, t0);
        uint32_t cPortIdx = rGroup - 1; // 0..g-2

        uint32_t localIdxMin = cPortIdx / m_globalLinksPerRouter;
        uint32_t localIdxMax = cGroup / m_globalLinksPerRouter;
        uint32_t localIdx = (s0 == cGroup) ? localIdxMin : localIdxMax;

        if (localIdx < m_routersPerGroup)
        {
            uint32_t routerIndex = localIdx + 1; // 1-based
            auto itByGroup = m_routerByGroupIndex.find(srcGroup);
            if (itByGroup != m_routerByGroupIndex.end())
            {
                auto itRouter = itByGroup->second.find(routerIndex);
                if (itRouter != itByGroup->second.end())
                {
                    uint32_t borderRouter = itRouter->second;
                    if (borderRouter != srcRouterId)
                    {
                        auto next = FindNextHopToRouter(srcRouterId, borderRouter);
                        if (next.has_value())
                        {
                            return next;
                        }
                    }
                    else
                    {
                        auto direct = GetGlobalNeighbor(srcRouterId, targetGroup);
                        if (direct.has_value())
                        {
                            return direct;
                        }
                    }
                }
            }
        }
    }

    if (GetGlobalNeighbor(srcRouterId, targetGroup).has_value())
    {
        return GetGlobalNeighbor(srcRouterId, targetGroup);
    }

    std::queue<uint32_t> q;
    std::unordered_map<uint32_t, uint32_t> prev;
    q.push(srcRouterId);
    prev[srcRouterId] = srcRouterId;

    while (!q.empty())
    {
        uint32_t u = q.front();
        q.pop();
        if (GetGlobalNeighbor(u, targetGroup).has_value())
        {
            uint32_t v = u;
            while (prev[v] != srcRouterId && prev[v] != v)
            {
                v = prev[v];
            }
            return v == srcRouterId ? u : v;
        }

        auto it = m_localNeighbors.find(u);
        if (it == m_localNeighbors.end())
        {
            continue;
        }
        for (uint32_t v : it->second)
        {
            if (prev.find(v) != prev.end())
            {
                continue;
            }
            prev[v] = u;
            q.push(v);
        }
    }
    return std::nullopt;
}

std::optional<DragonflyValiantRouting::NeighborInfo>
DragonflyValiantRouting::GetNeighbor(uint32_t nodeId, uint32_t nextNodeId) const
{
    auto it = m_neighbors.find(nodeId);
    if (it == m_neighbors.end())
    {
        return std::nullopt;
    }
    for (const auto& nb : it->second)
    {
        if (nb.nodeId == nextNodeId)
        {
            return nb;
        }
    }
    return std::nullopt;
}

std::optional<uint32_t>
DragonflyValiantRouting::GetGlobalNeighbor(uint32_t nodeId, uint32_t targetGroup) const
{
    auto it = m_globalNeighborByGroup.find(nodeId);
    if (it == m_globalNeighborByGroup.end())
    {
        return std::nullopt;
    }
    auto it2 = it->second.find(targetGroup);
    if (it2 == it->second.end())
    {
        return std::nullopt;
    }
    return it2->second;
}

uint32_t
DragonflyValiantRouting::PickValiantGroup(uint32_t srcGroup,
                                          uint32_t dstGroup,
                                          uint64_t hash) const
{
    if (srcGroup == 0 || dstGroup == 0)
    {
        return dstGroup;
    }
    if (m_groupList.empty())
    {
        return dstGroup;
    }
    const size_t n = m_groupList.size();
    uint64_t h = (hash ^ (m_seed * 0x9e3779b97f4a7c15ULL)) ^ 0x9e3779b97f4a7c15ULL;
    size_t choice = static_cast<size_t>(h % n);
    uint32_t pick = m_groupList[choice];
    if (n > 1)
    {
        for (size_t offset = 0; offset < n; ++offset)
        {
            size_t idx = (choice + offset) % n;
            uint32_t cand = m_groupList[idx];
            const bool forbidden = (cand == srcGroup) || (cand == dstGroup);
            if (!forbidden)
            {
                pick = cand;
                break;
            }
            pick = cand; // fallback if all candidates are forbidden
        }
    }
    return pick;
}

std::optional<DragonflyValiantRouting::NextHop>
DragonflyValiantRouting::LookupNextHop(const Ipv4Header& header,
                                       Ptr<const Packet> p,
                                       bool isInject) const
{
    auto fmtOpt = [](const std::optional<uint32_t>& v) {
        return v.has_value() ? std::to_string(*v) : std::string("na");
    };
    std::optional<uint32_t> srcGroupOpt;
    std::optional<uint32_t> dstGroupOpt;
    std::optional<uint32_t> curGroupOpt;
    std::optional<uint32_t> midGroupOpt;
    std::optional<uint32_t> targetGroupOpt;
    std::string tagStr = "na";

    auto ipToString = [](Ipv4Address ip) {
        std::ostringstream oss;
        oss << ip;
        return oss.str();
    };

    auto traceDecision = [&](const std::string& action,
                             const std::optional<NextHop>& nh,
                             const std::string& reason,
                             const std::string& detail) {
        if (!m_traceRouting || !m_traceStream)
        {
            return;
        }
        auto fmtStructured = [](const Ipv4Address& ip) {
            Ptr<StructuredAddressDirectory> dir = StructuredAddressDirectory::Get();
            if (dir->Has(ip))
            {
                return dir->Lookup(ip).ToString();
            }
            return std::string("na");
        };
        std::ostringstream oss;
        oss << Simulator::Now().GetSeconds() << "\tDVR\t"
            << "node=" << m_nodeId << "\t"
            << "inject=" << (isInject ? 1 : 0) << "\t"
            << "src=" << fmtStructured(header.GetSource()) << "\t"
            << "dst=" << fmtStructured(header.GetDestination()) << "\t"
            << "srcG=" << fmtOpt(srcGroupOpt) << "\t"
            << "dstG=" << fmtOpt(dstGroupOpt) << "\t"
            << "curG=" << fmtOpt(curGroupOpt) << "\t"
            << "midG=" << fmtOpt(midGroupOpt) << "\t"
            << "targetG=" << fmtOpt(targetGroupOpt) << "\t"
            << "tag=" << tagStr << "\t"
            << "action=" << action;
        if (!reason.empty())
        {
            oss << "\t" << "reason=" << reason;
        }
        if (m_traceVerbose && !detail.empty())
        {
            oss << "\t" << "detail=" << detail;
        }
        if (nh)
        {
            oss << "\t" << "if=" << nh->ifIndex << "\t" << "nh=" << nh->nextHop;
        }
        (*m_traceStream) << oss.str() << std::endl;
    };

    uint32_t dstNodeId = GetNodeIdForIp(header.GetDestination());
    if (dstNodeId == UINT32_MAX)
    {
        traceDecision("NO_ROUTE", std::nullopt, "dstNodeUnknown",
                      "dstIp=" + ipToString(header.GetDestination()));
        return std::nullopt;
    }
    if (dstNodeId == m_nodeId)
    {
        traceDecision("LOCAL", std::nullopt, "dstIsSelf", "");
        return std::nullopt;
    }

    bool curIsHost = IsHost(m_nodeId);
    bool dstIsHost = IsHost(dstNodeId);

    std::optional<NextHop> hostNextHop;
    if (curIsHost)
    {
        auto it = m_hostToRouter.find(m_nodeId);
        if (it == m_hostToRouter.end())
        {
            traceDecision("NO_ROUTE", std::nullopt, "hostNoUplink",
                          "hostId=" + std::to_string(m_nodeId));
            return std::nullopt;
        }
        auto nb = GetNeighbor(m_nodeId, it->second);
        if (!nb)
        {
            traceDecision("NO_ROUTE", std::nullopt, "hostUplinkMissingNeighbor",
                          "hostId=" + std::to_string(m_nodeId) + ",routerId=" +
                              std::to_string(it->second));
            return std::nullopt;
        }
        hostNextHop = NextHop{nb->ifIndex, nb->nextHop};
    }
    else if (!IsRouter(m_nodeId))
    {
        traceDecision("NO_ROUTE", std::nullopt, "notRouter",
                      "nodeId=" + std::to_string(m_nodeId));
        return std::nullopt;
    }

    uint32_t dstRouterId = dstNodeId;
    if (dstIsHost)
    {
        auto it = m_hostToRouter.find(dstNodeId);
        if (it == m_hostToRouter.end())
        {
            traceDecision("NO_ROUTE", std::nullopt, "dstHostNoUplink",
                          "dstHostId=" + std::to_string(dstNodeId));
            return std::nullopt;
        }
        dstRouterId = it->second;
    }
    if (dstRouterId == UINT32_MAX)
    {
        traceDecision("NO_ROUTE", std::nullopt, "dstRouterUnknown",
                      "dstHostId=" + std::to_string(dstNodeId));
        return std::nullopt;
    }

    uint32_t srcGroup = 0;
    uint32_t dstGroup = 0;
    StructuredAddress dstAddr;

    Ptr<StructuredAddressDirectory> dir = StructuredAddressDirectory::Get();
    if (dir->Has(header.GetSource()))
    {
        StructuredAddress srcAddr = dir->Lookup(header.GetSource());
        srcGroup = GetGroupFromAddr(srcAddr);
    }
    else
    {
        auto itCur = m_groupByNode.find(m_nodeId);
        if (itCur != m_groupByNode.end())
        {
            srcGroup = itCur->second;
        }
    }

    if (dir->Has(header.GetDestination()))
    {
        dstAddr = dir->Lookup(header.GetDestination());
        dstGroup = GetGroupFromAddr(dstAddr);
    }
    else
    {
        traceDecision("NO_ROUTE", std::nullopt, "dstAddrMissing",
                      "dstIp=" + ipToString(header.GetDestination()));
        return std::nullopt;
    }
    srcGroupOpt = srcGroup;
    dstGroupOpt = dstGroup;

    auto itGroup = m_groupByNode.find(m_nodeId);
    if (itGroup == m_groupByNode.end())
    {
        traceDecision("NO_ROUTE", std::nullopt, "curGroupUnknown",
                      "nodeId=" + std::to_string(m_nodeId));
        return std::nullopt;
    }
    uint32_t curGroup = itGroup->second;
    curGroupOpt = curGroup;

    uint32_t seed = m_nodeId;
    auto itSeed = m_ipToNode.find(header.GetSource().Get());
    if (itSeed != m_ipToNode.end())
    {
        seed = itSeed->second;
    }
    uint64_t h = ComputeFlowHash(header, p, seed);
    uint32_t midGroup = PickValiantGroup(srcGroup, dstGroup, h);
    midGroupOpt = midGroup;

    auto matchesRewrites =
        [](const StructuredAddress& addr,
           const std::vector<NonMinimalRoutingTag::RewriteEntry>& rewrites) -> bool {
        for (const auto& entry : rewrites)
        {
            if (entry.index >= addr.Size())
            {
                return false;
            }
            if (addr[entry.index] != entry.value)
            {
                return false;
            }
        }
        return true;
    };

    auto applyRewrites =
        [](StructuredAddress base,
           const std::vector<NonMinimalRoutingTag::RewriteEntry>& rewrites) -> StructuredAddress {
        for (const auto& entry : rewrites)
        {
            if (entry.index < base.Size())
            {
                base[entry.index] = entry.value;
            }
        }
        return base;
    };

    std::optional<uint16_t> groupFieldIndex;
    if (!dstAddr.Empty())
    {
        size_t idx = dstAddr.Size() - 1;
        if (m_topo)
        {
            int levelAddrBit = m_topo->GetLevelAddrBit(m_routerLevel);
            if (levelAddrBit >= 0)
            {
                size_t candidate = static_cast<size_t>(levelAddrBit + m_groupDimId);
                if (candidate < dstAddr.Size())
                {
                    idx = candidate;
                }
            }
        }
        groupFieldIndex = static_cast<uint16_t>(idx);
    }

    NonMinimalRoutingTag tag;
    bool hasTag = false;
    if (p)
    {
        hasTag = p->PeekPacketTag(tag);
    }
    if (!hasTag)
    {
        tag.Reset();
    }
    auto updateTagStr = [&]() {
        std::ostringstream oss;
        oss << static_cast<int>(tag.GetAlgorithm()) << "/"
            << static_cast<int>(tag.GetPhase());
        if (tag.HasRewrites())
        {
            oss << "/rw=[";
            const auto& rws = tag.GetRewrites();
            for (size_t i = 0; i < rws.size(); ++i)
            {
                if (i)
                {
                    oss << ",";
                }
                oss << rws[i].index << ":" << rws[i].value;
            }
            oss << "]";
        }
        tagStr = oss.str();
    };
    updateTagStr();

    bool tagUpdated = false;
    bool hasActiveTransit = false;
    if (tag.GetAlgorithm() == NonMinimalAlgorithm::kValiant &&
        tag.GetPhase() == NonMinimalPhase::kToTransit && tag.HasRewrites())
    {
        hasActiveTransit = true;
        auto itAddr = m_nodeAddr.find(m_nodeId);
        if (itAddr != m_nodeAddr.end() &&
            matchesRewrites(itAddr->second, tag.GetRewrites()))
        {
            NonMinimalRoutingTag nextTag = tag;
            nextTag.SetPhase(NonMinimalPhase::kToFinal);
            nextTag.ClearRewrite();
            tag = nextTag;
            tagUpdated = true;
            hasActiveTransit = false;
            updateTagStr();
        }
    }

    if (isInject && p && groupFieldIndex.has_value() && midGroup != 0 && dstGroup != 0)
    {
        NonMinimalRoutingTag nextTag;
        nextTag.Reset();
        nextTag.SetAlgorithm(NonMinimalAlgorithm::kValiant);
        nextTag.SetPhase(NonMinimalPhase::kToTransit);
        nextTag.AddRewrite(*groupFieldIndex, midGroup);
        tag = nextTag;
        tagUpdated = true;
        hasActiveTransit = true;
        updateTagStr();
    }

    uint32_t targetGroup = dstGroup;
    if (hasActiveTransit)
    {
        StructuredAddress effectiveDst = applyRewrites(dstAddr, tag.GetRewrites());
        targetGroup = GetGroupFromAddr(effectiveDst);
    }
    targetGroupOpt = targetGroup;

    if (tagUpdated && p)
    {
        Ptr<Packet> mutablePacket = ConstCast<Packet>(p);
        NonMinimalRoutingTag oldTag;
        if (mutablePacket->PeekPacketTag(oldTag))
        {
            mutablePacket->RemovePacketTag(oldTag);
        }
        mutablePacket->AddPacketTag(tag);
    }

    if (curIsHost)
    {
        traceDecision("HOST_UPLINK", hostNextHop, "", "");
        return hostNextHop;
    }

    if (curGroup == targetGroup)
    {
        if (m_nodeId == dstRouterId)
        {
            if (!dstIsHost)
            {
                traceDecision("LOCAL", std::nullopt, "dstIsRouterSelf", "");
                return std::nullopt;
            }
            auto nb = GetNeighbor(m_nodeId, dstNodeId);
            if (!nb)
            {
                traceDecision("NO_ROUTE", std::nullopt, "dstHostNeighborMissing",
                              "dstHostId=" + std::to_string(dstNodeId));
                return std::nullopt;
            }
            traceDecision("TO_HOST", NextHop{nb->ifIndex, nb->nextHop}, "", "");
            return NextHop{nb->ifIndex, nb->nextHop};
        }

        auto nextRouter = FindNextHopToRouter(m_nodeId, dstRouterId);
        if (!nextRouter.has_value())
        {
            traceDecision("NO_ROUTE", std::nullopt, "localBfsFail",
                          "dstRouterId=" + std::to_string(dstRouterId));
            return std::nullopt;
        }
        auto nb = GetNeighbor(m_nodeId, *nextRouter);
        if (!nb)
        {
            traceDecision("NO_ROUTE", std::nullopt, "localNextHopMissing",
                          "nextRouterId=" + std::to_string(*nextRouter));
            return std::nullopt;
        }
        traceDecision("LOCAL_BFS", NextHop{nb->ifIndex, nb->nextHop}, "", "");
        return NextHop{nb->ifIndex, nb->nextHop};
    }

    auto directGlobal = GetGlobalNeighbor(m_nodeId, targetGroup);
    if (directGlobal.has_value())
    {
        auto nb = GetNeighbor(m_nodeId, *directGlobal);
        if (!nb)
        {
            traceDecision("NO_ROUTE", std::nullopt, "directGlobalMissingNeighbor",
                          "directRouterId=" + std::to_string(*directGlobal));
            return std::nullopt;
        }
        traceDecision("GLOBAL_DIRECT", NextHop{nb->ifIndex, nb->nextHop}, "", "");
        return NextHop{nb->ifIndex, nb->nextHop};
    }

    auto nextRouter = FindNextHopToGroup(m_nodeId, targetGroup);
    if (!nextRouter.has_value())
    {
        traceDecision("NO_ROUTE", std::nullopt, "globalBfsFail",
                      "targetGroup=" + std::to_string(targetGroup));
        return std::nullopt;
    }
    auto nb = GetNeighbor(m_nodeId, *nextRouter);
    if (!nb)
    {
        traceDecision("NO_ROUTE", std::nullopt, "globalNextHopMissing",
                      "nextRouterId=" + std::to_string(*nextRouter));
        return std::nullopt;
    }
    traceDecision("GLOBAL_BFS", NextHop{nb->ifIndex, nb->nextHop}, "", "");
    return NextHop{nb->ifIndex, nb->nextHop};
}

Ptr<Ipv4Route>
DragonflyValiantRouting::RouteOutput(Ptr<Packet> p,
                                     const Ipv4Header& header,
                                     Ptr<NetDevice> oif,
                                     Socket::SocketErrno& sockerr)
{
    NS_LOG_FUNCTION(this << header << oif);
    if (header.GetDestination().IsMulticast())
    {
        return nullptr;
    }
    if (!EnsureCaches())
    {
        sockerr = Socket::ERROR_NOROUTETOHOST;
        return nullptr;
    }

    auto nh = LookupNextHop(header, p, true);
    if (!nh)
    {
        sockerr = Socket::ERROR_NOROUTETOHOST;
        return nullptr;
    }

    if (oif && m_ipv4->GetNetDevice(nh->ifIndex) != oif)
    {
        sockerr = Socket::ERROR_NOROUTETOHOST;
        return nullptr;
    }

    Ptr<Ipv4Route> rt = Create<Ipv4Route>();
    rt->SetDestination(header.GetDestination());
    const Ipv4InterfaceAddress ifAddr = m_ipv4->GetAddress(nh->ifIndex, 0);
    rt->SetSource(ifAddr.GetLocal());
    rt->SetGateway(nh->nextHop);
    rt->SetOutputDevice(m_ipv4->GetNetDevice(nh->ifIndex));
    sockerr = Socket::ERROR_NOTERROR;
    return rt;
}

bool
DragonflyValiantRouting::RouteInput(Ptr<const Packet> p,
                                    const Ipv4Header& header,
                                    Ptr<const NetDevice> idev,
                                    const UnicastForwardCallback& ucb,
                                    const MulticastForwardCallback& mcb,
                                    const LocalDeliverCallback& lcb,
                                    const ErrorCallback& ecb)
{
    (void)mcb;
    NS_LOG_FUNCTION(this << p << header << idev);
    NS_ASSERT(m_ipv4->GetInterfaceForDevice(idev) >= 0);
    uint32_t iif = m_ipv4->GetInterfaceForDevice(idev);

    if (!EnsureCaches())
    {
        ecb(p, header, Socket::ERROR_NOROUTETOHOST);
        return true;
    }

    if (m_ipv4->IsDestinationAddress(header.GetDestination(), iif))
    {
        if (!lcb.IsNull())
        {
            lcb(p, header, iif);
            return true;
        }
        return false;
    }

    if (!m_ipv4->IsForwarding(iif))
    {
        ecb(p, header, Socket::ERROR_NOROUTETOHOST);
        return true;
    }

    auto nh = LookupNextHop(header, p, false);
    if (!nh)
    {
        return false;
    }

    Ptr<Ipv4Route> rt = Create<Ipv4Route>();
    rt->SetDestination(header.GetDestination());
    const Ipv4InterfaceAddress ifAddr = m_ipv4->GetAddress(nh->ifIndex, 0);
    rt->SetSource(ifAddr.GetLocal());
    rt->SetGateway(nh->nextHop);
    rt->SetOutputDevice(m_ipv4->GetNetDevice(nh->ifIndex));
    ucb(rt, p, header);
    return true;
}

void
DragonflyValiantRouting::BuildCaches()
{
    m_cacheBuilt = false;
    m_nodeLevel.clear();
    m_nodeAddr.clear();
    m_groupByNode.clear();
    m_routerNodes.clear();
    m_hostNodes.clear();
    m_neighbors.clear();
    m_hostToRouter.clear();
    m_ipToNode.clear();
    m_localNeighbors.clear();
    m_globalNeighborByGroup.clear();
    m_groupList.clear();
    m_routerIndexByNode.clear();
    m_routerByGroupIndex.clear();
    m_routerLocalIdxByNode.clear();
    m_routerNodeByLocalIdx.clear();
    m_groups = 0;
    m_routersPerGroup = 0;
    m_globalLinksPerRouter = 0;

    if (!m_topo || !m_node)
    {
        return;
    }

    std::unordered_map<uint32_t, std::vector<uint32_t>> routersByGroup;

    uint32_t levels = m_topo->GetNumLevels();
    for (uint32_t lv = 0; lv < levels; ++lv)
    {
        const NodeContainer& nodes = m_topo->GetLevel(lv);
        if (lv == m_routerLevel)
        {
            m_routerNodeByLocalIdx.assign(nodes.GetN(), UINT32_MAX);
        }
        for (uint32_t i = 0; i < nodes.GetN(); ++i)
        {
            Ptr<Node> n = nodes.Get(i);
            uint32_t nodeId = n->GetId();
            m_nodeLevel[nodeId] = lv;
            StructuredAddress addr = m_topo->GetStructuredAddrByLocal(lv, i);
            m_nodeAddr[nodeId] = addr;
            uint32_t group = GetGroupFromAddr(addr);
            m_groupByNode[nodeId] = group;
            if (lv == m_routerLevel)
            {
                m_routerNodes.insert(nodeId);
                m_routerLocalIdxByNode[nodeId] = i;
                if (i < m_routerNodeByLocalIdx.size())
                {
                    m_routerNodeByLocalIdx[i] = nodeId;
                }
                if (group != 0)
                {
                    routersByGroup[group].push_back(nodeId);
                }
            }
            if (lv == 0)
            {
                m_hostNodes.insert(nodeId);
            }
        }
    }

    for (const auto& kv : routersByGroup)
    {
        m_groupList.push_back(kv.first);
    }
    std::sort(m_groupList.begin(), m_groupList.end());
    m_groups = static_cast<uint32_t>(m_groupList.size());

    if (!m_groupList.empty())
    {
        m_routersPerGroup = static_cast<uint32_t>(routersByGroup[m_groupList[0]].size());
        for (uint32_t g : m_groupList)
        {
            if (routersByGroup[g].size() != m_routersPerGroup)
            {
                m_routersPerGroup = 0;
                break;
            }
        }
    }
    if (m_groups > 0 && m_routersPerGroup > 0)
    {
        for (uint32_t localIdx = 0; localIdx < m_routerNodeByLocalIdx.size(); ++localIdx)
        {
            uint32_t nodeId = m_routerNodeByLocalIdx[localIdx];
            if (nodeId == UINT32_MAX)
            {
                continue;
            }
            uint32_t g = (localIdx / m_routersPerGroup) + 1;
            uint32_t rIndex = (localIdx % m_routersPerGroup) + 1;
            m_routerIndexByNode[nodeId] = rIndex;
            m_routerByGroupIndex[g][rIndex] = nodeId;
        }
    }

    const NodeContainer& all = m_topo->GetAll();
    for (uint32_t i = 0; i < all.GetN(); ++i)
    {
        Ptr<Node> node = all.Get(i);
        Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
        if (!ipv4)
        {
            continue;
        }

        uint32_t nodeId = node->GetId();
        uint32_t nIf = ipv4->GetNInterfaces();
        for (uint32_t ifIdx = 0; ifIdx < nIf; ++ifIdx)
        {
            uint32_t nAddr = ipv4->GetNAddresses(ifIdx);
            for (uint32_t addrIdx = 0; addrIdx < nAddr; ++addrIdx)
            {
                m_ipToNode[ipv4->GetAddress(ifIdx, addrIdx).GetLocal().Get()] = nodeId;
            }
        }

        for (uint32_t ifIdx = 1; ifIdx < nIf; ++ifIdx)
        {
            Ptr<NetDevice> dev = ipv4->GetNetDevice(ifIdx);
            Ptr<PointToPointChannel> channel = DynamicCast<PointToPointChannel>(dev->GetChannel());
            if (!channel)
            {
                continue;
            }
            Ptr<NetDevice> remoteDev = nullptr;
            for (uint32_t k = 0; k < channel->GetNDevices(); ++k)
            {
                if (channel->GetDevice(k) != dev)
                {
                    remoteDev = channel->GetDevice(k);
                    break;
                }
            }
            if (!remoteDev)
            {
                continue;
            }
            Ptr<Node> remoteNode = remoteDev->GetNode();
            Ptr<Ipv4> remoteIpv4 = remoteNode->GetObject<Ipv4>();
            if (!remoteIpv4)
            {
                continue;
            }
            int32_t remoteIf = remoteIpv4->GetInterfaceForDevice(remoteDev);
            if (remoteIf < 0)
            {
                continue;
            }
            Ipv4Address nextHop = remoteIpv4->GetAddress(static_cast<uint32_t>(remoteIf), 0)
                                      .GetLocal();
            NeighborInfo info;
            info.nodeId = remoteNode->GetId();
            info.ifIndex = ifIdx;
            info.nextHop = nextHop;
            m_neighbors[nodeId].push_back(info);
        }
    }

    for (uint32_t hostId : m_hostNodes)
    {
        auto it = m_neighbors.find(hostId);
        if (it == m_neighbors.end())
        {
            continue;
        }
        for (const auto& nb : it->second)
        {
            if (IsRouter(nb.nodeId))
            {
                m_hostToRouter[hostId] = nb.nodeId;
                break;
            }
        }
    }

    for (uint32_t routerId : m_routerNodes)
    {
        uint32_t g = m_groupByNode[routerId];
        auto it = m_neighbors.find(routerId);
        if (it == m_neighbors.end())
        {
            continue;
        }
        for (const auto& nb : it->second)
        {
            if (!IsRouter(nb.nodeId))
            {
                continue;
            }
            uint32_t ng = m_groupByNode[nb.nodeId];
            if (ng == g)
            {
                m_localNeighbors[routerId].push_back(nb.nodeId);
            }
            else
            {
                auto& byGroup = m_globalNeighborByGroup[routerId];
                if (byGroup.find(ng) == byGroup.end())
                {
                    byGroup[ng] = nb.nodeId;
                }
            }
        }
    }

    if (m_groups > 0 && m_routersPerGroup > 0 &&
        (m_groups - 1) % m_routersPerGroup == 0)
    {
        m_globalLinksPerRouter = (m_groups - 1) / m_routersPerGroup;
    }
    else
    {
        for (const auto& kv : m_globalNeighborByGroup)
        {
            m_globalLinksPerRouter =
                std::max<uint32_t>(m_globalLinksPerRouter,
                                   static_cast<uint32_t>(kv.second.size()));
        }
    }

    m_cacheBuilt = true;
}

} // namespace ns3
