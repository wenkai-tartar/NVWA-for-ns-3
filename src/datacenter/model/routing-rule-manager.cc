/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#include "routing-rule-manager.h"

#include "ns3/log.h"
#include "ns3/simulator.h"

#include <algorithm>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("RoutingRuleManager");

RoutingRuleManager*
RoutingRuleManager::GetInstance()
{
    return RoutingRuleManagerSingleton::Get();
}

RoutingRuleManager::RoutingRuleManager()
{
    NS_LOG_FUNCTION(this);
}

RoutingRuleManager::~RoutingRuleManager()
{
    NS_LOG_FUNCTION(this);
    Clear();
}

void
RoutingRuleManager::AddRule(uint32_t levelId, Ptr<RoutingRule> rule)
{
    NS_LOG_FUNCTION(this << levelId << rule);

    if (!rule)
    {
        NS_LOG_WARN("Attempting to add null routing rule");
        return;
    }

    m_levelRules[levelId].push_back(rule);
    if (rule->RequiresFlowHash())
    {
        m_levelRequiresFlowHash[levelId] = true;
    }
}

void
RoutingRuleManager::FreezeLevel(uint32_t levelId)
{
    NS_LOG_FUNCTION(this << levelId);

    auto it = m_levelRules.find(levelId);
    if (it == m_levelRules.end())
    {
        NS_LOG_WARN("No rules found for level " << levelId);
        return;
    }

    // Sort rules by priority (descending order - higher priority first)
    std::sort(it->second.begin(),
              it->second.end(),
              [](const Ptr<RoutingRule>& a, const Ptr<RoutingRule>& b) {
                  return a->GetPriority() > b->GetPriority();
              });

    NS_LOG_DEBUG("Frozen " << it->second.size() << " rules for level " << levelId);
}

const std::vector<Ptr<RoutingRule>>&
RoutingRuleManager::GetRules(uint32_t levelId) const
{
    static const std::vector<Ptr<RoutingRule>> emptyRules;

    auto it = m_levelRules.find(levelId);
    if (it != m_levelRules.end())
    {
        return it->second;
    }

    NS_LOG_WARN("No rules found for level " << levelId << ", returning empty rules");
    return emptyRules;
}

bool
RoutingRuleManager::HasRules(uint32_t levelId) const
{
    auto it = m_levelRules.find(levelId);
    return it != m_levelRules.end() && !it->second.empty();
}

bool
RoutingRuleManager::LevelRequiresFlowHash(uint32_t levelId) const
{
    auto it = m_levelRequiresFlowHash.find(levelId);
    return it != m_levelRequiresFlowHash.end() && it->second;
}

uint32_t
RoutingRuleManager::GetNumLevels() const
{
    return m_levelRules.size();
}

void
RoutingRuleManager::Clear()
{
    NS_LOG_FUNCTION(this);

    for (auto& levelRules : m_levelRules)
    {
        levelRules.second.clear();
    }
    m_levelRules.clear();
    m_levelRequiresFlowHash.clear();
}

void
RoutingRuleManager::ClearLevel(uint32_t levelId)
{
    NS_LOG_FUNCTION(this << levelId);

    auto it = m_levelRules.find(levelId);
    if (it != m_levelRules.end())
    {
        it->second.clear();
        m_levelRules.erase(it);
        m_levelRequiresFlowHash.erase(levelId);
    }
}

} // namespace ns3