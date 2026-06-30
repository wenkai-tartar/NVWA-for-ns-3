/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef MESH_INTRA_LEVEL_TEMPLATE_H
#define MESH_INTRA_LEVEL_TEMPLATE_H

#include "intra-level-template.h"
#include "routing-rule-manager.h"

#include <optional>
#include <string>

namespace ns3
{

class MeshIntraLevelTemplate : public IntraLevelTemplate
{
  public:
    static TypeId GetTypeId();

    MeshIntraLevelTemplate();

    MeshIntraLevelTemplate(uint32_t levelId,
                           uint32_t dimId,
                           uint32_t nodeNum,
                           uint32_t subBlockNum,
                           std::string linkArrangement,
                           const LinkProfile& link);

    ~MeshIntraLevelTemplate() override;

    void Connect(StructuredTopology& topo, uint32_t levelId, uint32_t dimId) override;

    void GenerateRoutingRules(uint32_t levelId, uint32_t dimId) override;

    void GenerateSameLevelRules(uint32_t levelId, uint32_t dimId, RoutingRuleManager* ruleManager);

    MatchCondition MatchDimension(const StructuredAddress& src,
                                  const StructuredAddress& dst,
                                  const RoutingContext& ctx);

    std::optional<uint32_t> SelectPortTheSameLevel(const PortSet& ports,
                                                   const StructuredAddress& src,
                                                   const StructuredAddress& dst,
                                                   const RoutingContext& ctx);

  private:
    std::string m_linkArrangement{"SameRank"};
};

} // namespace ns3

#endif /* MESH_INTRA_LEVEL_TEMPLATE_H */
