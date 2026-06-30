/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "mesh-intra-level-template.h"

#include "rule-based-routing.h"
#include "structured-address.h"

#include "ns3/structured-address-helper.h"

#include <cstdint>

namespace ns3
{

namespace
{
const std::string kPositiveDirectionKey = "1";
const std::string kNegativeDirectionKey = "0";
} // namespace

NS_LOG_COMPONENT_DEFINE("MeshIntraLevelTemplate");

NS_OBJECT_ENSURE_REGISTERED(MeshIntraLevelTemplate);

TypeId
MeshIntraLevelTemplate::GetTypeId()
{
    static TypeId tid = TypeId("ns3::MeshIntraLevelTemplate")
                            .SetParent<IntraLevelTemplate>()
                            .SetGroupName("Datacenter");
    return tid;
}

MeshIntraLevelTemplate::MeshIntraLevelTemplate()
{
    NS_LOG_FUNCTION(this);
}

MeshIntraLevelTemplate::MeshIntraLevelTemplate(uint32_t levelId,
                                               uint32_t dimId,
                                               uint32_t nodeNum,
                                               uint32_t subBlockNum,
                                               std::string linkArrangement,
                                               const LinkProfile& link)
    : IntraLevelTemplate(levelId, dimId, nodeNum, subBlockNum, link),
      m_linkArrangement(std::move(linkArrangement))
{
    NS_LOG_FUNCTION(this << nodeNum << subBlockNum);
}

MeshIntraLevelTemplate::~MeshIntraLevelTemplate()
{
    NS_LOG_FUNCTION(this);
}

void
MeshIntraLevelTemplate::Connect(StructuredTopology& topo, uint32_t levelId, uint32_t dimId)
{
    NS_LOG_FUNCTION(this << levelId << dimId);
    NS_ASSERT_MSG(levelId == m_levelId, "Connect: levelId mismatch");
    NS_ASSERT_MSG(dimId == m_dimId, "Connect: dimId mismatch");

    if (m_linkArrangement != "SameRank" && m_linkArrangement != "Default")
    {
        NS_ABORT_MSG("MeshIntraLevelTemplate only supports SameRank linkArrangement");
    }

    uint32_t groupNum = m_subBlockNum;
    uint32_t groupSize = topo.GetLevel(levelId).GetN() / groupNum;

    for (uint32_t cGroup = 0; cGroup + 1 < groupNum; ++cGroup)
    {
        uint32_t rGroup = cGroup + 1;
        for (uint32_t clocalIdx = 0; clocalIdx < groupSize; ++clocalIdx)
        {
            uint32_t cLocal = cGroup * groupSize + clocalIdx;
            uint32_t rLocal = rGroup * groupSize + clocalIdx;

            Ptr<RuleBasedRouting> cRouting =
                DynamicCast<RuleBasedRouting>(topo.GetRoutingProtocolByLocal(levelId, cLocal));
            Ptr<RuleBasedRouting> rRouting =
                DynamicCast<RuleBasedRouting>(topo.GetRoutingProtocolByLocal(levelId, rLocal));

            std::pair<uint32_t, uint32_t> devs =
                m_topoHelper->ConnectNodes(topo, levelId, dimId, cLocal, levelId, rLocal);

            if (cRouting)
            {
                cRouting->GetPortSet().AddSameLevel(dimId, kPositiveDirectionKey, devs.first);
            }
            if (rRouting)
            {
                rRouting->GetPortSet().AddSameLevel(dimId, kNegativeDirectionKey, devs.second);
            }
        }
    }
}

void
MeshIntraLevelTemplate::GenerateRoutingRules(uint32_t levelId, uint32_t dimId)
{
    NS_LOG_FUNCTION(this << levelId);

    RoutingRuleManager* ruleManager = RoutingRuleManager::GetInstance();
    GenerateSameLevelRules(levelId, dimId, ruleManager);
    ruleManager->FreezeLevel(levelId);
}

void
MeshIntraLevelTemplate::GenerateSameLevelRules(uint32_t levelId,
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

    Ptr<RoutingRule> rule =
        CreateObject<RoutingRule>(matchFunc,
                                  MatchCondition::MatchFailure,
                                  actionFunc,
                                  dimId * 100,
                                  m_portSelectPolicy != PortSelectPolicy::kFirst);
    rule->SetName("Level_" + std::to_string(levelId) + "_dim_" + std::to_string(dimId) +
                  "_mesh_same_level_rule");
    ruleManager->AddRule(levelId, rule);
}

MatchCondition
MeshIntraLevelTemplate::MatchDimension(const StructuredAddress& src,
                                       const StructuredAddress& dst,
                                       const RoutingContext& ctx)
{
    int dimAddrBit = ctx.topo->GetLevelAddrBit(m_levelId) + m_dimId;
    NS_ASSERT_MSG(dimAddrBit >= 0 && dimAddrBit < static_cast<int>(src.Size()),
                  "MatchDimension: dimAddrBit out of range");

    return StructuredAddressHelper::CompareField(src, dst, dimAddrBit)
               ? MatchCondition::MatchSuccess
               : MatchCondition::MatchFailure;
}

std::optional<uint32_t>
MeshIntraLevelTemplate::SelectPortTheSameLevel(const PortSet& ports,
                                               const StructuredAddress& src,
                                               const StructuredAddress& dst,
                                               const RoutingContext& ctx)
{
    PortSelector selector(m_portSelectPolicy);
    int dimAddrBit = ctx.topo->GetLevelAddrBit(m_levelId) + m_dimId;
    int srcAddrBitValue = src[dimAddrBit];
    int dstAddrBitValue = dst[dimAddrBit];

    std::string sameLevelPortSetKey =
        (dstAddrBitValue > srcAddrBitValue) ? kPositiveDirectionKey : kNegativeDirectionKey;

    const auto& sameLevelPorts = ports.GetAvailableSameLevel(m_dimId, sameLevelPortSetKey);
    if (sameLevelPorts.empty())
    {
        return std::nullopt;
    }
    return selector.Pick(sameLevelPorts, ctx);
}

} // namespace ns3
