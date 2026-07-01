/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "torus-detour-routing.h"

#include "non-minimal-routing-tag.h"
#include "structured-address-directory.h"

#include "ns3/ipv4.h"
#include "ns3/log.h"
#include "ns3/net-device.h"
#include "ns3/node.h"
#include "ns3/point-to-point-channel.h"
#include "ns3/uinteger.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("TorusDetourRouting");

NS_OBJECT_ENSURE_REGISTERED(TorusDetourRouting);

TypeId
TorusDetourRouting::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::TorusDetourRouting")
            .SetParent<Ipv4RoutingProtocol>()
            .SetGroupName("Datacenter")
            .AddConstructor<TorusDetourRouting>()
            .AddAttribute("TorusLevel",
                          "Level id for torus nodes in StructuredTopology.",
                          UintegerValue(0),
                          MakeUintegerAccessor(&TorusDetourRouting::SetTorusLevel),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("DetourStages",
                          "Number of detour stages.",
                          UintegerValue(1),
                          MakeUintegerAccessor(&TorusDetourRouting::SetDetourStages),
                          MakeUintegerChecker<uint32_t>());
    return tid;
}

TorusDetourRouting::TorusDetourRouting()
    : m_ipv4(nullptr),
      m_node(nullptr),
      m_topo(nullptr)
{
    NS_LOG_FUNCTION(this);
    m_detourPolicy = CreateObject<DetourPolicy>();
}

TorusDetourRouting::TorusDetourRouting(Ptr<Node> node, const StructuredTopology* topo)
    : m_ipv4(nullptr),
      m_node(node),
      m_topo(topo)
{
    NS_LOG_FUNCTION(this);
    if (m_node)
    {
        m_nodeId = m_node->GetId();
    }
    m_detourPolicy = CreateObject<DetourPolicy>();
}

TorusDetourRouting::~TorusDetourRouting()
{
    NS_LOG_FUNCTION(this);
}

void
TorusDetourRouting::SetTopology(const StructuredTopology* topo)
{
    NS_LOG_FUNCTION(this << topo);
    m_topo = topo;
    m_cacheBuilt = false;
}

void
TorusDetourRouting::SetTorusLevel(uint32_t level)
{
    m_torusLevel = level;
    m_cacheBuilt = false;
}

void
TorusDetourRouting::SetTransitFields(const std::vector<uint16_t>& fields)
{
    m_transitFields = fields;
    if (m_detourPolicy)
    {
        m_detourPolicy->SetTransitFields(fields);
    }
}

void
TorusDetourRouting::SetDetourStages(uint8_t stages)
{
    m_detourStages = stages == 0 ? 1 : stages;
    if (m_detourPolicy)
    {
        m_detourPolicy->SetStages(m_detourStages);
    }
}

void
TorusDetourRouting::DoDispose()
{
    NS_LOG_FUNCTION(this);
    m_topo = nullptr;
    m_ipv4 = nullptr;
    m_node = nullptr;
    m_detourPolicy = nullptr;
    Ipv4RoutingProtocol::DoDispose();
}

void
TorusDetourRouting::SetIpv4(Ptr<Ipv4> ipv4)
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
TorusDetourRouting::NotifyInterfaceUp(uint32_t interface)
{
    NS_LOG_FUNCTION(this << interface);
    m_cacheBuilt = false;
}

void
TorusDetourRouting::NotifyInterfaceDown(uint32_t interface)
{
    NS_LOG_FUNCTION(this << interface);
    m_cacheBuilt = false;
}

void
TorusDetourRouting::NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address)
{
    NS_LOG_FUNCTION(this << interface << address);
    m_cacheBuilt = false;
}

void
TorusDetourRouting::NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress address)
{
    NS_LOG_FUNCTION(this << interface << address);
    m_cacheBuilt = false;
}

void
TorusDetourRouting::PrintRoutingTable(Ptr<OutputStreamWrapper> stream, Time::Unit unit) const
{
    (void)stream;
    (void)unit;
}

