/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#ifndef INTRA_SERVER_LEVEL_TEMPLATE_H
#define INTRA_SERVER_LEVEL_TEMPLATE_H

#include "level-template.h"
#include "routing-rule-manager.h"
#include "structured-address.h"

#include "ns3/object.h"
#include "ns3/ptr.h"
#include "ns3/structured-topology.h"

#include <cstdint>
#include <optional>
#include <string>

namespace ns3
{

class IntraServerLevelTemplate : public LevelTemplate
{
  public:
    static TypeId GetTypeId();

    IntraServerLevelTemplate();

    IntraServerLevelTemplate(uint32_t serverNum,
                             uint32_t endpointsPerServer,
                             std::string linkArrangement,
                             const LinkProfile& link);

    ~IntraServerLevelTemplate() override;

    std::pair<uint32_t, uint32_t> Build(StructuredTopology& topo) override;
    void Connect(StructuredTopology& topo, uint32_t levelId, uint32_t dimId) override;
    void GenerateRoutingRules(uint32_t levelId, uint32_t dimId) override;

    MatchCondition MatchSameServer(const StructuredAddress& src,
                                   const StructuredAddress& dst,
                                   const RoutingContext& ctx);

    std::optional<uint32_t> SelectIntraServerPort(const PortSet& ports,
                                                  const StructuredAddress& src,
                                                  const StructuredAddress& dst,
                                                  const RoutingContext& ctx);

  private:
    uint32_t m_serverNum{0};
    uint32_t m_endpointsPerServer{0};
    std::string m_linkArrangement{"FullMesh"};
};

} // namespace ns3

#endif /* INTRA_SERVER_LEVEL_TEMPLATE_H */
