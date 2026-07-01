/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#include "inter-level-template.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("InterLevelTemplate");

NS_OBJECT_ENSURE_REGISTERED(InterLevelTemplate);

TypeId
InterLevelTemplate::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::InterLevelTemplate").SetParent<LevelTemplate>().SetGroupName("Datacenter");

    return tid;
}

InterLevelTemplate::InterLevelTemplate()
{
    NS_LOG_FUNCTION(this);
}

InterLevelTemplate::InterLevelTemplate(uint32_t levelId,
                                       uint32_t dimId,
                                       uint32_t nodeNum,
                                       uint32_t subBlockNum,
                                       const LinkProfile& link)
    : LevelTemplate(levelId, dimId, nodeNum, subBlockNum, link)
{
    NS_LOG_FUNCTION(this << nodeNum << subBlockNum);
}

InterLevelTemplate::~InterLevelTemplate()
{
    NS_LOG_FUNCTION(this);
}

std::pair<uint32_t, uint32_t>
InterLevelTemplate::Build(StructuredTopology& topo)
{
    // Replicate current topology m_subBlockNum times (non-direct template)
    std::vector<uint32_t> origSizes = topo.ReplicateTopologyInPlace(m_subBlockNum);
    if (m_topoHelper)
    {
        m_topoHelper->SetLinkAttributes(m_link.rate, m_link.delay, /*mtu=*/1500);
    }
    // Create an new layer that inter-connects the n copies
    auto [newLevelId, newDimId] = topo.CreateLevel(m_nodeNum);
    // Connect aggregation layer with all sub-topologies
    Connect(topo, newLevelId, newDimId);
    GenerateRoutingRules(newLevelId, newDimId);

    return std::make_pair(newLevelId, newDimId);
}

void
InterLevelTemplate::GenerateRoutingRules(uint32_t levelId, uint32_t dimId)
{
    NS_LOG_FUNCTION(this << levelId);
}

} // namespace ns3
