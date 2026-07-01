/*
 * Author: Wenkai Li (v-wenkaili@microsoft.com)
 */

#include "node-bfs-routing-helper.h"

#include "ns3/ipv4.h"
#include "ns3/log.h"
#include "ns3/net-device.h"
#include "ns3/node-container.h"
#include "ns3/point-to-point-channel.h"

#include <map>
#include <stdint.h>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("NodeBfsRoutingHelper");

std::map<Ptr<Node>, std::map<Ptr<Node>, std::vector<std::pair<Ptr<Node>, uint32_t>>>>
    NodeBfsRoutingHelper::m_nextHop;
std::map<uint32_t, bool> NodeBfsRoutingHelper::m_isHost;
bool NodeBfsRoutingHelper::m_withHost = false;
bool NodeBfsRoutingHelper::m_strictBaseline = false;
bool NodeBfsRoutingHelper::m_suppressRecalc = false;
std::map<uint32_t, std::map<uint32_t, uint32_t>> NodeBfsRoutingHelper::m_baseDist;

NodeBfsRoutingHelper::NodeBfsRoutingHelper()
    : m_topo(nullptr)
{
}

NodeBfsRoutingHelper::NodeBfsRoutingHelper(Ptr<StructuredTopology> topo)
    : m_topo(topo)
{
}

NodeBfsRoutingHelper::NodeBfsRoutingHelper(const NodeBfsRoutingHelper& o)
{
    m_topo = o.m_topo;
}

NodeBfsRoutingHelper::~NodeBfsRoutingHelper()
{
    m_topo = nullptr;
}

NodeBfsRoutingHelper*
NodeBfsRoutingHelper::Copy() const
{
    return new NodeBfsRoutingHelper(*this);
}

Ptr<Ipv4RoutingProtocol>
NodeBfsRoutingHelper::Create(Ptr<Node> node) const
{
    return CreateObject<NodeBfsRouting>(node, m_topo);
}


void
NodeBfsRoutingHelper::CalculateRoutes(Ptr<StructuredTopology> topo)
{

    m_withHost = false;
    m_nextHop.clear();
    m_isHost.clear();

    const NodeContainer& hosts = topo->GetLevel(0);
    for (auto i = hosts.Begin(); i != hosts.End(); i++)
    {
        m_isHost[(*i)->GetId()] = true;
    }

    for (auto i = hosts.Begin(); i != hosts.End(); i++)
    {
        CalculateRoute(topo, *i);
    }
}

void
NodeBfsRoutingHelper::CalculateRoute(Ptr<StructuredTopology> topo, Ptr<Node> host)
{
    const uint32_t hostId = host->GetId();
    auto baseIt = m_baseDist.find(hostId);
    const bool useBaseline = m_strictBaseline && (baseIt != m_baseDist.end());
    const auto* baseDist = useBaseline ? &baseIt->second : nullptr;

    // Queue for the BFS
    std::vector<Ptr<Node>> q;
    // Distance from the host to each node
    std::map<Ptr<Node>, uint32_t> dis;
    // Init BFS
    q.push_back(host);
    dis[host] = 0;

    // BFS
    for (uint32_t i = 0; i < (uint32_t)q.size(); i++)
    {
        Ptr<Node> now = q[i];
        uint32_t d = dis[now];

        for (uint32_t j = 1; j < now->GetNDevices(); j++)
        {
            Ptr<NetDevice> dev = now->GetDevice(j);
            // Skip down link
            if (!dev->IsLinkUp())
            {
                continue;
            }
            Ptr<Ipv4> nowIpv4 = now->GetObject<Ipv4>();
            if (nowIpv4)
            {
                int32_t ifIndex = nowIpv4->GetInterfaceForDevice(dev);
                if (ifIndex != -1 && !nowIpv4->IsUp(ifIndex))
                {
                    continue;
                }
            }

            Ptr<PointToPointChannel> channel = DynamicCast<PointToPointChannel>(dev->GetChannel());
            Ptr<Node> next = nullptr;
            uint32_t remoteInterface = 0;

            for (uint32_t k = 0; k < channel->GetNDevices(); k++)
            {
                Ptr<NetDevice> remoteDev = channel->GetDevice(k);
                remoteInterface = remoteDev->GetIfIndex();
                if (remoteDev != dev)
                {
                    next = remoteDev->GetNode();
                    break;
                }
            }
            if (next)
            {
                Ptr<Ipv4> nextIpv4 = next->GetObject<Ipv4>();
                if (nextIpv4 && remoteInterface < nextIpv4->GetNInterfaces() &&
                    !nextIpv4->IsUp(remoteInterface))
                {
                    continue;
                }
            }

            if (dis.find(next) == dis.end())
            {
                if (useBaseline)
                {
                    auto itBase = baseDist->find(next->GetId());
                    if (itBase == baseDist->end() || d + 1 > itBase->second)
                    {
                        continue;
                    }
                }
                dis[next] = d + 1;
                if (m_isHost.find(next->GetId()) == m_isHost.end())
                {
                    q.push_back(next);
                }
            }
            // if 'now' is on the shortest path from 'next' to 'host'.
            if (d + 1 == dis[next])
            {
                if (useBaseline)
                {
                    auto itBase = baseDist->find(next->GetId());
                    if (itBase == baseDist->end() || d + 1 != itBase->second)
                    {
                        continue;
                    }
                }
                m_nextHop[next][host].emplace_back(now, remoteInterface);
            }
        }
    }
}

