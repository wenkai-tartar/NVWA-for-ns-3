/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#include "clos-inter-level-template.h"

#include "inter-level-template.h"
#include "rule-based-routing.h"
#include "structured-address.h"

#include "ns3/structured-address-helper.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("ClosInterLevelTemplate");

NS_OBJECT_ENSURE_REGISTERED(ClosInterLevelTemplate);

TypeId
ClosInterLevelTemplate::GetTypeId()
{
    static TypeId tid = TypeId("ns3::ClosInterLevelTemplate")
                            .SetParent<InterLevelTemplate>()
                            .SetGroupName("Datacenter");

    return tid;
}

ClosInterLevelTemplate::ClosInterLevelTemplate()
{
    NS_LOG_FUNCTION(this);

    m_topoHelper->SetLinkAttributes("10Gbps", "1us");
}

ClosInterLevelTemplate::ClosInterLevelTemplate(uint32_t levelId,
                                               uint32_t dimId,
                                               uint32_t nodeNum,
                                               uint32_t subBlockNum,
                                               uint32_t groupNum,
                                               const LinkProfile& link)
    : InterLevelTemplate(levelId, dimId, nodeNum, subBlockNum, link),
      m_groupNum(groupNum)
{
    NS_LOG_FUNCTION(this << nodeNum << subBlockNum);
}

ClosInterLevelTemplate::ClosInterLevelTemplate(uint32_t levelId,
                                               uint32_t dimId,
                                               uint32_t nodeNum,
                                               uint32_t subBlockNum,
                                               uint32_t groupNum,
                                               std::string linkArrangement,
                                               uint32_t endpointsPerServer,
                                               uint32_t nicsPerAswitch,
                                               const LinkProfile& link)
    : InterLevelTemplate(levelId, dimId, nodeNum, subBlockNum, link),
      m_groupNum(groupNum),
      m_linkArrangement(std::move(linkArrangement)),
      m_endpointsPerServer(endpointsPerServer),
      m_nicsPerAswitch(nicsPerAswitch)
{
    NS_LOG_FUNCTION(this << nodeNum << subBlockNum << m_linkArrangement);
}

ClosInterLevelTemplate::~ClosInterLevelTemplate()
{
    NS_LOG_FUNCTION(this);
}

/**
 * Connect each upper-level node to every node of its group in the sub-level
 */
