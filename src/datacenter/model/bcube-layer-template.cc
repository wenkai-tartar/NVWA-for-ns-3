/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "bcube-layer-template.h"

#include "rule-based-routing.h"
#include "structured-address.h"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("BCubeLayerTemplate");

NS_OBJECT_ENSURE_REGISTERED(BCubeLayerTemplate);
NS_OBJECT_ENSURE_REGISTERED(ZCubeLayerTemplate);

TypeId
BCubeLayerTemplate::GetTypeId()
{
    static TypeId tid = TypeId("ns3::BCubeLayerTemplate")
                            .SetParent<InterLevelTemplate>()
                            .SetGroupName("Datacenter");
    return tid;
}

BCubeLayerTemplate::BCubeLayerTemplate()
{
    NS_LOG_FUNCTION(this);
}

BCubeLayerTemplate::BCubeLayerTemplate(uint32_t levelId,
                                       uint32_t dimId,
                                       uint32_t nodeNum,
                                       uint32_t subBlockNum,
                                       const LinkProfile& link)
    : InterLevelTemplate(levelId, dimId, nodeNum, subBlockNum, link)
{
    NS_LOG_FUNCTION(this << nodeNum << subBlockNum);
}

BCubeLayerTemplate::~BCubeLayerTemplate()
{
    NS_LOG_FUNCTION(this);
}

void
BCubeLayerTemplate::Connect(StructuredTopology& topo, uint32_t levelId, uint32_t dimId)
{
    NS_LOG_FUNCTION(this << levelId << dimId);
    NS_ASSERT_MSG(levelId == m_levelId, "Connect: levelId mismatch");
    NS_ASSERT_MSG(dimId == m_dimId, "Connect: dimId mismatch");
    NS_ASSERT_MSG(m_nodeNum > 0, "BCubeLayer requires nodeNum > 0");
    NS_ASSERT_MSG(m_subBlockNum > 0, "BCubeLayer requires subBlockNum > 0");

    const uint32_t endpointLevelId = 0;
    uint32_t parentCount = topo.GetLevel(levelId).GetN();
    uint32_t endpointCount = topo.GetLevel(endpointLevelId).GetN();
    NS_ASSERT_MSG(parentCount == m_nodeNum, "BCubeLayer: parent count must equal nodeNum");
    NS_ASSERT_MSG(endpointCount >= parentCount,
                  "BCubeLayer: endpoint count must be at least nodeNum");
    NS_ASSERT_MSG(endpointCount % parentCount == 0,
                  "BCubeLayer: endpoint count must be divisible by nodeNum");

    for (uint32_t pLocal = 0; pLocal < parentCount; ++pLocal)
    {
        topo.SetStructuredAddrByLocal(levelId,
                                      pLocal,
                                      StructuredAddress{levelId + 1, pLocal + 1});

        Ptr<RuleBasedRouting> pRouting =
            DynamicCast<RuleBasedRouting>(topo.GetRoutingProtocolByLocal(levelId, pLocal));

        for (uint32_t block = 0; block < endpointCount / parentCount; ++block)
        {
            uint32_t cLocal = block * parentCount + pLocal;
            Ptr<RuleBasedRouting> cRouting = DynamicCast<RuleBasedRouting>(
                topo.GetRoutingProtocolByLocal(endpointLevelId, cLocal));

            std::pair<uint32_t, uint32_t> devs = m_topoHelper->ConnectNodes(topo,
                                                                            levelId,
                                                                            dimId,
                                                                            pLocal,
                                                                            endpointLevelId,
                                                                            cLocal);

            const StructuredAddress& cAddr =
                topo.GetStructuredAddrByLocal(endpointLevelId, cLocal);
            if (pRouting)
            {
                pRouting->GetPortSet().AddDownward(DestinationBucketKey(cAddr), devs.first);
            }
            if (cRouting)
            {
                cRouting->GetPortSet().AddUpward(UpwardBucketKey(pLocal), devs.second);
            }
        }
    }
}

void
BCubeLayerTemplate::GenerateRoutingRules(uint32_t levelId, uint32_t dimId)
{
    NS_LOG_FUNCTION(this << levelId << dimId);
    RoutingRuleManager* ruleManager = RoutingRuleManager::GetInstance();

    auto switchMatch = [this](const StructuredAddress& src,
                              const StructuredAddress& dst,
                              const RoutingContext& ctx) { return MatchSwitch(src, dst, ctx); };
    auto switchAction = [this](const PortSet& ports,
                               const StructuredAddress& src,
                               const StructuredAddress& dst,
                               const RoutingContext& ctx) {
        return SelectSwitchDownward(ports, src, dst, ctx);
    };
    Ptr<RoutingRule> switchRule =
        CreateObject<RoutingRule>(switchMatch,
                                  MatchCondition::MatchFailure,
                                  switchAction,
                                  1000 + levelId,
                                  m_portSelectPolicy != PortSelectPolicy::kFirst);
    switchRule->SetName(RuleNamePrefix() + "_level_" + std::to_string(levelId) +
                        "_downward_rule");
    ruleManager->AddRule(levelId, switchRule);
    ruleManager->FreezeLevel(levelId);

    auto endpointMatch = [this](const StructuredAddress& src,
                                const StructuredAddress& dst,
                                const RoutingContext& ctx) { return MatchEndpoint(src, dst, ctx); };
    auto endpointAction = [this](const PortSet& ports,
                                 const StructuredAddress& src,
                                 const StructuredAddress& dst,
                                 const RoutingContext& ctx) {
        return SelectEndpointUpward(ports, src, dst, ctx);
    };
    Ptr<RoutingRule> endpointRule =
        CreateObject<RoutingRule>(endpointMatch,
                                  MatchCondition::MatchFailure,
                                  endpointAction,
                                  1000 + levelId,
                                  m_portSelectPolicy != PortSelectPolicy::kFirst);
    endpointRule->SetName(RuleNamePrefix() + "_level_" + std::to_string(levelId) +
                          "_endpoint_upward_rule");
    ruleManager->AddRule(0, endpointRule);
    ruleManager->FreezeLevel(0);
}

