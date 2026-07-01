/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef DRAGONFLY_VALIANT_ROUTING_HELPER_H
#define DRAGONFLY_VALIANT_ROUTING_HELPER_H

#include "ns3/ipv4-routing-helper.h"
#include "ns3/ptr.h"

#include "ns3/dragonfly-valiant-routing.h"
#include "ns3/structured-topology.h"

namespace ns3
{

class DragonflyValiantRoutingHelper : public Ipv4RoutingHelper
{
  public:
    DragonflyValiantRoutingHelper();
    explicit DragonflyValiantRoutingHelper(Ptr<StructuredTopology> topo);
    DragonflyValiantRoutingHelper(const DragonflyValiantRoutingHelper& o);
    ~DragonflyValiantRoutingHelper() override;

    DragonflyValiantRoutingHelper& operator=(const DragonflyValiantRoutingHelper&) = delete;

    DragonflyValiantRoutingHelper* Copy() const override;
    Ptr<Ipv4RoutingProtocol> Create(Ptr<Node> node) const override;

    void SetTopology(Ptr<StructuredTopology> topo);
    void SetRouterLevel(uint32_t level);
    void SetGroupDimId(uint32_t dimId);
    void SetSeed(uint64_t seed);

  private:
    Ptr<StructuredTopology> m_topo;
    uint32_t m_routerLevel{1};
    uint32_t m_groupDimId{1};
    uint64_t m_seed{1};
};

} // namespace ns3

#endif // DRAGONFLY_VALIANT_ROUTING_HELPER_H
