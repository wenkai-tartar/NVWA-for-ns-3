/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#include "topology-builder.h"

#include "ns3/log.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("TopologyBuilder");

TopologyBuilder::TopologyBuilder()
{
    NS_LOG_FUNCTION(this);
}

TopologyBuilder::~TopologyBuilder()
{
    NS_LOG_FUNCTION(this);
}

TopologyBuilder&
TopologyBuilder::AddLevel(Ptr<LevelTemplate> level)
{
    m_levels.push_back(level);
    return *this;
}

Ptr<StructuredTopology>
TopologyBuilder::Build(Ptr<StructuredTopology> topo)
{
    NS_LOG_FUNCTION(this);

    for (const auto& level : m_levels)
    {
        level->Build(*topo);
    }
    return topo;
}

} // namespace ns3