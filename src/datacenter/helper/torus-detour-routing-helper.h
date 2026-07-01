/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef TORUS_DETOUR_ROUTING_HELPER_H
#define TORUS_DETOUR_ROUTING_HELPER_H

#include "ns3/ipv4-routing-helper.h"
#include "ns3/ptr.h"

#include "ns3/torus-detour-routing.h"
#include "ns3/structured-topology.h"

namespace ns3
{

class TorusDetourRoutingHelper : public Ipv4RoutingHelper
{
  public:
    TorusDetourRoutingHelper();
    explicit TorusDetourRoutingHelper(const StructuredTopology* topo);
    TorusDetourRoutingHelper(const TorusDetourRoutingHelper& o);
    ~TorusDetourRoutingHelper() override;

    TorusDetourRoutingHelper& operator=(const TorusDetourRoutingHelper&) = delete;

    TorusDetourRoutingHelper* Copy() const override;
    Ptr<Ipv4RoutingProtocol> Create(Ptr<Node> node) const override;

    void SetTopology(const StructuredTopology* topo);
    void SetTorusLevel(uint32_t level);
    void SetTransitFields(const std::vector<uint16_t>& fields);
    void SetDetourStages(uint8_t stages);

  private:
    const StructuredTopology* m_topo{nullptr};
    uint32_t m_torusLevel{0};
    std::vector<uint16_t> m_transitFields;
    uint8_t m_detourStages{1};
};

} // namespace ns3

#endif // TORUS_DETOUR_ROUTING_HELPER_H
