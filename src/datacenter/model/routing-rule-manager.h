/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#ifndef ROUTING_RULE_MANAGER_H
#define ROUTING_RULE_MANAGER_H

#include "routing-rule.h"

#include "ns3/simulation-singleton.h"

#include <unordered_map>
#include <vector>

namespace ns3
{

/**
 * @brief Centralized routing rule manager for data center topologies
 *
 * Manages routing rules by level to avoid duplication across nodes.
 * Each level has its own set of routing rules that are shared by all nodes in that level.
 * Implemented as a simulation singleton - automatically destroyed when simulation ends.
 */
class RoutingRuleManager : public Object
{
  public:
    /**
     * @brief Get the singleton instance
     * @return Pointer to the singleton instance
     */
    static RoutingRuleManager* GetInstance();

    // Disable copy constructor and assignment operator
    RoutingRuleManager(const RoutingRuleManager&) = delete;
    RoutingRuleManager& operator=(const RoutingRuleManager&) = delete;

    /**
     * @brief Add a routing rule to a specific level
     * @param levelId The level identifier
     * @param rule The routing rule to add
     */
    void AddRule(uint32_t levelId, Ptr<RoutingRule> rule);

    /**
     * @brief Freeze rules for a level (sort by priority)
     * @param levelId The level identifier
     */
    void FreezeLevel(uint32_t levelId);

    /**
     * @brief Get rules for a specific level
     * @param levelId The level identifier
     * @return Const reference to the rules vector
     */
    const std::vector<Ptr<RoutingRule>>& GetRules(uint32_t levelId) const;

    /**
     * @brief Check if a level has rules
     * @param levelId The level identifier
     * @return True if the level has rules
     */
    bool HasRules(uint32_t levelId) const;

    /**
     * @brief Check if any rule for a level may call RoutingContext::GetFlowHash()
     */
    bool LevelRequiresFlowHash(uint32_t levelId) const;

    /**
     * @brief Get the number of levels with rules
     * @return Number of levels
     */
    uint32_t GetNumLevels() const;

    /**
     * @brief Clear all rules
     */
    void Clear();

    /**
     * @brief Clear rules for a specific level
     * @param levelId The level identifier
     */
    void ClearLevel(uint32_t levelId);

  private:
    friend class SimulationSingleton<RoutingRuleManager>;

    // Private constructor (SimulationSingleton requires)
    RoutingRuleManager();
    ~RoutingRuleManager() override;

    // levelId -> vector of routing rules (sorted by priority after Freeze)
    std::unordered_map<uint32_t, std::vector<Ptr<RoutingRule>>> m_levelRules;
    std::unordered_map<uint32_t, bool> m_levelRequiresFlowHash;
};

// Declare as SimulationSingleton
typedef SimulationSingleton<RoutingRuleManager> RoutingRuleManagerSingleton;

} // namespace ns3

#endif // ROUTING_RULE_MANAGER_H