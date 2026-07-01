/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#include "intra-level-template.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("IntraLevelTemplate");

NS_OBJECT_ENSURE_REGISTERED(IntraLevelTemplate);

TypeId
IntraLevelTemplate::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::IntraLevelTemplate").SetParent<LevelTemplate>().SetGroupName("Datacenter");

    return tid;
}

IntraLevelTemplate::IntraLevelTemplate()
{
    NS_LOG_FUNCTION(this);
}

IntraLevelTemplate::IntraLevelTemplate(uint32_t levelId,
                                       uint32_t dimId,
                                       uint32_t nodeNum,
                                       uint32_t subBlockNum,
                                       const LinkProfile& link)
    : LevelTemplate(levelId, dimId, nodeNum, subBlockNum, link)
{
    NS_LOG_FUNCTION(this << nodeNum << subBlockNum);
}

IntraLevelTemplate::~IntraLevelTemplate()
{
    NS_LOG_FUNCTION(this);
}

std::pair<uint32_t, uint32_t>
IntraLevelTemplate::Build(StructuredTopology& topo)
{
    NS_LOG_FUNCTION(this);
    std::vector<uint32_t> origSizes = topo.ReplicateTopologyInPlace(m_subBlockNum);
    if (m_topoHelper)
    {
        m_topoHelper->SetLinkAttributes(m_link.rate, m_link.delay, /*mtu=*/1500);
    }
    auto [newLevelId, newDimId] = topo.CreateLevel(m_nodeNum);
    Connect(topo, newLevelId, newDimId);
    GenerateRoutingRules(newLevelId, newDimId);
    return std::make_pair(newLevelId, newDimId);
}

void
IntraLevelTemplate::GenerateRoutingRules(uint32_t levelId, uint32_t dimId)
{
    NS_LOG_FUNCTION(this << levelId);
}

} // namespace ns3