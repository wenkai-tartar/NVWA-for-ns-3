/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef DRAGONFLY_UGAL_ROUTING_HELPER_H
#define DRAGONFLY_UGAL_ROUTING_HELPER_H

#include "ns3/ipv4-routing-helper.h"
#include "ns3/ptr.h"

#include "ns3/dragonfly-ugal-routing.h"
#include "ns3/structured-topology.h"

namespace ns3
{

class DragonflyUgalRoutingHelper : public Ipv4RoutingHelper
{
  public:
    DragonflyUgalRoutingHelper();
    explicit DragonflyUgalRoutingHelper(Ptr<StructuredTopology> topo);
    DragonflyUgalRoutingHelper(const DragonflyUgalRoutingHelper& o);
    ~DragonflyUgalRoutingHelper() override;

    DragonflyUgalRoutingHelper& operator=(const DragonflyUgalRoutingHelper&) = delete;

    DragonflyUgalRoutingHelper* Copy() const override;
    Ptr<Ipv4RoutingProtocol> Create(Ptr<Node> node) const override;

    void SetTopology(Ptr<StructuredTopology> topo);
    void SetRouterLevel(uint32_t level);
    void SetGroupDimId(uint32_t dimId);
    void SetSeed(uint64_t seed);
    void SetAlpha(double alpha);
    void SetDetourPenalty(double penalty);
    void SetQueueMetric(QueueMetric metric);

  private:
    Ptr<StructuredTopology> m_topo;
    uint32_t m_routerLevel{1};
    uint32_t m_groupDimId{1};
    uint64_t m_seed{1};
    double m_alpha{1.0};
    double m_detourPenalty{1.0};
    QueueMetric m_queueMetric{QueueMetric::kBytes};
};

} // namespace ns3

#endif // DRAGONFLY_UGAL_ROUTING_HELPER_H
