/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#include "rule-based-routing-helper.h"

#include "ns3/log.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("RuleBasedRoutingHelper");

RuleBasedRoutingHelper::RuleBasedRoutingHelper(const RuleBasedRoutingHelper& o)
    : Ipv4RoutingHelper(o)
{
    NS_LOG_FUNCTION(this);
}

RuleBasedRoutingHelper::RuleBasedRoutingHelper()
{
    NS_LOG_FUNCTION(this);
}

RuleBasedRoutingHelper*
RuleBasedRoutingHelper::Copy() const
{
    return new RuleBasedRoutingHelper(*this);
}

Ptr<Ipv4RoutingProtocol>
RuleBasedRoutingHelper::Create(Ptr<Node> node) const
{
    return CreateObject<RuleBasedRouting>();
}

void
RuleBasedRoutingHelper::Initialize(StructuredTopology& topo)
{
    for (uint32_t levelId = 0; levelId < topo.GetNumLevels(); levelId++)
    {
        for (uint32_t localIdx = 0; localIdx < topo.GetLevel(levelId).GetN(); localIdx++)
        {
            Ptr<Node> node = topo.GetNode(levelId, localIdx);
            Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
            Ptr<RuleBasedRouting> routing =
                DynamicCast<RuleBasedRouting>(ipv4->GetRoutingProtocol());
            routing->SetLevelId(levelId);
            routing->SetLocalIdx(localIdx);
            routing->SetSrc(topo.GetStructuredAddrByLocal(levelId, localIdx));
            routing->SetTopo(&topo);
        }
    }
}

} // namespace ns3
