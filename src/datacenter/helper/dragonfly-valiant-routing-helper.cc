/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "dragonfly-valiant-routing-helper.h"

namespace ns3
{

DragonflyValiantRoutingHelper::DragonflyValiantRoutingHelper()
    : m_topo(nullptr)
{
}

DragonflyValiantRoutingHelper::DragonflyValiantRoutingHelper(Ptr<StructuredTopology> topo)
    : m_topo(topo)
{
}

DragonflyValiantRoutingHelper::DragonflyValiantRoutingHelper(
    const DragonflyValiantRoutingHelper& o)
{
    m_topo = o.m_topo;
    m_routerLevel = o.m_routerLevel;
    m_groupDimId = o.m_groupDimId;
    m_seed = o.m_seed;
}

DragonflyValiantRoutingHelper::~DragonflyValiantRoutingHelper()
{
    m_topo = nullptr;
}

DragonflyValiantRoutingHelper*
DragonflyValiantRoutingHelper::Copy() const
{
    return new DragonflyValiantRoutingHelper(*this);
}

Ptr<Ipv4RoutingProtocol>
DragonflyValiantRoutingHelper::Create(Ptr<Node> node) const
{
    Ptr<DragonflyValiantRouting> routing = CreateObject<DragonflyValiantRouting>(node, m_topo);
    routing->SetRouterLevel(m_routerLevel);
    routing->SetGroupDimId(m_groupDimId);
    routing->SetSeed(m_seed);
    return routing;
}

void
DragonflyValiantRoutingHelper::SetTopology(Ptr<StructuredTopology> topo)
{
    m_topo = topo;
}

void
DragonflyValiantRoutingHelper::SetRouterLevel(uint32_t level)
{
    m_routerLevel = level;
}

void
DragonflyValiantRoutingHelper::SetGroupDimId(uint32_t dimId)
{
    m_groupDimId = dimId;
}

void
DragonflyValiantRoutingHelper::SetSeed(uint64_t seed)
{
    m_seed = seed;
}

} // namespace ns3
