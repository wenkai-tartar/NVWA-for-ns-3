/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "torus-detour-routing-helper.h"

namespace ns3
{

TorusDetourRoutingHelper::TorusDetourRoutingHelper()
    : m_topo(nullptr)
{
}

TorusDetourRoutingHelper::TorusDetourRoutingHelper(const StructuredTopology* topo)
    : m_topo(topo)
{
}

TorusDetourRoutingHelper::TorusDetourRoutingHelper(
    const TorusDetourRoutingHelper& o)
{
    m_topo = o.m_topo;
    m_torusLevel = o.m_torusLevel;
    m_transitFields = o.m_transitFields;
    m_detourStages = o.m_detourStages;
}

TorusDetourRoutingHelper::~TorusDetourRoutingHelper()
{
    m_topo = nullptr;
}

TorusDetourRoutingHelper*
TorusDetourRoutingHelper::Copy() const
{
    return new TorusDetourRoutingHelper(*this);
}

Ptr<Ipv4RoutingProtocol>
TorusDetourRoutingHelper::Create(Ptr<Node> node) const
{
    Ptr<TorusDetourRouting> routing = CreateObject<TorusDetourRouting>(node, m_topo);
    routing->SetTorusLevel(m_torusLevel);
    routing->SetTransitFields(m_transitFields);
    routing->SetDetourStages(m_detourStages);
    return routing;
}

void
TorusDetourRoutingHelper::SetTopology(const StructuredTopology* topo)
{
    m_topo = topo;
}

void
TorusDetourRoutingHelper::SetTorusLevel(uint32_t level)
{
    m_torusLevel = level;
}

void
TorusDetourRoutingHelper::SetTransitFields(const std::vector<uint16_t>& fields)
{
    m_transitFields = fields;
}

void
TorusDetourRoutingHelper::SetDetourStages(uint8_t stages)
{
    m_detourStages = stages == 0 ? 1 : stages;
}

} // namespace ns3
