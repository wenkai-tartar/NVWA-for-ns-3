/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef BCUBE_LAYER_TEMPLATE_H
#define BCUBE_LAYER_TEMPLATE_H

#include "inter-level-template.h"

#include <optional>
#include <string>

namespace ns3
{

class BCubeLayerTemplate : public InterLevelTemplate
{
  public:
    static TypeId GetTypeId();

    BCubeLayerTemplate();

    BCubeLayerTemplate(uint32_t levelId,
                       uint32_t dimId,
                       uint32_t nodeNum,
                       uint32_t subBlockNum,
                       const LinkProfile& link);

    ~BCubeLayerTemplate() override;

    void Connect(StructuredTopology& topo, uint32_t levelId, uint32_t dimId) override;

    void GenerateRoutingRules(uint32_t levelId, uint32_t dimId) override;

    MatchCondition MatchEndpoint(const StructuredAddress& src,
                                 const StructuredAddress& dst,
                                 const RoutingContext& ctx);

    MatchCondition MatchSwitch(const StructuredAddress& src,
                               const StructuredAddress& dst,
                               const RoutingContext& ctx);

    std::optional<uint32_t> SelectEndpointUpward(const PortSet& ports,
                                                 const StructuredAddress& src,
                                                 const StructuredAddress& dst,
                                                 const RoutingContext& ctx);

    std::optional<uint32_t> SelectSwitchDownward(const PortSet& ports,
                                                 const StructuredAddress& src,
                                                 const StructuredAddress& dst,
                                                 const RoutingContext& ctx);

  protected:
    std::string UpwardBucketKey(uint32_t switchLocal) const;
    uint32_t SwitchLocalForDestination(const StructuredAddress& dst) const;
    std::string DestinationBucketKey(const StructuredAddress& dst) const;
    virtual std::string RuleNamePrefix() const;
};

class ZCubeLayerTemplate : public BCubeLayerTemplate
{
  public:
    static TypeId GetTypeId();

    ZCubeLayerTemplate();

    ZCubeLayerTemplate(uint32_t levelId,
                       uint32_t dimId,
                       uint32_t nodeNum,
                       uint32_t subBlockNum,
                       const LinkProfile& link);

    ~ZCubeLayerTemplate() override;

  protected:
    std::string RuleNamePrefix() const override;
};

} // namespace ns3

#endif /* BCUBE_LAYER_TEMPLATE_H */
