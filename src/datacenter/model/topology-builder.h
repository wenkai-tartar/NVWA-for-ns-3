/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#ifndef TOPOLOGY_BUILDER_H
#define TOPOLOGY_BUILDER_H

#include "level-template.h"

#include "ns3/ptr.h"

#include <vector>

namespace ns3
{

class TopologyBuilder
{
  public:
    TopologyBuilder();

    ~TopologyBuilder();

    TopologyBuilder& AddLevel(Ptr<LevelTemplate> level);

    Ptr<StructuredTopology> Build(Ptr<StructuredTopology> topo);

  private:
    std::vector<Ptr<LevelTemplate>> m_levels;
};

} // namespace ns3

#endif /* TOPOLOGY_BUILDER_H */