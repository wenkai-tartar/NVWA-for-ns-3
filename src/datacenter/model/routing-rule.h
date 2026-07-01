/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#ifndef ROUTING_RULE_H
#define ROUTING_RULE_H

#include "routing-common.h"
#include "structured-address.h" // See existing file for StructuredAddress, AddressPattern

#include "ns3/object.h"
#include "ns3/ptr.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ns3
{

/**
 * @brief Abstract base class for routing rules
 *
 * Design points:
 * - Rules are not limited to "prefix classification", but allow templates to encode any matching
 * method into rules;
 * - Match can use AddressPattern/handwritten logic/state machine, etc.;
 * - Action executes the selection policy (or more complex actions) on named port buckets.
 */
class RoutingRule : public Object
{
  public:
    static TypeId GetTypeId();

    using MatchFunction = std::function<
        MatchCondition(const StructuredAddress&, const StructuredAddress&, const RoutingContext&)>;
    using ActionFunction = std::function<std::optional<uint32_t>(const PortSet&,
                                                                 const StructuredAddress&,
                                                                 const StructuredAddress&,
                                                                 const RoutingContext&)>;

    RoutingRule(MatchFunction matchFunc,
                MatchCondition matchCondition,
                ActionFunction actionFunc,
                uint32_t priority,
                bool requiresFlowHash = false);

    bool Match(const StructuredAddress& src,
               const StructuredAddress& dst,
               const RoutingContext& ctx) const;

    std::optional<uint32_t> Action(const PortSet& ports,
                                   const StructuredAddress& src,
                                   const StructuredAddress& dst,
                                   const RoutingContext& ctx) const;

    const uint32_t GetPriority() const;

    bool RequiresFlowHash() const;

    void SetName(std::string name);

    std::string GetName() const;

  private:
    MatchFunction m_matchFunc;
    MatchCondition m_matchCondition;
    ActionFunction m_actionFunc;
    uint32_t m_priority;
    bool m_requiresFlowHash{false};

    std::string m_name; // for debugging
};

} // namespace ns3

#endif // ROUTING_RULE_H