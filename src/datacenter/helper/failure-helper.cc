/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#include "failure-helper.h"

#include "ns3/ipv4.h"
#include "ns3/json.hpp"
#include "ns3/log.h"
#include "ns3/point-to-point-channel.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/rule-based-routing.h"
#include "ns3/simulator.h"
#include "ns3/structured-topology.h"

#include <fstream>
#include <iostream>
#include <vector>

using json = nlohmann::json;

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("FailureHelper");

FailureHelper::FailureHelper()
{
    NS_LOG_FUNCTION(this);
}

FailureHelper::~FailureHelper()
{
    NS_LOG_FUNCTION(this);
}


Time
FailureHelper::ParseTime(double value, const std::string& unit)
{
    if (unit == "s")
    {
        return Seconds(value);
    }
    else if (unit == "ms")
    {
        return MilliSeconds(value);
    }
    else if (unit == "us")
    {
        return MicroSeconds(value);
    }
    else if (unit == "ns")
    {
        return NanoSeconds(value);
    }
    else
    {
        NS_FATAL_ERROR("Unknown time unit: " << unit);
        return Seconds(0); // unreachable
    }
}

void
FailureHelper::SetLinkDown(Ptr<StructuredTopology> topo,
                           uint32_t srcLevel,
                           uint32_t srcLocalIndex,
                           uint32_t dstLevel,
                           uint32_t dstLocalIndex)
{
    NS_LOG_FUNCTION(srcLevel << srcLocalIndex << dstLevel << dstLocalIndex);
    Ptr<Node> srcNode = topo->GetNodeByLocal(srcLevel, srcLocalIndex);
    Ptr<Node> dstNode = topo->GetNodeByLocal(dstLevel, dstLocalIndex);

    if (!srcNode || !dstNode)
    {
        NS_LOG_ERROR("Failed to find nodes: srcNode=" << srcNode << ", dstNode=" << dstNode);
        return;
    }

    NS_LOG_INFO("Setting link DOWN between Node " << srcNode->GetId() << " (level " << srcLevel
                                                   << ", local " << srcLocalIndex << ") and Node "
                                                   << dstNode->GetId() << " (level " << dstLevel
                                                   << ", local " << dstLocalIndex << ")");

    // Find the link from src to dst
    bool foundSrcToDst = false;
    for (uint32_t i = 0; i < srcNode->GetNDevices(); ++i)
    {
        Ptr<NetDevice> dev = srcNode->GetDevice(i);
        Ptr<PointToPointNetDevice> p2pDev = DynamicCast<PointToPointNetDevice>(dev);
        if (p2pDev)
        {
            Ptr<Channel> channel = p2pDev->GetChannel();
            Ptr<PointToPointChannel> p2pChannel = DynamicCast<PointToPointChannel>(channel);
            if (p2pChannel)
            {
                // Check if this channel connects to dstNode
                Ptr<NetDevice> dev0 = p2pChannel->GetDevice(0);
                Ptr<NetDevice> dev1 = p2pChannel->GetDevice(1);

                if ((dev0->GetNode() == srcNode && dev1->GetNode() == dstNode) ||
                    (dev0->GetNode() == dstNode && dev1->GetNode() == srcNode))
                {
                    // Found the link, set both interface directions down using Ipv4
                    Ptr<Node> node0 = dev0->GetNode();
                    Ptr<Node> node1 = dev1->GetNode();

                    Ptr<Ipv4> ipv4_0 = node0->GetObject<Ipv4>();
                    Ptr<Ipv4> ipv4_1 = node1->GetObject<Ipv4>();

                    if (ipv4_0)
                    {
                        int32_t ifIndex0 = ipv4_0->GetInterfaceForDevice(dev0);
                        if (ifIndex0 != -1)
                        {
                            ipv4_0->SetDown(ifIndex0);
                            NS_LOG_INFO("Set interface " << ifIndex0 << " DOWN on node "
                                                         << node0->GetId());

                            Ptr<RuleBasedRouting> routing0 =
                                DynamicCast<RuleBasedRouting>(ipv4_0->GetRoutingProtocol());
                            if (routing0)
                            {
                                // Guard against double notification if Ipv4::SetDown already triggers it.
                                if (!routing0->GetPortSet().IsPortDown(ifIndex0))
                                {
                                    routing0->NotifyInterfaceDown(ifIndex0);
                                }
                            }
                        }
                    }

                    if (ipv4_1)
                    {
                        int32_t ifIndex1 = ipv4_1->GetInterfaceForDevice(dev1);
                        if (ifIndex1 != -1)
                        {
                            ipv4_1->SetDown(ifIndex1);
                            NS_LOG_INFO("Set interface " << ifIndex1 << " DOWN on node "
                                                         << node1->GetId());

                            // Update PortSet for RuleBasedRouting
                            Ptr<RuleBasedRouting> routing1 =
                                DynamicCast<RuleBasedRouting>(ipv4_1->GetRoutingProtocol());
                            if (routing1)
                            {
                                if (!routing1->GetPortSet().IsPortDown(ifIndex1))
                                {
                                    routing1->NotifyInterfaceDown(ifIndex1);
                                }
                            }
                        }
                    }

                    foundSrcToDst = true;
                    NS_LOG_INFO("Link set to DOWN at time " << Simulator::Now().GetSeconds()
                                                            << "s");
                    break;
                }
            }
        }
    }

    if (!foundSrcToDst)
    {
        NS_LOG_WARN("Could not find link between nodes " << srcNode->GetId() << " and "
                                                          << dstNode->GetId());
    }
}

