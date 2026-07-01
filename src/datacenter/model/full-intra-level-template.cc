/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#include "full-intra-level-template.h"

#include "rule-based-routing.h"
#include "structured-address.h"

#include "ns3/structured-address-helper.h"

#include <cstdint>
#include <optional>
#include <string>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("FullIntraLevelTemplate");

NS_OBJECT_ENSURE_REGISTERED(FullIntraLevelTemplate);

TypeId
FullIntraLevelTemplate::GetTypeId()
{
    static TypeId tid = TypeId("ns3::FullIntraLevelTemplate")
                            .SetParent<IntraLevelTemplate>()
                            .SetGroupName("Datacenter");
    return tid;
}

FullIntraLevelTemplate::FullIntraLevelTemplate()
{
    NS_LOG_FUNCTION(this);
}

FullIntraLevelTemplate::FullIntraLevelTemplate(uint32_t levelId,
                                               uint32_t dimId,
                                               uint32_t nodeNum,
                                               uint32_t subBlockNum,
                                               uint32_t outLinkNum,
                                               std::string linkArrangement,
                                               const LinkProfile& link)
    : IntraLevelTemplate(levelId, dimId, nodeNum, subBlockNum, link),
      m_outLinkNum(outLinkNum),
      m_linkArrangement(linkArrangement)
{
    NS_LOG_FUNCTION(this << nodeNum << subBlockNum);
}

FullIntraLevelTemplate::~FullIntraLevelTemplate()
{
    NS_LOG_FUNCTION(this);
}

/**
 * Connect each node in the top level of the topology to nodes in the top level of its neighboring
 * topologies
 */
void
FullIntraLevelTemplate::Connect(StructuredTopology& topo, uint32_t levelId, uint32_t dimId)
{
    NS_LOG_FUNCTION(this << levelId << dimId);
    NS_ASSERT_MSG(levelId == m_levelId, "Connect: levelId mismatch");
    NS_ASSERT_MSG(dimId == m_dimId, "Connect: dimId mismatch");

    uint32_t groupNum = m_subBlockNum;
    uint32_t groupSize = topo.GetLevel(levelId).GetN() / groupNum;

    // Full size Dragonfly
    if (m_linkArrangement != "SameRank")
    {
        NS_ASSERT_MSG(m_outLinkNum * groupSize == groupNum - 1,
                      "Connect: outLinkNum * groupSize must be equal to groupNum - 1");
    }

    for (uint32_t cGroup = 0; cGroup < groupNum; ++cGroup)
    {
        for (uint32_t clocalIdx = 0; clocalIdx < groupSize; ++clocalIdx)
        {
            uint32_t cLocal = cGroup * groupSize + clocalIdx;
            Ptr<RuleBasedRouting> cRouting =
                DynamicCast<RuleBasedRouting>(topo.GetRoutingProtocolByLocal(levelId, cLocal));

            if (m_linkArrangement == "Absolute")
            {
                for (uint32_t h = 0; h < m_outLinkNum; ++h)
                {
                    uint32_t cPortIdx = m_outLinkNum * clocalIdx + h;
                    if (cPortIdx >= cGroup)
                    {
                        uint32_t rGroup = cPortIdx + 1;
                        if (cGroup < rGroup)
                        {
                            std::uint32_t rLocal = rGroup * groupSize + (cGroup / m_outLinkNum);
                            Ptr<RuleBasedRouting> rRouting = DynamicCast<RuleBasedRouting>(
                                topo.GetRoutingProtocolByLocal(levelId, rLocal));

                            std::pair<uint32_t, uint32_t> devs = m_topoHelper->ConnectNodes(topo,
                                                                                            levelId,
                                                                                            dimId,
                                                                                            cLocal,
                                                                                            levelId,
                                                                                            rLocal);

                            if (cRouting)
                            {
                                cRouting->GetPortSet().AddSameLevel(dimId,
                                                                    std::to_string(rGroup + 1),
                                                                    devs.first);
                            }
                            if (rRouting)
                            {
                                rRouting->GetPortSet().AddSameLevel(dimId,
                                                                    std::to_string(cGroup + 1),
                                                                    devs.second);
                            }
                        }
                    }
                }
            }
            else if (m_linkArrangement == "SameRank")
            {
                for (uint32_t rGroup = cGroup + 1; rGroup < groupNum; ++rGroup)
                {
                    uint32_t rLocal = rGroup * groupSize + clocalIdx;
                    Ptr<RuleBasedRouting> rRouting = DynamicCast<RuleBasedRouting>(
                        topo.GetRoutingProtocolByLocal(levelId, rLocal));

                    std::pair<uint32_t, uint32_t> devs =
                        m_topoHelper->ConnectNodes(topo, levelId, dimId, cLocal, levelId, rLocal);

                    if (cRouting)
                    {
                        cRouting->GetPortSet().AddSameLevel(dimId,
                                                            std::to_string(rGroup + 1),
                                                            devs.first);
                    }
                    if (rRouting)
                    {
                        rRouting->GetPortSet().AddSameLevel(dimId,
                                                            std::to_string(cGroup + 1),
                                                            devs.second);
                    }
                }
            }
        }
    }
}

