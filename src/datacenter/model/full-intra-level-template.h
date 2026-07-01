/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#ifndef FULL_INTRA_LEVEL_TEMPLATE_H
#define FULL_INTRA_LEVEL_TEMPLATE_H

#include "intra-level-template.h"
#include "routing-rule-manager.h"

#include "ns3/object.h"
#include "ns3/ptr.h"
#include "ns3/structured-topology.h"

#include <cstdint>

namespace ns3
{

class FullIntraLevelTemplate : public IntraLevelTemplate
{
  public:
    static TypeId GetTypeId();

    FullIntraLevelTemplate();

    FullIntraLevelTemplate(uint32_t levelId,
                           uint32_t dimId,
                           uint32_t nodeNum,
                           uint32_t subBlockNum,
                           uint32_t outLinkNum,
                           std::string linkArrangement,
                           const LinkProfile& link);

    ~FullIntraLevelTemplate() override;

    void Connect(StructuredTopology& topo, uint32_t levelId, uint32_t dimId) override;

    void GenerateRoutingRules(uint32_t levelId, uint32_t dimId) override;

    void GenerateSameLevelRules(uint32_t levelId, uint32_t dimId, RoutingRuleManager* ruleManager);

    // Match function
    MatchCondition MatchDimension(const StructuredAddress& src,
                                  const StructuredAddress& dst,
                                  const RoutingContext& ctx);
    // Action function
    std::optional<uint32_t> SelectPortTheSameLevel(const PortSet& ports,
                                                   const StructuredAddress& src,
                                                   const StructuredAddress& dst,
                                                   const RoutingContext& ctx);

  private:
    uint32_t m_outLinkNum{0};
    std::string m_linkArrangement{"Absolute"};
};

} // namespace ns3

#endif /* FULL_INTRA_LEVEL_TEMPLATE_H */