bool
TorusDetourRouting::EnsureCaches()
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
TorusDetourRouting::ComputeFlowHash(const Ipv4Header& h,
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
TorusDetourRouting::GetNodeIdForIp(Ipv4Address ip) const
{
    auto it = m_ipToNode.find(ip.Get());
    if (it == m_ipToNode.end())
    {
        return UINT32_MAX;
    }
    return it->second;
}

std::optional<TorusDetourRouting::NeighborInfo>
TorusDetourRouting::SelectNeighbor(uint32_t nodeId,
                                   size_t dimIdx,
                                   uint8_t direction,
                                   uint64_t flowHash) const
{
    auto it = m_dirNeighbors.find(nodeId);
    if (it == m_dirNeighbors.end())
    {
        return std::nullopt;
    }
    if (dimIdx >= it->second.size() || direction > 1)
    {
        return std::nullopt;
    }
    const auto& buckets = it->second[dimIdx][direction];
    if (buckets.empty())
    {
        return std::nullopt;
    }
    if (buckets.size() == 1)
    {
        return buckets.front();
    }
    size_t idx = static_cast<size_t>(flowHash % buckets.size());
    return buckets[idx];
}

std::optional<TorusDetourRouting::NextHop>
TorusDetourRouting::LookupNextHop(const Ipv4Header& header,
                                  Ptr<const Packet> p,
                                  bool isInject) const
{
    uint32_t dstNodeId = GetNodeIdForIp(header.GetDestination());
    if (dstNodeId == UINT32_MAX || dstNodeId == m_nodeId)
    {
        return std::nullopt;
    }

    auto itAddr = m_nodeAddr.find(m_nodeId);
    if (itAddr == m_nodeAddr.end())
    {
        return std::nullopt;
    }
    StructuredAddress curAddr = itAddr->second;

    Ptr<StructuredAddressDirectory> dir = StructuredAddressDirectory::Get();
    if (!dir->Has(header.GetDestination()))
    {
        return std::nullopt;
    }
    StructuredAddress finalDst = dir->Lookup(header.GetDestination());

    RoutingContext ctx;
    ctx.levelId = m_torusLevel;
    ctx.localIdx = 0;
    ctx.SetFlowHash(ComputeFlowHash(header, p, m_nodeId));
    ctx.src = curAddr;
    ctx.topo = m_topo;

    IngressInfo ingress = isInject ? IngressInfo::Inject() : IngressInfo::Unknown();
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

    if (!m_detourPolicy)
    {
        Ptr<TorusDetourRouting> self = const_cast<TorusDetourRouting*>(this);
        self->m_detourPolicy = CreateObject<DetourPolicy>();
        self->m_detourPolicy->SetTransitFields(m_transitFields);
        self->m_detourPolicy->SetStages(m_detourStages);
    }

    NonMinimalPolicy::ProbeCallback probe =
        [](const StructuredAddress&, PortSelectPolicy) -> NonMinimalPolicy::ProbeResult {
        return {};
    };

    auto decision = m_detourPolicy->Decide(ctx, finalDst, ingress, tag, probe);
    StructuredAddress effectiveDst = decision.effectiveDst;

    if (decision.tagUpdated && p)
    {
        Ptr<Packet> mutablePacket = ConstCast<Packet>(p);
        NonMinimalRoutingTag oldTag;
        if (mutablePacket->PeekPacketTag(oldTag))
        {
            mutablePacket->RemovePacketTag(oldTag);
        }
        mutablePacket->AddPacketTag(decision.tag);
    }

    size_t dims = std::min({curAddr.Size(), effectiveDst.Size(), m_dimValues.size()});
    if (dims == 0)
    {
        return std::nullopt;
    }

    for (size_t idx = dims; idx-- > 0;)
    {
        if (curAddr[idx] == effectiveDst[idx])
        {
            continue;
        }
        const auto& values = m_dimValues[idx];
        if (values.size() < 2)
        {
            return std::nullopt;
        }
        auto itSrc = m_dimIndexByValue[idx].find(curAddr[idx]);
        auto itDst = m_dimIndexByValue[idx].find(effectiveDst[idx]);
        if (itSrc == m_dimIndexByValue[idx].end() || itDst == m_dimIndexByValue[idx].end())
        {
            return std::nullopt;
        }
        size_t srcPos = itSrc->second;
        size_t dstPos = itDst->second;
        size_t n = values.size();
        size_t distPos = (dstPos >= srcPos) ? (dstPos - srcPos) : (n - (srcPos - dstPos));
        size_t distNeg = (srcPos >= dstPos) ? (srcPos - dstPos) : (n - (dstPos - srcPos));
        bool positive = distPos <= distNeg;
        uint8_t direction = positive ? 1 : 0;
        auto nb = SelectNeighbor(m_nodeId, idx, direction, ctx.GetFlowHash());
        if (!nb)
        {
            return std::nullopt;
        }
        return NextHop{nb->ifIndex, nb->nextHop};
    }

    return std::nullopt;
}

Ptr<Ipv4Route>
TorusDetourRouting::RouteOutput(Ptr<Packet> p,
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
    if (m_ipv4->GetNAddresses(nh->ifIndex) == 0)
    {
        sockerr = Socket::ERROR_NOROUTETOHOST;
        return nullptr;
    }
    const Ipv4InterfaceAddress ifAddr = m_ipv4->GetAddress(nh->ifIndex, 0);
    rt->SetSource(ifAddr.GetLocal());
    rt->SetGateway(nh->nextHop);
    rt->SetOutputDevice(m_ipv4->GetNetDevice(nh->ifIndex));
    sockerr = Socket::ERROR_NOTERROR;
    return rt;
}

bool
TorusDetourRouting::RouteInput(Ptr<const Packet> p,
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
    if (m_ipv4->GetNAddresses(nh->ifIndex) == 0)
    {
        return false;
    }
    const Ipv4InterfaceAddress ifAddr = m_ipv4->GetAddress(nh->ifIndex, 0);
    rt->SetSource(ifAddr.GetLocal());
    rt->SetGateway(nh->nextHop);
    rt->SetOutputDevice(m_ipv4->GetNetDevice(nh->ifIndex));
    ucb(rt, p, header);
    return true;
}

void
TorusDetourRouting::BuildCaches()
{
    m_cacheBuilt = false;
    m_nodeLevel.clear();
    m_nodeLocalIdx.clear();
    m_nodeAddr.clear();
    m_neighbors.clear();
    m_dirNeighbors.clear();
    m_ipToNode.clear();
    m_addrKeyToNode.clear();
    m_dimValues.clear();
    m_dimIndexByValue.clear();

    if (!m_topo || !m_node)
    {
        return;
    }

    uint32_t levels = m_topo->GetNumLevels();
    for (uint32_t lv = 0; lv < levels; ++lv)
    {
        const NodeContainer& nodes = m_topo->GetLevel(lv);
        for (uint32_t i = 0; i < nodes.GetN(); ++i)
        {
            Ptr<Node> n = nodes.Get(i);
            uint32_t nodeId = n->GetId();
            m_nodeLevel[nodeId] = lv;
            if (lv == m_torusLevel)
            {
                m_nodeLocalIdx[nodeId] = i;
            }
            StructuredAddress addr = m_topo->GetStructuredAddrByLocal(lv, i);
            m_nodeAddr[nodeId] = addr;
            m_addrKeyToNode[addr.ToString('.', false)] = nodeId;
        }
    }

    if (m_torusLevel < levels)
    {
        const NodeContainer& torusNodes = m_topo->GetLevel(m_torusLevel);
        size_t maxSize = 0;
        for (uint32_t i = 0; i < torusNodes.GetN(); ++i)
        {
            StructuredAddress addr = m_topo->GetStructuredAddrByLocal(m_torusLevel, i);
            maxSize = std::max(maxSize, addr.Size());
        }
        std::vector<std::unordered_set<uint32_t>> sets(maxSize);
        for (uint32_t i = 0; i < torusNodes.GetN(); ++i)
        {
            StructuredAddress addr = m_topo->GetStructuredAddrByLocal(m_torusLevel, i);
            for (size_t idx = 0; idx < addr.Size(); ++idx)
            {
                sets[idx].insert(addr[idx]);
            }
        }
        m_dimValues.resize(maxSize);
        m_dimIndexByValue.resize(maxSize);
        for (size_t idx = 0; idx < maxSize; ++idx)
        {
            auto& vec = m_dimValues[idx];
            vec.assign(sets[idx].begin(), sets[idx].end());
            std::sort(vec.begin(), vec.end());
            auto& map = m_dimIndexByValue[idx];
            for (size_t pos = 0; pos < vec.size(); ++pos)
            {
                map[vec[pos]] = pos;
            }
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
            // Some interfaces may temporarily have no assigned IPv4 address (e.g., if address
            // assignment failed or was skipped). Skip them to avoid out-of-range access.
            if (ipv4->GetNAddresses(ifIdx) == 0)
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
            if (remoteIpv4->GetNAddresses(static_cast<uint32_t>(remoteIf)) == 0)
            {
                continue;
            }
            Ipv4Address nextHop =
                remoteIpv4->GetAddress(static_cast<uint32_t>(remoteIf), 0).GetLocal();
            Ipv4Address localIp = ipv4->GetAddress(ifIdx, 0).GetLocal();
            NeighborInfo info;
            info.nodeId = remoteNode->GetId();
            info.ifIndex = ifIdx;
            info.nextHop = nextHop;
            m_neighbors[nodeId].push_back(info);

            auto itCur = m_nodeAddr.find(nodeId);
            auto itNb = m_nodeAddr.find(info.nodeId);
            if (itCur == m_nodeAddr.end() || itNb == m_nodeAddr.end())
            {
                continue;
            }
            const StructuredAddress& curAddr = itCur->second;
            const StructuredAddress& nbAddr = itNb->second;
            size_t dims = std::min({curAddr.Size(), nbAddr.Size(), m_dimValues.size()});
            if (dims == 0)
            {
                continue;
            }
            size_t diffIdx = dims;
            for (size_t d = 0; d < dims; ++d)
            {
                if (curAddr[d] != nbAddr[d])
                {
                    if (diffIdx != dims)
                    {
                        diffIdx = dims;
                        break;
                    }
                    diffIdx = d;
                }
            }
            if (diffIdx == dims)
            {
                continue;
            }
            int levelStart = m_topo->GetLevelAddrBit(m_torusLevel);
            int dimId = static_cast<int>(diffIdx) - levelStart;
            if (dimId <= 0)
            {
                continue;
            }
            auto itLocal = m_nodeLocalIdx.find(nodeId);
            auto itNbLocal = m_nodeLocalIdx.find(info.nodeId);
            if (itLocal == m_nodeLocalIdx.end() || itNbLocal == m_nodeLocalIdx.end())
            {
                continue;
            }
            const auto& adj = m_topo->GetAdjacency(m_torusLevel,
                                                   static_cast<uint32_t>(dimId),
                                                   itLocal->second);
            bool isPositiveNeighbor = false;
            for (const auto& entry : adj)
            {
                if (entry.first == m_torusLevel && entry.second == itNbLocal->second)
                {
                    isPositiveNeighbor = true;
                    break;
                }
            }
            bool localIsA = localIp.Get() < nextHop.Get();
            if (!isPositiveNeighbor && localIsA)
            {
                continue;
            }
            uint8_t dir = localIsA ? 1 : 0;
            auto& buckets = m_dirNeighbors[nodeId];
            if (buckets.size() < m_dimValues.size())
            {
                buckets.resize(m_dimValues.size());
            }
            buckets[diffIdx][dir].push_back(info);
        }
    }

    m_cacheBuilt = true;
}

} // namespace ns3