void
FullIntraLevelTemplate::GenerateRoutingRules(uint32_t levelId, uint32_t dimId)
{
    NS_LOG_FUNCTION(this << levelId);

    RoutingRuleManager* ruleManager = RoutingRuleManager::GetInstance();

    GenerateSameLevelRules(levelId, dimId, ruleManager);

    ruleManager->FreezeLevel(levelId);
}

void
FullIntraLevelTemplate::GenerateSameLevelRules(uint32_t levelId,
                                               uint32_t dimId,
                                               RoutingRuleManager* ruleManager)
{
    NS_LOG_FUNCTION(this << levelId);

    auto matchFunc = [this](const StructuredAddress& src,
                            const StructuredAddress& dst,
                            const RoutingContext& ctx) { return MatchDimension(src, dst, ctx); };
    auto actionFunc = [this](const PortSet& ports,
                             const StructuredAddress& src,
                             const StructuredAddress& dst,
                             const RoutingContext& ctx) {
        return SelectPortTheSameLevel(ports, src, dst, ctx);
    };

    MatchCondition matchCondition = MatchCondition::MatchFailure;

    Ptr<RoutingRule> rule =
        CreateObject<RoutingRule>(matchFunc,
                                  matchCondition,
                                  actionFunc,
                                  dimId * 100,
                                  m_portSelectPolicy != PortSelectPolicy::kFirst);
    rule->SetName("Level_" + std::to_string(levelId) + "_dim_" + std::to_string(dimId) +
                  "_same_level_rule");

    ruleManager->AddRule(levelId, rule);
}

MatchCondition
FullIntraLevelTemplate::MatchDimension(const StructuredAddress& src,
                                       const StructuredAddress& dst,
                                       const RoutingContext& ctx)
{
    NS_LOG_FUNCTION(this << src << dst);

    int dimAddrBit = ctx.topo->GetLevelAddrBit(m_levelId) + m_dimId;
    NS_ASSERT_MSG(dimAddrBit >= 0 && dimAddrBit < static_cast<int>(src.Size()),
                  "MatchDimension: dimAddrBit out of range");

    if (StructuredAddressHelper::CompareField(src, dst, dimAddrBit))
    {
        return MatchCondition::MatchSuccess;
    }
    else
    {
        return MatchCondition::MatchFailure;
    }
}

std::optional<uint32_t>
FullIntraLevelTemplate::SelectPortTheSameLevel(const PortSet& ports,
                                               const StructuredAddress& src,
                                               const StructuredAddress& dst,
                                               const RoutingContext& ctx)
{
    PortSelector selector(m_portSelectPolicy);
    int srcAddrBitValue = src[ctx.topo->GetLevelAddrBit(m_levelId) + m_dimId];
    int dstAddrBitValue = dst[ctx.topo->GetLevelAddrBit(m_levelId) + m_dimId];

    if (m_linkArrangement == "Absolute")
    {
        uint32_t nodeToRGroup;
        if (dstAddrBitValue < srcAddrBitValue)
        {
            nodeToRGroup = (dstAddrBitValue - 1) / m_outLinkNum;
        }
        else
        {
            nodeToRGroup = (dstAddrBitValue - 2) / m_outLinkNum;
        }

        uint32_t groupSize = ctx.topo->GetLevel(ctx.levelId).GetN() / m_subBlockNum;
        bool isBorder = ((ctx.localIdx % groupSize) == nodeToRGroup);

        const auto& sameLevelPorts =
            isBorder ? ports.GetAvailableSameLevel(m_dimId, std::to_string(dstAddrBitValue))
                     : ports.GetAvailableSameLevel(m_dimId - 1, std::to_string(nodeToRGroup + 1));
        if (sameLevelPorts.empty())
        {
            return std::nullopt;
        }
        return selector.Pick(sameLevelPorts, ctx);
    }
    else if (m_linkArrangement == "SameRank")
    {
        const auto& sameLevelPorts = ports.GetAvailableSameLevel(m_dimId, std::to_string(dstAddrBitValue));
        if (sameLevelPorts.empty())
        {
            return std::nullopt;
        }
        return selector.Pick(sameLevelPorts, ctx);
    }
    return std::nullopt;
}

} // namespace ns3