void
ClosInterLevelTemplate::Connect(StructuredTopology& topo, uint32_t levelId, uint32_t dimId)
{
    NS_ASSERT_MSG(levelId > 0, "Connect: need a child layer");
    NS_ASSERT_MSG(levelId == m_levelId, "Connect: levelId mismatch");
    NS_ASSERT_MSG(dimId == m_dimId, "Connect: dimId mismatch");

    if (m_linkArrangement == "RailOptimized")
    {
        ConnectRailOptimized(topo, levelId, dimId);
        return;
    }
    if (m_linkArrangement != "Contiguous" && m_linkArrangement != "Default")
    {
        NS_ABORT_MSG("Unknown ClosInterLevel linkArrangement: " << m_linkArrangement);
    }

    uint32_t childLevelId = levelId - 1;

    uint32_t parentCount = topo.GetLevel(levelId).GetN();
    uint32_t childCount = topo.GetLevel(childLevelId).GetN();

    if (parentCount == 0 || childCount == 0)
    {
        return;
    }

    NS_ASSERT_MSG(parentCount % m_groupNum == 0,
                  "Connect: parentCount must be divisible by m_groupNum");
    uint32_t parentGroupCount = parentCount / m_groupNum;
    NS_ASSERT_MSG(childCount % m_subBlockNum == 0,
                  "Connect: childCount must be divisible by m_subBlockNum");
    uint32_t childBlockCount = childCount / m_subBlockNum;
    NS_ASSERT_MSG(childBlockCount % m_groupNum == 0,
                  "Connect: childBlockCount must be divisible by m_groupNum");
    uint32_t childGroupCount = childBlockCount / m_groupNum;

    // Each node in parent layer connects to every node of its group in child layer
    for (uint32_t pGroup = 0; pGroup < m_groupNum; ++pGroup)
    {
        for (uint32_t pLocalInGroup = 0; pLocalInGroup < parentGroupCount; ++pLocalInGroup)
        {
            uint32_t pLocal = pGroup * parentGroupCount + pLocalInGroup;
            const StructuredAddress& cAddr = topo.GetStructuredAddrByLocal(childLevelId, pGroup);
            StructuredAddress newAddr;
            newAddr.Append(pLocalInGroup);
            int levelAddrBitForAddress = topo.GetLevelAddrBit(levelId);
            for (int i = 0; i < levelAddrBitForAddress &&
                            i < static_cast<int>(cAddr.Size());
                 ++i)
            {
                newAddr.Append(cAddr[static_cast<size_t>(i)]);
            }
            topo.SetStructuredAddrByLocal(levelId, pLocal, newAddr);

            Ptr<RuleBasedRouting> pRouting =
                DynamicCast<RuleBasedRouting>(topo.GetRoutingProtocolByLocal(levelId, pLocal));

            for (uint32_t cBlock = 0; cBlock < m_subBlockNum; ++cBlock)
            {
                for (uint32_t cNode = 0; cNode < childGroupCount; ++cNode)
                {
                    uint32_t cLocal = cBlock * childBlockCount + pGroup * childGroupCount + cNode;

                    Ptr<RuleBasedRouting> cRouting = DynamicCast<RuleBasedRouting>(
                        topo.GetRoutingProtocolByLocal(childLevelId, cLocal));

                    std::pair<uint32_t, uint32_t> devs = m_topoHelper->ConnectNodes(topo,
                                                                                    levelId,
                                                                                    dimId,
                                                                                    pLocal,
                                                                                    childLevelId,
                                                                                    cLocal);

                    int childLevelAddrBit = topo.GetLevelAddrBit(childLevelId);
                    int levelAddrBit = topo.GetLevelAddrBit(levelId);

                    StructuredAddress cAddr = topo.GetStructuredAddrByLocal(childLevelId, cLocal);
                    std::string downwardPortSetKey =
                        cAddr.ToStringRange(childLevelAddrBit + 1, levelAddrBit, '.', false);

                    // devs.Get(0) is the parent port, devs.Get(1) is the child port
                    if (pRouting)
                    {
                        pRouting->GetPortSet().AddDownward(downwardPortSetKey, devs.first);
                    }
                    if (cRouting)
                    {
                        // PortSetKey "" means ports to the whole parent layer nodes
                        cRouting->GetPortSet().AddUpward("", devs.second);
                    }
                }
            }
        }
    }
}

