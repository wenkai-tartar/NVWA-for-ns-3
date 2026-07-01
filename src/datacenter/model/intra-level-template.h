/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#ifndef INTRA_LEVEL_TEMPLATE_H
#define INTRA_LEVEL_TEMPLATE_H

#include "level-template.h"

#include "ns3/object.h"
#include "ns3/ptr.h"
#include "ns3/structured-topology.h"

namespace ns3
{

class IntraLevelTemplate : public LevelTemplate
{
  public:
    static TypeId GetTypeId();

    IntraLevelTemplate();

    IntraLevelTemplate(uint32_t levelId,
                       uint32_t dimId,
                       uint32_t nodeNum,
                       uint32_t subBlockNum,
                       const LinkProfile& link);

    ~IntraLevelTemplate() override;

    std::pair<uint32_t, uint32_t> Build(StructuredTopology& topo) override;

    void GenerateRoutingRules(uint32_t levelId, uint32_t dimId) override;
};

} // namespace ns3

#endif /* INTRA_LEVEL_TEMPLATE_H */