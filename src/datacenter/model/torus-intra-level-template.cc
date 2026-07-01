/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#include "torus-intra-level-template.h"

#include "rule-based-routing.h"
#include "structured-address.h"

#include "ns3/structured-address-helper.h"

#include <cstdint>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("TorusIntraLevelTemplate");

NS_OBJECT_ENSURE_REGISTERED(TorusIntraLevelTemplate);

TypeId
TorusIntraLevelTemplate::GetTypeId()
{
    static TypeId tid = TypeId("ns3::TorusIntraLevelTemplate")
                            .SetParent<IntraLevelTemplate>()
                            .SetGroupName("Datacenter");
    return tid;
}

TorusIntraLevelTemplate::TorusIntraLevelTemplate()
{
    NS_LOG_FUNCTION(this);
}

TorusIntraLevelTemplate::TorusIntraLevelTemplate(uint32_t levelId,
                                                 uint32_t dimId,
                                                 uint32_t nodeNum,
                                                 uint32_t subBlockNum,
                                                 std::string linkArrangement,
                                                 const LinkProfile& link)
    : IntraLevelTemplate(levelId, dimId, nodeNum, subBlockNum, link),
      m_linkArrangement(linkArrangement)
{
    NS_LOG_FUNCTION(this << nodeNum << subBlockNum);
}

TorusIntraLevelTemplate::~TorusIntraLevelTemplate()
{
    NS_LOG_FUNCTION(this);
}

/**
 * Connect each node in the top level of the topology to nodes in the top level of its neighboring
 * topologies
 */
void
TorusIntraLevelTemplate::Connect(StructuredTopology& topo, uint32_t levelId, uint32_t dimId)
{
    NS_LOG_FUNCTION(this << levelId << dimId);
    NS_ASSERT_MSG(levelId == m_levelId, "Connect: levelId mismatch");
    NS_ASSERT_MSG(dimId == m_dimId, "Connect: dimId mismatch");

    uint32_t groupNum = m_subBlockNum;
    uint32_t groupSize = topo.GetLevel(levelId).GetN() / groupNum;

    for (uint32_t cGroup = 0; cGroup < groupNum; ++cGroup)
    {
        for (uint32_t clocalIdx = 0; clocalIdx < groupSize; ++clocalIdx)
        {
            uint32_t cLocal = cGroup * groupSize + clocalIdx;
            Ptr<RuleBasedRouting> cRouting =
                DynamicCast<RuleBasedRouting>(topo.GetRoutingProtocolByLocal(levelId, cLocal));

            if (m_linkArrangement == "SameRank")
            {
                uint32_t rGroup = (cGroup + 1) % groupNum;
                uint32_t rLocal = rGroup * groupSize + clocalIdx;

                Ptr<RuleBasedRouting> rRouting =
                    DynamicCast<RuleBasedRouting>(topo.GetRoutingProtocolByLocal(levelId, rLocal));

                std::pair<uint32_t, uint32_t> devs =
                    m_topoHelper->ConnectNodes(topo, levelId, dimId, cLocal, levelId, rLocal);

                if (cRouting)
                {
                    cRouting->GetPortSet().AddSameLevel(dimId, positiveDirectionKey, devs.first);
                }
                if (rRouting)
                {
                    rRouting->GetPortSet().AddSameLevel(dimId, negativeDirectionKey, devs.second);
                }
            }
        }
    }
}

void
TorusIntraLevelTemplate::GenerateRoutingRules(uint32_t levelId, uint32_t dimId)
{
    NS_LOG_FUNCTION(this << levelId);

    RoutingRuleManager* ruleManager = RoutingRuleManager::GetInstance();

    GenerateSameLevelRules(levelId, dimId, ruleManager);

    ruleManager->FreezeLevel(levelId);
}

void
TorusIntraLevelTemplate::GenerateSameLevelRules(uint32_t levelId,
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
TorusIntraLevelTemplate::MatchDimension(const StructuredAddress& src,
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
TorusIntraLevelTemplate::SelectPortTheSameLevel(const PortSet& ports,
                                                const StructuredAddress& src,
                                                const StructuredAddress& dst,
                                                const RoutingContext& ctx)
{
    PortSelector selector(m_portSelectPolicy);
    int srcAddrBitValue = src[ctx.topo->GetLevelAddrBit(m_levelId) + m_dimId];
    int dstAddrBitValue = dst[ctx.topo->GetLevelAddrBit(m_levelId) + m_dimId];

    int dist_neg = srcAddrBitValue - dstAddrBitValue;
    if (dist_neg < 0)
    {
        dist_neg += m_subBlockNum;
    }
    int dist_pos = dstAddrBitValue - srcAddrBitValue;
    if (dist_pos < 0)
    {
        dist_pos += m_subBlockNum;
    }
    std::string sameLevelPortSetKey =
        (dist_pos <= dist_neg) ? positiveDirectionKey : negativeDirectionKey;

    const auto& sameLevelPorts = ports.GetAvailableSameLevel(m_dimId, sameLevelPortSetKey);
    if (sameLevelPorts.empty())
    {
        return std::nullopt;
    }
    return selector.Pick(sameLevelPorts, ctx);
}

} // namespace ns3