void
ClosInterLevelTemplate::ConnectRailOptimized(StructuredTopology& topo,
                                              uint32_t levelId,
                                              uint32_t dimId)
{
    NS_LOG_FUNCTION(this << levelId << dimId);
    if (levelId == 0)
    {
        NS_ABORT_MSG("ConnectRailOptimized: need a child layer");
    }
    if (levelId != m_levelId)
    {
        NS_ABORT_MSG("ConnectRailOptimized: levelId mismatch");
    }
    if (dimId != m_dimId)
    {
        NS_ABORT_MSG("ConnectRailOptimized: dimId mismatch");
    }
    if (m_endpointsPerServer == 0)
    {
        NS_ABORT_MSG("RailOptimized Clos requires endpointsPerServer > 0");
    }
    if (m_nicsPerAswitch == 0)
    {
        NS_ABORT_MSG("RailOptimized Clos requires nicsPerAswitch > 0");
    }

    uint32_t childLevelId = levelId - 1;
    if (childLevelId != 0)
    {
        NS_ABORT_MSG("RailOptimized Clos is intended for the endpoint-to-ASW hop");
    }

    uint32_t parentCount = topo.GetLevel(levelId).GetN();
    uint32_t childCount = topo.GetLevel(childLevelId).GetN();
    if (parentCount == 0 || childCount == 0)
    {
        NS_ABORT_MSG("RailOptimized Clos requires non-empty parent and child levels");
    }

    if (childCount % m_endpointsPerServer != 0)
    {
        NS_ABORT_MSG("RailOptimized Clos: child count must be divisible by endpointsPerServer");
    }
    uint32_t serverCount = childCount / m_endpointsPerServer;
    uint32_t segmentCount = (serverCount + m_nicsPerAswitch - 1) / m_nicsPerAswitch;
    if (parentCount != segmentCount * m_endpointsPerServer)
    {
        NS_ABORT_MSG("RailOptimized Clos: parent ASW count must equal ceil(servers/nicsPerAswitch) * endpointsPerServer");
    }

    int childLevelAddrBit = topo.GetLevelAddrBit(childLevelId);
    int levelAddrBit = topo.GetLevelAddrBit(levelId);

    for (uint32_t segment = 0; segment < segmentCount; ++segment)
    {
        uint32_t firstServer = segment * m_nicsPerAswitch;
        uint32_t lastServer = std::min(serverCount, firstServer + m_nicsPerAswitch);
        for (uint32_t rail = 0; rail < m_endpointsPerServer; ++rail)
        {
            uint32_t pLocal = segment * m_endpointsPerServer + rail;
            topo.SetStructuredAddrByLocal(levelId,
                                          pLocal,
                                          StructuredAddress{rail + 1, segment + 1, pLocal + 1});

            Ptr<RuleBasedRouting> pRouting =
                DynamicCast<RuleBasedRouting>(topo.GetRoutingProtocolByLocal(levelId, pLocal));

            for (uint32_t server = firstServer; server < lastServer; ++server)
            {
                uint32_t cLocal = server * m_endpointsPerServer + rail;
                topo.SetStructuredAddrByLocal(childLevelId,
                                              cLocal,
                                              StructuredAddress{rail + 1, server + 1, pLocal + 1});

                Ptr<RuleBasedRouting> cRouting = DynamicCast<RuleBasedRouting>(
                    topo.GetRoutingProtocolByLocal(childLevelId, cLocal));

                std::pair<uint32_t, uint32_t> devs = m_topoHelper->ConnectNodes(topo,
                                                                                levelId,
                                                                                dimId,
                                                                                pLocal,
                                                                                childLevelId,
                                                                                cLocal);

                StructuredAddress cAddr = topo.GetStructuredAddrByLocal(childLevelId, cLocal);
                std::string downwardPortSetKey =
                    cAddr.ToStringRange(childLevelAddrBit + 1, levelAddrBit, '.', false);

                if (pRouting)
                {
                    pRouting->GetPortSet().AddDownward(downwardPortSetKey, devs.first);
                }
                if (cRouting)
                {
                    cRouting->GetPortSet().AddUpward("", devs.second);
                }
            }
        }
    }
}

void
ClosInterLevelTemplate::GenerateRoutingRules(uint32_t levelId, uint32_t dimId)
{
    NS_LOG_FUNCTION(this << levelId);

    RoutingRuleManager* ruleManager = RoutingRuleManager::GetInstance();

    // Generate routing rules for parent level (aggregation layer)
    GenerateParentLevelRules(levelId, ruleManager);

    // Generate routing rules for child level (copied sub-topology)
    if (levelId > 0)
    {
        uint32_t childLevelId = levelId - 1;
        GenerateChildLevelRules(childLevelId, ruleManager);
    }

    // Freeze all levels' rules
    ruleManager->FreezeLevel(levelId);
    if (levelId > 0)
    {
        ruleManager->FreezeLevel(levelId - 1);
    }
}