void
FailureHelper::SetLinkUp(Ptr<StructuredTopology> topo,
                         uint32_t srcLevel,
                         uint32_t srcLocalIndex,
                         uint32_t dstLevel,
                         uint32_t dstLocalIndex)
{
    NS_LOG_FUNCTION(srcLevel << srcLocalIndex << dstLevel << dstLocalIndex);

    Ptr<Node> srcNode = topo->GetNodeByLocal(srcLevel, srcLocalIndex);
    Ptr<Node> dstNode = topo->GetNodeByLocal(dstLevel, dstLocalIndex);

    if (!srcNode || !dstNode)
    {
        NS_LOG_ERROR("Failed to find nodes: srcNode=" << srcNode << ", dstNode=" << dstNode);
        return;
    }

    NS_LOG_INFO("Setting link UP between Node " << srcNode->GetId() << " (level " << srcLevel
                                                 << ", local " << srcLocalIndex << ") and Node "
                                                 << dstNode->GetId() << " (level " << dstLevel
                                                 << ", local " << dstLocalIndex << ")");

    // Find the link from src to dst
    bool foundSrcToDst = false;
    for (uint32_t i = 0; i < srcNode->GetNDevices(); ++i)
    {
        Ptr<NetDevice> dev = srcNode->GetDevice(i);
        Ptr<PointToPointNetDevice> p2pDev = DynamicCast<PointToPointNetDevice>(dev);
        if (p2pDev)
        {
            Ptr<Channel> channel = p2pDev->GetChannel();
            Ptr<PointToPointChannel> p2pChannel = DynamicCast<PointToPointChannel>(channel);
            if (p2pChannel)
            {
                // Check if this channel connects to dstNode
                Ptr<NetDevice> dev0 = p2pChannel->GetDevice(0);
                Ptr<NetDevice> dev1 = p2pChannel->GetDevice(1);

                if ((dev0->GetNode() == srcNode && dev1->GetNode() == dstNode) ||
                    (dev0->GetNode() == dstNode && dev1->GetNode() == srcNode))
                {
                    // Found the link, set both interface directions up using Ipv4
                    Ptr<Node> node0 = dev0->GetNode();
                    Ptr<Node> node1 = dev1->GetNode();

                    Ptr<Ipv4> ipv4_0 = node0->GetObject<Ipv4>();
                    Ptr<Ipv4> ipv4_1 = node1->GetObject<Ipv4>();

                    if (ipv4_0)
                    {
                        int32_t ifIndex0 = ipv4_0->GetInterfaceForDevice(dev0);
                        if (ifIndex0 != -1)
                        {
                            ipv4_0->SetUp(ifIndex0);
                            NS_LOG_INFO("Set interface " << ifIndex0 << " UP on node "
                                                         << node0->GetId());

                            // Update PortSet for RuleBasedRouting
                            Ptr<RuleBasedRouting> routing0 =
                                DynamicCast<RuleBasedRouting>(ipv4_0->GetRoutingProtocol());
                            if (routing0)
                            {
                                if (routing0->GetPortSet().IsPortDown(ifIndex0))
                                {
                                    routing0->NotifyInterfaceUp(ifIndex0);
                                }
                            }
                        }
                    }

                    if (ipv4_1)
                    {
                        int32_t ifIndex1 = ipv4_1->GetInterfaceForDevice(dev1);
                        if (ifIndex1 != -1)
                        {
                            ipv4_1->SetUp(ifIndex1);
                            NS_LOG_INFO("Set interface " << ifIndex1 << " UP on node "
                                                         << node1->GetId());

                            // Update PortSet for RuleBasedRouting
                            Ptr<RuleBasedRouting> routing1 =
                                DynamicCast<RuleBasedRouting>(ipv4_1->GetRoutingProtocol());
                            if (routing1)
                            {
                                if (routing1->GetPortSet().IsPortDown(ifIndex1))
                                {
                                    routing1->NotifyInterfaceUp(ifIndex1);
                                }
                            }
                        }
                    }

                    foundSrcToDst = true;
                    NS_LOG_INFO("Link set to UP at time " << Simulator::Now().GetSeconds() << "s");
                    break;
                }
            }
        }
    }

    if (!foundSrcToDst)
    {
        NS_LOG_WARN("Could not find link between nodes " << srcNode->GetId() << " and "
                                                          << dstNode->GetId());
    }
}