MatchCondition
BCubeLayerTemplate::MatchEndpoint(const StructuredAddress& src,
                                  const StructuredAddress& dst,
                                  const RoutingContext& ctx)
{
    if (ctx.levelId != 0 || src.ToString('.', false) == dst.ToString('.', false))
    {
        return MatchCondition::MatchSuccess;
    }
    return MatchCondition::MatchFailure;
}

MatchCondition
BCubeLayerTemplate::MatchSwitch(const StructuredAddress& src,
                                const StructuredAddress& dst,
                                const RoutingContext& ctx)
{
    if (ctx.levelId != m_levelId || src.ToString('.', false) == dst.ToString('.', false))
    {
        return MatchCondition::MatchSuccess;
    }
    return MatchCondition::MatchFailure;
}

std::optional<uint32_t>
BCubeLayerTemplate::SelectEndpointUpward(const PortSet& ports,
                                         const StructuredAddress& src,
                                         const StructuredAddress& dst,
                                         const RoutingContext& ctx)
{
    PortSelector selector(m_portSelectPolicy);
    const auto& upwardPorts =
        ports.GetAvailableUpward(UpwardBucketKey(SwitchLocalForDestination(dst)));
    if (upwardPorts.empty())
    {
        return std::nullopt;
    }
    return selector.Pick(upwardPorts, ctx);
}

std::optional<uint32_t>
BCubeLayerTemplate::SelectSwitchDownward(const PortSet& ports,
                                         const StructuredAddress& src,
                                         const StructuredAddress& dst,
                                         const RoutingContext& ctx)
{
    PortSelector selector(m_portSelectPolicy);
    const auto& downwardPorts = ports.GetAvailableDownward(DestinationBucketKey(dst));
    if (downwardPorts.empty())
    {
        return std::nullopt;
    }
    return selector.Pick(downwardPorts, ctx);
}

std::string
BCubeLayerTemplate::UpwardBucketKey(uint32_t switchLocal) const
{
    return "L" + std::to_string(m_levelId) + ":" + std::to_string(switchLocal);
}

uint32_t
BCubeLayerTemplate::SwitchLocalForDestination(const StructuredAddress& dst) const
{
    if (m_nodeNum == 0 || m_subBlockNum == 0)
    {
        return 0;
    }

    uint64_t local = 0;
    uint64_t multiplier = 1;
    for (size_t i = 0; i < dst.Size() && multiplier < m_nodeNum; ++i)
    {
        uint32_t field = dst[i] == 0 ? 0 : dst[i] - 1;
        local += static_cast<uint64_t>(field) * multiplier;
        multiplier *= m_subBlockNum;
    }
    return static_cast<uint32_t>(local % m_nodeNum);
}

std::string
BCubeLayerTemplate::DestinationBucketKey(const StructuredAddress& dst) const
{
    if (dst.Empty())
    {
        return "";
    }
    size_t end = std::min<size_t>(m_levelId - 1, dst.Size() - 1);
    return dst.ToStringRange(0, end, '.', false);
}

std::string
BCubeLayerTemplate::RuleNamePrefix() const
{
    return "BCubeLayer";
}

TypeId
ZCubeLayerTemplate::GetTypeId()
{
    static TypeId tid = TypeId("ns3::ZCubeLayerTemplate")
                            .SetParent<BCubeLayerTemplate>()
                            .SetGroupName("Datacenter");
    return tid;
}

ZCubeLayerTemplate::ZCubeLayerTemplate()
{
    NS_LOG_FUNCTION(this);
}

ZCubeLayerTemplate::ZCubeLayerTemplate(uint32_t levelId,
                                       uint32_t dimId,
                                       uint32_t nodeNum,
                                       uint32_t subBlockNum,
                                       const LinkProfile& link)
    : BCubeLayerTemplate(levelId, dimId, nodeNum, subBlockNum, link)
{
    NS_LOG_FUNCTION(this << nodeNum << subBlockNum);
}

ZCubeLayerTemplate::~ZCubeLayerTemplate()
{
    NS_LOG_FUNCTION(this);
}

std::string
ZCubeLayerTemplate::RuleNamePrefix() const
{
    return "ZCubeLayer";
}

} // namespace ns3