void
ClosInterLevelTemplate::GenerateParentLevelRules(uint32_t levelId, RoutingRuleManager* ruleManager)
{
    NS_LOG_FUNCTION(this << levelId);

    auto matchFunc = [this](const StructuredAddress& src,
                            const StructuredAddress& dst,
                            const RoutingContext& ctx) { return MatchPrefix(src, dst, ctx); };
    auto actionFunc = [this](const PortSet& ports,
                             const StructuredAddress& src,
                             const StructuredAddress& dst,
                             const RoutingContext& ctx) {
        return SelectPortDownward(ports, src, dst, ctx);
    };

    MatchCondition matchCondition = MatchCondition::MatchSuccess;

    Ptr<RoutingRule> rule = CreateObject<RoutingRule>(matchFunc,
                                            matchCondition,
                                            actionFunc,
                                            0,
                                            m_portSelectPolicy != PortSelectPolicy::kFirst);
    rule->SetName("Level_" + std::to_string(levelId) + "_downward_rule");

    ruleManager->AddRule(levelId, rule);
}

void
ClosInterLevelTemplate::GenerateChildLevelRules(uint32_t levelId, RoutingRuleManager* ruleManager)
{
    NS_LOG_FUNCTION(this << levelId);

    auto matchFunc = [this](const StructuredAddress& src,
                            const StructuredAddress& dst,
                            const RoutingContext& ctx) { return MatchPrefix(src, dst, ctx); };
    auto actionFunc = [this](const PortSet& ports,
                             const StructuredAddress& src,
                             const StructuredAddress& dst,
                             const RoutingContext& ctx) {
        return SelectPortUpward(ports, src, dst, ctx);
    };

    MatchCondition matchCondition = MatchCondition::MatchFailure;

    Ptr<RoutingRule> rule = CreateObject<RoutingRule>(matchFunc,
                                            matchCondition,
                                            actionFunc,
                                            1000,
                                            m_portSelectPolicy != PortSelectPolicy::kFirst);
    rule->SetName("Level_" + std::to_string(levelId) + "_upward_rule");
    ruleManager->AddRule(levelId, rule);
}

MatchCondition
ClosInterLevelTemplate::MatchPrefix(const StructuredAddress& src,
                                    const StructuredAddress& dst,
                                    const RoutingContext& ctx)
{
    NS_LOG_FUNCTION(this << src << dst);
    int levelAddrBit = ctx.topo->GetLevelAddrBit(ctx.levelId);
    if (StructuredAddressHelper::ComparePrefix(src, dst, levelAddrBit + 1))
    {
        return MatchCondition::MatchSuccess;
    }
    else
    {
        return MatchCondition::MatchFailure;
    }
}

std::optional<uint32_t>
ClosInterLevelTemplate::SelectPortUpward(const PortSet& ports,
                                         const StructuredAddress& src,
                                         const StructuredAddress& dst,
                                         const RoutingContext& ctx)
{
    PortSelector selector(m_portSelectPolicy);
    const auto& upwardPorts = ports.GetAvailableUpward("");
    if (upwardPorts.empty())
    {
        return std::nullopt;
    }
    return selector.Pick(upwardPorts, ctx);
}

std::optional<uint32_t>
ClosInterLevelTemplate::SelectPortDownward(const PortSet& ports,
                                           const StructuredAddress& src,
                                           const StructuredAddress& dst,
                                           const RoutingContext& ctx)
{
    PortSelector selector(m_portSelectPolicy);

    int levelAddrBit = ctx.topo->GetLevelAddrBit(ctx.levelId);
    int childLevelAddrBit = ctx.topo->GetLevelAddrBit(ctx.levelId - 1);

    const std::vector<uint32_t>* downwardPortsPtr = nullptr;
    if (childLevelAddrBit + 1 == levelAddrBit &&
        levelAddrBit >= 0 &&
        static_cast<size_t>(levelAddrBit) < dst.Size())
    {
        downwardPortsPtr = &ports.GetAvailableDownward(dst[static_cast<size_t>(levelAddrBit)]);
    }

    if (!downwardPortsPtr)
    {
        downwardPortsPtr = &ports.GetAvailableDownward(
            dst.ToStringRange(childLevelAddrBit + 1, levelAddrBit, '.', false));
    }
    const auto& downwardPorts = *downwardPortsPtr;

    if (downwardPorts.empty())
    {
        return std::nullopt;
    }
    return selector.Pick(downwardPorts, ctx);
}

} // namespace ns3