void
FailureHelper::LoadFailuresFromJson(const std::string& filename, Ptr<StructuredTopology> topo)
{
    NS_LOG_FUNCTION(this << filename);

    std::ifstream ifs(filename);
    if (!ifs.is_open())
    {
        NS_FATAL_ERROR("Cannot open failure config file: " << filename);
    }

    json cfg;
    ifs >> cfg;

    if (!cfg.contains("failures"))
    {
        NS_LOG_WARN("No 'failures' array found in " << filename);
        return;
    }

    const auto& failures = cfg["failures"];
    NS_LOG_INFO("Loading " << failures.size() << " failure events from " << filename);
    for (const auto& failure : failures)
    {
        // Parse link information
        const auto& link = failure["link"];
        uint32_t srcLevel = link["src"]["level"];
        uint32_t srcLocalIndex = link["src"]["local_index"];
        uint32_t dstLevel = link["dst"]["level"];
        uint32_t dstLocalIndex = link["dst"]["local_index"];

        // Parse failure time
        double failureTime = failure["failure_time"];
        std::string failureTimeUnit = failure["failure_time_unit"];
        Time failTime = ParseTime(failureTime, failureTimeUnit);

        // Schedule failure event
        Simulator::Schedule(failTime,
                            &FailureHelper::SetLinkDown,
                            topo,
                            srcLevel,
                            srcLocalIndex,
                            dstLevel,
                            dstLocalIndex);

        NS_LOG_INFO("Scheduled link failure at time " << failTime.GetSeconds() << "s: level "
                                                       << srcLevel << " local " << srcLocalIndex
                                                       << " <-> level " << dstLevel << " local "
                                                       << dstLocalIndex);

        // Parse optional recovery time
        if (failure.contains("recovery_time"))
        {
            double recoveryTime = failure["recovery_time"];
            std::string recoveryTimeUnit = failure["recovery_time_unit"];
            Time recTime = ParseTime(recoveryTime, recoveryTimeUnit);

            // Schedule recovery event
            Simulator::Schedule(recTime,
                                &FailureHelper::SetLinkUp,
                                topo,
                                srcLevel,
                                srcLocalIndex,
                                dstLevel,
                                dstLocalIndex);

            NS_LOG_INFO("Scheduled link recovery at time " << recTime.GetSeconds() << "s: level "
                                                            << srcLevel << " local "
                                                            << srcLocalIndex << " <-> level "
                                                            << dstLevel << " local "
                                                            << dstLocalIndex);
        }
    }
}

void
FailureHelper::ApplyFailuresFromJsonNow(const std::string& filename, Ptr<StructuredTopology> topo)
{
    NS_LOG_FUNCTION(filename);

    std::ifstream ifs(filename);
    if (!ifs.is_open())
    {
        NS_FATAL_ERROR("Cannot open failure config file: " << filename);
    }

    json cfg;
    ifs >> cfg;

    if (!cfg.contains("failures"))
    {
        NS_LOG_WARN("No 'failures' array found in " << filename);
        return;
    }

    const auto& failures = cfg["failures"];
    NS_LOG_INFO("Applying " << failures.size() << " failure events from " << filename);

    for (const auto& failure : failures)
    {
        const auto& link = failure["link"];
        uint32_t srcLevel = link["src"]["level"];
        uint32_t srcLocalIndex = link["src"]["local_index"];
        uint32_t dstLevel = link["dst"]["level"];
        uint32_t dstLocalIndex = link["dst"]["local_index"];

        FailureHelper::SetLinkDown(topo, srcLevel, srcLocalIndex, dstLevel, dstLocalIndex);
    }
}

} // namespace ns3