void
NodeBfsRoutingHelper::CalculateRoutesWithHost(Ptr<StructuredTopology> topo)
{

    m_withHost = true;
    m_nextHop.clear();
    m_isHost.clear();

    const NodeContainer& hosts = topo->GetLevel(0);
    for (auto i = hosts.Begin(); i != hosts.End(); i++)
    {
        m_isHost[(*i)->GetId()] = true;
    }

    for (auto i = hosts.Begin(); i != hosts.End(); i++)
    {
        CalculateRouteWithHost(topo, *i);
    }
}

void
NodeBfsRoutingHelper::CalculateRouteWithHost(Ptr<StructuredTopology> topo, Ptr<Node> host)
{
    const uint32_t hostId = host->GetId();
    auto baseIt = m_baseDist.find(hostId);
    const bool useBaseline = m_strictBaseline && (baseIt != m_baseDist.end());
    const auto* baseDist = useBaseline ? &baseIt->second : nullptr;

    // Queue for the BFS
    std::vector<Ptr<Node>> q;
    // Distance from the host to each node
    std::map<Ptr<Node>, uint32_t> dis;
    // Init BFS
    q.push_back(host);
    dis[host] = 0;

    // BFS
    for (uint32_t i = 0; i < (uint32_t)q.size(); i++)
    {
        Ptr<Node> now = q[i];
        uint32_t d = dis[now];

        for (uint32_t j = 1; j < now->GetNDevices(); j++)
        {
            Ptr<NetDevice> dev = now->GetDevice(j);
            // Skip down link
            if (!dev->IsLinkUp())
            {
                continue;
            }
            Ptr<Ipv4> nowIpv4 = now->GetObject<Ipv4>();
            if (nowIpv4)
            {
                int32_t ifIndex = nowIpv4->GetInterfaceForDevice(dev);
                if (ifIndex != -1 && !nowIpv4->IsUp(ifIndex))
                {
                    continue;
                }
            }

            Ptr<PointToPointChannel> channel = DynamicCast<PointToPointChannel>(dev->GetChannel());
            Ptr<Node> next = nullptr;
            uint32_t remoteInterface = 0;

            for (uint32_t k = 0; k < channel->GetNDevices(); k++)
            {
                Ptr<NetDevice> remoteDev = channel->GetDevice(k);
                remoteInterface = remoteDev->GetIfIndex();
                if (remoteDev != dev)
                {
                    next = remoteDev->GetNode();
                    break;
                }
            }
            if (next)
            {
                Ptr<Ipv4> nextIpv4 = next->GetObject<Ipv4>();
                if (nextIpv4 && remoteInterface < nextIpv4->GetNInterfaces() &&
                    !nextIpv4->IsUp(remoteInterface))
                {
                    continue;
                }
            }

            if (dis.find(next) == dis.end())
            {
                if (useBaseline)
                {
                    auto itBase = baseDist->find(next->GetId());
                    if (itBase == baseDist->end() || d + 1 > itBase->second)
                    {
                        continue;
                    }
                }
                dis[next] = d + 1;
                q.push_back(next);
            }
            // if 'now' is on the shortest path from 'next' to 'host'.
            if (d + 1 == dis[next])
            {
                if (useBaseline)
                {
                    auto itBase = baseDist->find(next->GetId());
                    if (itBase == baseDist->end() || d + 1 != itBase->second)
                    {
                        continue;
                    }
                }
                m_nextHop[next][host].emplace_back(now, remoteInterface);
            }
        }
    }
}

