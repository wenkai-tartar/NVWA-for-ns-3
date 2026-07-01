/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#ifndef INTER_LEVEL_TEMPLATE_H
#define INTER_LEVEL_TEMPLATE_H

#include "level-template.h"

#include "ns3/object.h"
#include "ns3/ptr.h"
#include "ns3/structured-topology.h"

namespace ns3
{

class InterLevelTemplate : public LevelTemplate
{
  public:
    /**
     * @brief Get the type ID.
     * @return the object TypeId
     */
    static TypeId GetTypeId();

    InterLevelTemplate();

    InterLevelTemplate(uint32_t levelId,
                       uint32_t dimId,
                       uint32_t nodeNum,
                       uint32_t subBlockNum,
                       const LinkProfile& link);

    ~InterLevelTemplate() override;

    std::pair<uint32_t, uint32_t> Build(StructuredTopology& topo) override;

    void GenerateRoutingRules(uint32_t levelId, uint32_t dimId) override;
};

} // namespace ns3

#endif /* INTER_LEVEL_TEMPLATE_H */