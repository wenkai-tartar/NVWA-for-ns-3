/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef TORUS_DETOUR_ROUTING_H
#define TORUS_DETOUR_ROUTING_H

#include "non-minimal-policy.h"
#include "routing-common.h"
#include "structured-topology.h"

#include "ns3/ipv4-routing-protocol.h"
#include "ns3/ptr.h"

#include <array>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ns3
{

class TorusDetourRouting : public Ipv4RoutingProtocol
{
  public:
    static TypeId GetTypeId();

    TorusDetourRouting();
    TorusDetourRouting(Ptr<Node> node, const StructuredTopology* topo);
    ~TorusDetourRouting() override;

    void SetTopology(const StructuredTopology* topo);
    void SetTorusLevel(uint32_t level);
    void SetTransitFields(const std::vector<uint16_t>& fields);
    void SetDetourStages(uint8_t stages);

    Ptr<Ipv4Route> RouteOutput(Ptr<Packet> p,
                              const Ipv4Header& header,
                              Ptr<NetDevice> oif,
                              Socket::SocketErrno& sockerr) override;

    bool RouteInput(Ptr<const Packet> p,
                    const Ipv4Header& header,
                    Ptr<const NetDevice> idev,
                    const UnicastForwardCallback& ucb,
                    const MulticastForwardCallback& mcb,
                    const LocalDeliverCallback& lcb,
                    const ErrorCallback& ecb) override;

    void NotifyInterfaceUp(uint32_t interface) override;
    void NotifyInterfaceDown(uint32_t interface) override;
    void NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address) override;
    void NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress address) override;
    void SetIpv4(Ptr<Ipv4> ipv4) override;
    void PrintRoutingTable(Ptr<OutputStreamWrapper> stream,
                           Time::Unit unit = Time::S) const override;

  protected:
    void DoDispose() override;

  private:
    struct NeighborInfo
    {
        uint32_t nodeId{0};
        uint32_t ifIndex{0};
        Ipv4Address nextHop;
    };

    struct NextHop
    {
        uint32_t ifIndex{0};
        Ipv4Address nextHop;
    };

    void BuildCaches();
    bool EnsureCaches();

    uint64_t ComputeFlowHash(const Ipv4Header& h, Ptr<const Packet> p, uint32_t seed) const;
    uint32_t GetNodeIdForIp(Ipv4Address ip) const;

    std::optional<NeighborInfo> SelectNeighbor(uint32_t nodeId,
                                               size_t dimIdx,
                                               uint8_t direction,
                                               uint64_t flowHash) const;

    std::optional<NextHop> LookupNextHop(const Ipv4Header& header,
                                         Ptr<const Packet> p,
                                         bool isInject) const;

    Ptr<Ipv4> m_ipv4;
    Ptr<Node> m_node;
    uint32_t m_nodeId{0};
    const StructuredTopology* m_topo{nullptr};

    uint32_t m_torusLevel{0};
    std::vector<uint16_t> m_transitFields;
    uint8_t m_detourStages{1};
    Ptr<DetourPolicy> m_detourPolicy;

    bool m_cacheBuilt{false};

    std::unordered_map<uint32_t, uint32_t> m_nodeLevel;
    std::unordered_map<uint32_t, uint32_t> m_nodeLocalIdx;
    std::unordered_map<uint32_t, StructuredAddress> m_nodeAddr;

    std::unordered_map<uint32_t, std::vector<NeighborInfo>> m_neighbors;
    std::unordered_map<uint32_t, std::vector<std::array<std::vector<NeighborInfo>, 2>>>
        m_dirNeighbors;
    std::unordered_map<uint32_t, uint32_t> m_ipToNode;
    std::unordered_map<std::string, uint32_t> m_addrKeyToNode;
    std::vector<std::vector<uint32_t>> m_dimValues;
    std::vector<std::unordered_map<uint32_t, size_t>> m_dimIndexByValue;
};

} // namespace ns3

#endif // TORUS_DETOUR_ROUTING_H