void
NodeBfsRoutingHelper::EnableStrictBaseline(Ptr<StructuredTopology> topo, bool enable, bool withHost)
{
    m_strictBaseline = enable;
    m_baseDist.clear();
    if (enable && topo)
    {
        CalculateBaselineDistances(topo, withHost);
    }
}

void
NodeBfsRoutingHelper::CalculateBaselineDistances(Ptr<StructuredTopology> topo, bool withHost)
{
    if (!topo)
    {
        return;
    }

    m_baseDist.clear();
    std::unordered_set<uint32_t> hostIds;
    const NodeContainer& hosts = topo->GetLevel(0);
    for (auto i = hosts.Begin(); i != hosts.End(); i++)
    {
        hostIds.insert((*i)->GetId());
    }

    auto computeDistances = [&](Ptr<Node> host) {
        std::vector<Ptr<Node>> q;
        std::map<Ptr<Node>, uint32_t> dis;
        q.push_back(host);
        dis[host] = 0;

        for (uint32_t i = 0; i < (uint32_t)q.size(); i++)
        {
            Ptr<Node> now = q[i];
            uint32_t d = dis[now];

            for (uint32_t j = 1; j < now->GetNDevices(); j++)
            {
                Ptr<NetDevice> dev = now->GetDevice(j);
                if (!dev->IsLinkUp())
                {
                    continue;
                }
                Ptr<Ipv4> nowIpv4 = now->GetObject<Ipv4>();
                if (nowIpv4)
                {
                    int32_t ifIndex = nowIpv4->GetInterfaceForDevice(dev);
                    if (ifIndex != -1 && !nowIpv4->IsUp(ifIndex))
                    {
                        continue;
                    }
                }

                Ptr<PointToPointChannel> channel = DynamicCast<PointToPointChannel>(dev->GetChannel());
                Ptr<Node> next = nullptr;
                uint32_t remoteInterface = 0;

                for (uint32_t k = 0; k < channel->GetNDevices(); k++)
                {
                    Ptr<NetDevice> remoteDev = channel->GetDevice(k);
                    remoteInterface = remoteDev->GetIfIndex();
                    if (remoteDev != dev)
                    {
                        next = remoteDev->GetNode();
                        break;
                    }
                }
                if (next)
                {
                    Ptr<Ipv4> nextIpv4 = next->GetObject<Ipv4>();
                    if (nextIpv4 && remoteInterface < nextIpv4->GetNInterfaces() &&
                        !nextIpv4->IsUp(remoteInterface))
                    {
                        continue;
                    }
                }

                if (dis.find(next) == dis.end())
                {
                    dis[next] = d + 1;
                    if (withHost || hostIds.find(next->GetId()) == hostIds.end())
                    {
                        q.push_back(next);
                    }
                }
            }
        }

        std::map<uint32_t, uint32_t> distMap;
        for (const auto& kv : dis)
        {
            distMap[kv.first->GetId()] = kv.second;
        }
        m_baseDist[host->GetId()] = std::move(distMap);
    };

    for (auto i = hosts.Begin(); i != hosts.End(); i++)
    {
        computeDistances(*i);
    }
}

