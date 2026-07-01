/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#ifndef TORUS_INTRA_LEVEL_TEMPLATE_H
#define TORUS_INTRA_LEVEL_TEMPLATE_H

#include "intra-level-template.h"
#include "routing-rule-manager.h"

#include "ns3/object.h"
#include "ns3/ptr.h"
#include "ns3/structured-topology.h"

#include <cstdint>

namespace ns3
{

const std::string positiveDirectionKey = "1";
const std::string negativeDirectionKey = "0";

class TorusIntraLevelTemplate : public IntraLevelTemplate
{
  public:
    static TypeId GetTypeId();

    TorusIntraLevelTemplate();

    TorusIntraLevelTemplate(uint32_t levelId,
                            uint32_t dimId,
                            uint32_t nodeNum,
                            uint32_t subBlockNum,
                            std::string linkArrangement,
                            const LinkProfile& link);

    ~TorusIntraLevelTemplate() override;

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
    std::string m_linkArrangement{"SameRank"};
};

} // namespace ns3

#endif /* TORUS_INTRA_LEVEL_TEMPLATE_H */