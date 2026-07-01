/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "dragonfly-ugal-routing-helper.h"

namespace ns3
{

DragonflyUgalRoutingHelper::DragonflyUgalRoutingHelper()
    : m_topo(nullptr)
{
}

DragonflyUgalRoutingHelper::DragonflyUgalRoutingHelper(Ptr<StructuredTopology> topo)
    : m_topo(topo)
{
}

DragonflyUgalRoutingHelper::DragonflyUgalRoutingHelper(
    const DragonflyUgalRoutingHelper& o)
{
    m_topo = o.m_topo;
    m_routerLevel = o.m_routerLevel;
    m_groupDimId = o.m_groupDimId;
    m_seed = o.m_seed;
    m_alpha = o.m_alpha;
    m_detourPenalty = o.m_detourPenalty;
    m_queueMetric = o.m_queueMetric;
}

DragonflyUgalRoutingHelper::~DragonflyUgalRoutingHelper()
{
    m_topo = nullptr;
}

DragonflyUgalRoutingHelper*
DragonflyUgalRoutingHelper::Copy() const
{
    return new DragonflyUgalRoutingHelper(*this);
}

Ptr<Ipv4RoutingProtocol>
DragonflyUgalRoutingHelper::Create(Ptr<Node> node) const
{
    Ptr<DragonflyUgalRouting> routing = CreateObject<DragonflyUgalRouting>(node, m_topo);
    routing->SetRouterLevel(m_routerLevel);
    routing->SetGroupDimId(m_groupDimId);
    routing->SetSeed(m_seed);
    routing->SetAlpha(m_alpha);
    routing->SetDetourPenalty(m_detourPenalty);
    routing->SetQueueMetric(m_queueMetric);
    return routing;
}

void
DragonflyUgalRoutingHelper::SetTopology(Ptr<StructuredTopology> topo)
{
    m_topo = topo;
}

void
DragonflyUgalRoutingHelper::SetRouterLevel(uint32_t level)
{
    m_routerLevel = level;
}

void
DragonflyUgalRoutingHelper::SetGroupDimId(uint32_t dimId)
{
    m_groupDimId = dimId;
}

void
DragonflyUgalRoutingHelper::SetSeed(uint64_t seed)
{
    m_seed = seed;
}

void
DragonflyUgalRoutingHelper::SetAlpha(double alpha)
{
    m_alpha = alpha;
}

void
DragonflyUgalRoutingHelper::SetDetourPenalty(double penalty)
{
    m_detourPenalty = penalty;
}

void
DragonflyUgalRoutingHelper::SetQueueMetric(QueueMetric metric)
{
    m_queueMetric = metric;
}

} // namespace ns3
