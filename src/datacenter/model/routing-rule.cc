/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#include "routing-rule.h"

#include "routing-common.h"

#include "ns3/log.h"

#include <algorithm>
#include <random>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("RoutingRule");

NS_OBJECT_ENSURE_REGISTERED(RoutingRule);

// ---------------- RoutingRule (base) ----------------

TypeId
RoutingRule::GetTypeId()
{
    static TypeId tid = TypeId("ns3::RoutingRule").SetParent<Object>().SetGroupName("Datacenter");
    return tid;
}

RoutingRule::RoutingRule(MatchFunction matchFunc,
                         MatchCondition matchCondition,
                         ActionFunction actionFunc,
                         uint32_t priority,
                         bool requiresFlowHash)
    : m_matchFunc(std::move(matchFunc)),
      m_matchCondition(matchCondition),
      m_actionFunc(std::move(actionFunc)),
      m_priority(priority),
      m_requiresFlowHash(requiresFlowHash)
{
}

bool
RoutingRule::Match(const StructuredAddress& src,
                   const StructuredAddress& dst,
                   const RoutingContext& ctx) const
{
    return m_matchFunc(src, dst, ctx) == m_matchCondition;
}

std::optional<uint32_t>
RoutingRule::Action(const PortSet& ports,
                    const StructuredAddress& src,
                    const StructuredAddress& dst,
                    const RoutingContext& ctx) const
{
    return m_actionFunc(ports, src, dst, ctx);
}

const uint32_t
RoutingRule::GetPriority() const
{
    return m_priority;
}

bool
RoutingRule::RequiresFlowHash() const
{
    return m_requiresFlowHash;
}

void
RoutingRule::SetName(std::string name)
{
    m_name = name;
}

std::string
RoutingRule::GetName() const
{
    return m_name;
}

} // namespace ns3
