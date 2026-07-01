/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#ifndef LEVEL_TEMPLATE_H
#define LEVEL_TEMPLATE_H

#include "routing-common.h"
#include "structured-topology.h"

#include "ns3/data-rate.h"
#include "ns3/object.h"
#include "ns3/ptr.h"
#include "ns3/topology-helper.h"

#include <cstdint>
#include <memory>

namespace ns3
{

class LevelTemplate : public Object
{
  public:
    /**
     * @brief Get the type ID.
     * @return the object TypeId
     */
    static TypeId GetTypeId();

    struct LinkProfile
    {
        ns3::DataRate rate;
        ns3::Time delay;
    };

    LevelTemplate();

    LevelTemplate(uint32_t levelId,
                  uint32_t dimId,
                  uint32_t nodeNum,
                  uint32_t subBlockNum,
                  const LinkProfile& link);

    ~LevelTemplate() override;

    /**
     * Build on top of previous topology
     * @return the global level id of current level
     */
    virtual std::pair<uint32_t, uint32_t> Build(StructuredTopology& topo) = 0;

    virtual void Connect(StructuredTopology& topo, uint32_t levelId, uint32_t dimId) = 0;

    virtual void GenerateRoutingRules(uint32_t levelId, uint32_t dimId) = 0;

    /**
     * Set the TopologyHelper to be shared with StructuredTopology
     */
    void SetTopologyHelper(std::shared_ptr<TopologyHelper> topoHelper);

    /**
     * Set the port selection policy for ECMP
     */
    void SetPortSelectPolicy(PortSelectPolicy policy);

  protected:
    uint32_t m_levelId{0};
    uint32_t m_dimId{0};

    uint32_t m_nodeNum{0};
    uint32_t m_subBlockNum{0};
    LinkProfile m_link;
    PortSelectPolicy m_portSelectPolicy = PortSelectPolicy::kFirst;

    std::shared_ptr<TopologyHelper> m_topoHelper;
};

} // namespace ns3

#endif /* LEVEL_TEMPLATE_H */
