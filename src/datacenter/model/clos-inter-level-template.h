/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#ifndef CLOS_INTER_LEVEL_TEMPLATE_H
#define CLOS_INTER_LEVEL_TEMPLATE_H

#include "inter-level-template.h"
#include "routing-rule-manager.h"
#include "structured-address.h"

#include "ns3/object.h"
#include "ns3/ptr.h"
#include "ns3/structured-topology.h"

#include <optional>
#include <string>

namespace ns3
{

class ClosInterLevelTemplate : public InterLevelTemplate
{
  public:
    /**
     * @brief Get the type ID.
     * @return the object TypeId
     */
    static TypeId GetTypeId();

    ClosInterLevelTemplate();

    ClosInterLevelTemplate(uint32_t levelId,
                           uint32_t dimId,
                           uint32_t nodeNum,
                           uint32_t subBlockNum,
                           uint32_t groupNum,
                           const LinkProfile& link);

    ClosInterLevelTemplate(uint32_t levelId,
                           uint32_t dimId,
                           uint32_t nodeNum,
                           uint32_t subBlockNum,
                           uint32_t groupNum,
                           std::string linkArrangement,
                           uint32_t endpointsPerServer,
                           uint32_t nicsPerAswitch,
                           const LinkProfile& link);

    ~ClosInterLevelTemplate() override;

    void Connect(StructuredTopology& topo, uint32_t levelId, uint32_t dimId) override;

    void GenerateRoutingRules(uint32_t levelId, uint32_t dimId) override;

    void GenerateParentLevelRules(uint32_t levelId, RoutingRuleManager* ruleManager);
    void GenerateChildLevelRules(uint32_t levelId, RoutingRuleManager* ruleManager);

    // Match function
    MatchCondition MatchPrefix(const StructuredAddress& src,
                               const StructuredAddress& dst,
                               const RoutingContext& ctx);

    std::optional<uint32_t> SelectPortUpward(const PortSet& ports,
                                             const StructuredAddress& src,
                                             const StructuredAddress& dst,
                                             const RoutingContext& ctx);

    std::optional<uint32_t> SelectPortDownward(const PortSet& ports,
                                               const StructuredAddress& src,
                                               const StructuredAddress& dst,
                                               const RoutingContext& ctx);

  private:
    void ConnectRailOptimized(StructuredTopology& topo, uint32_t levelId, uint32_t dimId);

    uint32_t m_groupNum{1};
    std::string m_linkArrangement{"Contiguous"};
    uint32_t m_endpointsPerServer{0};
    uint32_t m_nicsPerAswitch{0};
};

} // namespace ns3

#endif /* CLOS_INTER_LEVEL_TEMPLATE_H */