void
NodeBfsRoutingHelper::SetRoutingEntries(Ptr<StructuredTopology> topo)
{
    for (uint32_t levelId = 0; levelId < topo->GetNumLevels(); ++levelId)
    {
        for (uint32_t localIdx = 0; localIdx < topo->GetLevel(levelId).GetN(); ++localIdx)
        {
            Ptr<Node> node = topo->GetNode(levelId, localIdx);
            Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
            Ptr<Ipv4RoutingProtocol> protocol = ipv4->GetRoutingProtocol();
            Ptr<NodeBfsRouting> bfsRouting = DynamicCast<NodeBfsRouting>(protocol);
            if (bfsRouting)
            {
                // Ensure topology is set once for failure-triggered recomputation.
                bfsRouting->SetTopology(topo);
                bfsRouting->ClearRoutingTable();
            }
        }
    }

    for (auto i = m_nextHop.begin(); i != m_nextHop.end(); i++)
    {
        Ptr<Node> node = i->first;
        auto& table = i->second;
        for (auto j = table.begin(); j != table.end(); j++)
        {
            // The destination node
            Ptr<Node> dst = j->first;
            Ipv4Address dstAddr = dst->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal();
            std::vector<std::pair<Ptr<Node>, uint32_t>> nexts = j->second;
            for (uint32_t k = 0; k < (uint32_t)nexts.size(); k++)
            {
                Ptr<Node> next = nexts[k].first;
                uint32_t interface = nexts[k].second;
                Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
                Ptr<Ipv4RoutingProtocol> protocol = ipv4->GetRoutingProtocol();
                DynamicCast<NodeBfsRouting>(protocol)->AddTableEntry(dstAddr, interface);
            }
        }
    }
}

void
NodeBfsRoutingHelper::RecalculateRoutes(Ptr<StructuredTopology> topo)
{
    if (!topo)
    {
        return;
    }
    if (m_suppressRecalc)
    {
        return;
    }
    if (m_withHost)
    {
        CalculateRoutesWithHost(topo);
    }
    else
    {
        CalculateRoutes(topo);
    }
    SetRoutingEntries(topo);
}

void
NodeBfsRoutingHelper::SuppressRecalculate(bool enable)
{
    m_suppressRecalc = enable;
}

bool
NodeBfsRoutingHelper::IsRecalculateSuppressed()
{
    return m_suppressRecalc;
}

void
NodeBfsRoutingHelper::SetEcmpPolicy(Ptr<StructuredTopology> topo, PortSelectPolicy policy)
{
    // Set ECMP policy for all NodeBfsRouting instances
    for (uint32_t levelId = 0; levelId < topo->GetNumLevels(); ++levelId)
    {
        for (uint32_t localIdx = 0; localIdx < topo->GetLevel(levelId).GetN(); ++localIdx)
        {
            Ptr<Node> node = topo->GetNode(levelId, localIdx);
            Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
            Ptr<Ipv4RoutingProtocol> protocol = ipv4->GetRoutingProtocol();
            Ptr<NodeBfsRouting> bfsRouting = DynamicCast<NodeBfsRouting>(protocol);
            if (bfsRouting)
            {
                bfsRouting->SetPortSelectPolicy(policy);
            }
        }
    }
}

void
NodeBfsRoutingHelper::EnableEcmp(Ptr<StructuredTopology> topo, bool enable)
{
    SetEcmpPolicy(topo, enable ? PortSelectPolicy::kByHash : PortSelectPolicy::kFirst);
}

void
NodeBfsRoutingHelper::PrintRoutingEntries(Ptr<StructuredTopology> topo)
{
    std::cout << "Printing routing entries..." << std::endl;
    for (uint32_t levelId = 0; levelId < topo->GetNumLevels(); ++levelId)
    {
        for (uint32_t localIdx = 0; localIdx < topo->GetLevel(levelId).GetN(); ++localIdx)
        {
            Ptr<Node> node = topo->GetNode(levelId, localIdx);
            Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
            Ptr<Ipv4RoutingProtocol> protocol = ipv4->GetRoutingProtocol();
            Ptr<NodeBfsRouting> bfsRouting = DynamicCast<NodeBfsRouting>(protocol);
            if (bfsRouting)
            {
                bfsRouting->PrintRoutingTable();
            }
        }
    }
}

uint64_t
NodeBfsRoutingHelper::GetRoutingEntryNumber(Ptr<StructuredTopology> topo)
{
    uint64_t totalEntryNumber = 0;
    for (auto i = m_nextHop.begin(); i != m_nextHop.end(); i++)
    {
        Ptr<Node> src = i->first;
        auto& table = i->second;
        for (auto j = table.begin(); j != table.end(); j++)
        {
            // The destination node
            Ptr<Node> dst = j->first;
            std::vector<std::pair<Ptr<Node>, uint32_t>> nexts = j->second;
            totalEntryNumber += (uint64_t)nexts.size();
        }
    }
    return totalEntryNumber;
}

} /* namespace ns3 */
