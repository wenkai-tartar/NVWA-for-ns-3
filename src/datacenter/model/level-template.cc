/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#include "level-template.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("LevelTemplate");

NS_OBJECT_ENSURE_REGISTERED(LevelTemplate);

TypeId
LevelTemplate::GetTypeId()
{
    static TypeId tid = TypeId("ns3::LevelTemplate").SetParent<Object>().SetGroupName("Datacenter");

    return tid;
}

LevelTemplate::LevelTemplate()
{
    NS_LOG_FUNCTION(this);
}

LevelTemplate::LevelTemplate(uint32_t levelId,
                             uint32_t dimId,
                             uint32_t nodeNum,
                             uint32_t subBlockNum,
                             const LinkProfile& link)
    : m_levelId(levelId),
      m_dimId(dimId),
      m_nodeNum(nodeNum),
      m_subBlockNum(subBlockNum),
      m_link(link)
{
    NS_LOG_FUNCTION(this << nodeNum << subBlockNum);
}

void
LevelTemplate::SetTopologyHelper(std::shared_ptr<TopologyHelper> topoHelper)
{
    NS_LOG_FUNCTION(this);
    m_topoHelper = topoHelper;

    // Configure link attributes for this level
    if (m_topoHelper)
    {
        m_topoHelper->SetLinkAttributes(m_link.rate, m_link.delay, /*mtu=*/1500);
    }
}

void
LevelTemplate::SetPortSelectPolicy(PortSelectPolicy policy)
{
    NS_LOG_FUNCTION(this);
    m_portSelectPolicy = policy;
}

LevelTemplate::~LevelTemplate()
{
    NS_LOG_FUNCTION(this);
}

} // namespace ns3
