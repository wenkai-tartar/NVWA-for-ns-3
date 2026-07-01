/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef DRAGONFLY_UGAL_ROUTING_H
#define DRAGONFLY_UGAL_ROUTING_H

#include "congestion-signal-provider.h"
#include "routing-common.h"
#include "structured-topology.h"

#include "ns3/ipv4-routing-protocol.h"
#include "ns3/ptr.h"

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ns3
{

class DragonflyUgalRouting : public Ipv4RoutingProtocol
{
  public:
    static TypeId GetTypeId();

    DragonflyUgalRouting();
    DragonflyUgalRouting(Ptr<Node> node, Ptr<StructuredTopology> topo);
    ~DragonflyUgalRouting() override;

    void SetTopology(Ptr<StructuredTopology> topo);
    void SetRouterLevel(uint32_t level);
    void SetGroupDimId(uint32_t dimId);
    void SetSeed(uint64_t seed);
    void SetAlpha(double alpha);
    double GetAlpha() const;
    void SetDetourPenalty(double penalty);
    double GetDetourPenalty() const;
    void SetQueueMetric(QueueMetric metric);
    QueueMetric GetQueueMetric() const;
    void DumpCaches(std::ostream& os);
    void SetTraceRouting(bool enable);
    void SetTraceStream(std::ostream* os);
    void SetTraceVerbose(bool enable);

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
    uint32_t GetGroupFromAddr(const StructuredAddress& addr) const;
    uint32_t GetNodeIdForIp(Ipv4Address ip) const;

    bool IsHost(uint32_t nodeId) const;
    bool IsRouter(uint32_t nodeId) const;

    std::optional<uint32_t> FindNextHopToRouter(uint32_t srcRouterId,
                                                uint32_t dstRouterId) const;
    std::optional<uint32_t> FindNextHopToGroup(uint32_t srcRouterId,
                                               uint32_t targetGroup) const;

    std::optional<NeighborInfo> GetNeighbor(uint32_t nodeId, uint32_t nextNodeId) const;
    std::optional<uint32_t> GetGlobalNeighbor(uint32_t nodeId, uint32_t targetGroup) const;

    uint32_t PickValiantGroup(uint32_t srcGroup, uint32_t dstGroup, uint64_t hash) const;

    std::optional<NextHop> LookupNextHop(const Ipv4Header& header,
                                         Ptr<const Packet> p,
                                         bool isInject) const;

    Ptr<Ipv4> m_ipv4;
    Ptr<Node> m_node;
    uint32_t m_nodeId{0};
    Ptr<StructuredTopology> m_topo;

    uint32_t m_routerLevel{1};
    uint32_t m_groupDimId{1};
    uint64_t m_seed{1};
    double m_alpha{1.0};
    double m_detourPenalty{1.0};
    QueueMetric m_queueMetric{QueueMetric::kBytes};
    std::shared_ptr<CongestionSignalProvider> m_congestionProvider;

    bool m_cacheBuilt{false};

    std::unordered_map<uint32_t, uint32_t> m_nodeLevel;
    std::unordered_map<uint32_t, StructuredAddress> m_nodeAddr;
    std::unordered_map<uint32_t, uint32_t> m_groupByNode;
    std::unordered_set<uint32_t> m_routerNodes;
    std::unordered_set<uint32_t> m_hostNodes;

    std::unordered_map<uint32_t, std::vector<NeighborInfo>> m_neighbors;
    std::unordered_map<uint32_t, uint32_t> m_hostToRouter;
    std::unordered_map<uint32_t, uint32_t> m_ipToNode;

    std::unordered_map<uint32_t, std::vector<uint32_t>> m_localNeighbors;
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint32_t>> m_globalNeighborByGroup;
    std::vector<uint32_t> m_groupList;

    std::unordered_map<uint32_t, uint32_t> m_routerIndexByNode;
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint32_t>> m_routerByGroupIndex;
    std::unordered_map<uint32_t, uint32_t> m_routerLocalIdxByNode;
    std::vector<uint32_t> m_routerNodeByLocalIdx;
    uint32_t m_groups{0};
    uint32_t m_routersPerGroup{0};
    uint32_t m_globalLinksPerRouter{0};

    bool m_traceRouting{false};
    bool m_traceVerbose{false};
    std::ostream* m_traceStream{nullptr};
};

} // namespace ns3

#endif // DRAGONFLY_UGAL_ROUTING_H
