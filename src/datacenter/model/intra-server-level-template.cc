/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#include "intra-server-level-template.h"

#include "rule-based-routing.h"
#include "routing-common.h"
#include "routing-rule.h"

#include "ns3/fatal-error.h"
#include "ns3/log.h"

#include <string>
#include <utility>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("IntraServerLevelTemplate");

NS_OBJECT_ENSURE_REGISTERED(IntraServerLevelTemplate);

TypeId
IntraServerLevelTemplate::GetTypeId()
{
    static TypeId tid = TypeId("ns3::IntraServerLevelTemplate")
                            .SetParent<LevelTemplate>()
                            .SetGroupName("Datacenter");
    return tid;
}

IntraServerLevelTemplate::IntraServerLevelTemplate()
{
    NS_LOG_FUNCTION(this);
}

IntraServerLevelTemplate::IntraServerLevelTemplate(uint32_t serverNum,
                                                   uint32_t endpointsPerServer,
                                                   std::string linkArrangement,
                                                   const LinkProfile& link)
    : LevelTemplate(0, 1, 0, serverNum * endpointsPerServer, link),
      m_serverNum(serverNum),
      m_endpointsPerServer(endpointsPerServer),
      m_linkArrangement(std::move(linkArrangement))
{
    NS_LOG_FUNCTION(this << serverNum << endpointsPerServer << m_linkArrangement);
}

IntraServerLevelTemplate::~IntraServerLevelTemplate()
{
    NS_LOG_FUNCTION(this);
}

std::pair<uint32_t, uint32_t>
IntraServerLevelTemplate::Build(StructuredTopology& topo)
{
    NS_LOG_FUNCTION(this);
    if (m_serverNum == 0)
    {
        NS_ABORT_MSG("IntraServerLevelTemplate: serverNum must be > 0");
    }
    if (m_endpointsPerServer == 0)
    {
        NS_ABORT_MSG("IntraServerLevelTemplate: endpointsPerServer must be > 0");
    }
    if (m_linkArrangement != "FullMesh" && m_linkArrangement != "Default")
    {
        NS_ABORT_MSG("IntraServerLevelTemplate only supports FullMesh linkArrangement for now");
    }

    const uint32_t totalHosts = m_serverNum * m_endpointsPerServer;
    topo.ReplicateTopologyInPlace(totalHosts);

    // Field 0 is endpoint-in-server, field 1 is server id. Later inter-level
    // templates may append higher-level fields such as target switch id.
    auto [levelId, dimId] = topo.CreateLevel(0);
    if (levelId != 0)
    {
        NS_ABORT_MSG("IntraServerLevelTemplate must operate on level 0");
    }
    m_levelId = levelId;
    m_dimId = dimId;

    for (uint32_t server = 0; server < m_serverNum; ++server)
    {
        for (uint32_t endpoint = 0; endpoint < m_endpointsPerServer; ++endpoint)
        {
            uint32_t local = server * m_endpointsPerServer + endpoint;
            topo.SetStructuredAddrByLocal(levelId,
                                          local,
                                          StructuredAddress{endpoint + 1, server + 1});
        }
    }

    Connect(topo, levelId, dimId);
    GenerateRoutingRules(levelId, dimId);
    return std::make_pair(levelId, dimId);
}

void
IntraServerLevelTemplate::Connect(StructuredTopology& topo, uint32_t levelId, uint32_t dimId)
{
    NS_LOG_FUNCTION(this << levelId << dimId);
    if (levelId != 0)
    {
        NS_ABORT_MSG("IntraServerLevelTemplate only supports level 0");
    }

    if (!m_topoHelper)
    {
        NS_ABORT_MSG("IntraServerLevelTemplate requires a TopologyHelper");
    }
    m_topoHelper->SetLinkAttributes(m_link.rate, m_link.delay, /*mtu=*/1500);

    for (uint32_t server = 0; server < m_serverNum; ++server)
    {
        uint32_t base = server * m_endpointsPerServer;
        for (uint32_t leftEndpoint = 0; leftEndpoint < m_endpointsPerServer; ++leftEndpoint)
        {
            uint32_t left = base + leftEndpoint;
            Ptr<RuleBasedRouting> leftRouting =
                DynamicCast<RuleBasedRouting>(topo.GetRoutingProtocolByLocal(levelId, left));

            for (uint32_t rightEndpoint = leftEndpoint + 1;
                 rightEndpoint < m_endpointsPerServer;
                 ++rightEndpoint)
            {
                uint32_t right = base + rightEndpoint;
                Ptr<RuleBasedRouting> rightRouting =
                    DynamicCast<RuleBasedRouting>(topo.GetRoutingProtocolByLocal(levelId, right));

                auto devs = m_topoHelper->ConnectNodes(topo, levelId, dimId, left, levelId, right);

                if (leftRouting)
                {
                    leftRouting->GetPortSet().AddSameLevel(dimId,
                                                           std::to_string(rightEndpoint + 1),
                                                           devs.first);
                }
                if (rightRouting)
                {
                    rightRouting->GetPortSet().AddSameLevel(dimId,
                                                            std::to_string(leftEndpoint + 1),
                                                            devs.second);
                }
            }
        }
    }
}

void
IntraServerLevelTemplate::GenerateRoutingRules(uint32_t levelId, uint32_t dimId)
{
    NS_LOG_FUNCTION(this << levelId << dimId);

    RoutingRuleManager* ruleManager = RoutingRuleManager::GetInstance();
    auto matchFunc = [this](const StructuredAddress& src,
                            const StructuredAddress& dst,
                            const RoutingContext& ctx) { return MatchSameServer(src, dst, ctx); };
    auto actionFunc = [this](const PortSet& ports,
                             const StructuredAddress& src,
                             const StructuredAddress& dst,
                             const RoutingContext& ctx) {
        return SelectIntraServerPort(ports, src, dst, ctx);
    };

    Ptr<RoutingRule> rule = CreateObject<RoutingRule>(matchFunc,
                                                      MatchCondition::MatchSuccess,
                                                      actionFunc,
                                                      5000,
                                                      m_portSelectPolicy != PortSelectPolicy::kFirst);
    rule->SetName("Level_0_intra_server_rule");
    ruleManager->AddRule(levelId, rule);
    ruleManager->FreezeLevel(levelId);
}

MatchCondition
IntraServerLevelTemplate::MatchSameServer(const StructuredAddress& src,
                                          const StructuredAddress& dst,
                                          const RoutingContext& ctx)
{
    (void)ctx;
    if (src.Size() < 2 || dst.Size() < 2)
    {
        return MatchCondition::MatchFailure;
    }
    if (src[1] == dst[1] && src[0] != dst[0])
    {
        return MatchCondition::MatchSuccess;
    }
    return MatchCondition::MatchFailure;
}

std::optional<uint32_t>
IntraServerLevelTemplate::SelectIntraServerPort(const PortSet& ports,
                                                const StructuredAddress& src,
                                                const StructuredAddress& dst,
                                                const RoutingContext& ctx)
{
    (void)src;
    if (dst.Empty())
    {
        return std::nullopt;
    }

    PortSelector selector(m_portSelectPolicy);
    const auto& sameLevelPorts = ports.GetAvailableSameLevel(m_dimId, std::to_string(dst[0]));
    if (sameLevelPorts.empty())
    {
        return std::nullopt;
    }
    return selector.Pick(sameLevelPorts, ctx);
}

} // namespace ns3
