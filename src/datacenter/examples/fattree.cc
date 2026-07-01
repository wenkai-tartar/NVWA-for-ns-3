/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/datacenter-module.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-address.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/rule-based-routing-helper.h"
#include "ns3/structured-address-directory.h"

#include <filesystem>
#include <iostream>
#include <string>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("FatTreeExample");

// Packet trace file
std::ofstream packetTraceFile;

void
PacketTxTrace(Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface)
{
    if (packetTraceFile.is_open())
    {
        packetTraceFile << Simulator::Now().GetSeconds() << "\tTX\t"
                        << "Interface: " << interface << "\t"
                        << "Packet Size: " << packet->GetSize() << " bytes\t"
                        << "IPv4: " << ipv4->GetAddress(interface, 0).GetLocal() << std::endl;
    }
}

void
PacketRxTrace(Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface)
{
    if (packetTraceFile.is_open())
    {
        packetTraceFile << Simulator::Now().GetSeconds() << "\tRX\t"
                        << "Interface: " << interface << "\t"
                        << "Packet Size: " << packet->GetSize() << " bytes\t"
                        << "IPv4: " << ipv4->GetAddress(interface, 0).GetLocal() << std::endl;
    }
}

void
PacketDropTrace(const Ipv4Header& header,
                Ptr<const Packet> packet,
                Ipv4L3Protocol::DropReason reason,
                Ptr<Ipv4> ipv4,
                uint32_t interface)
{
    if (packetTraceFile.is_open())
    {
        packetTraceFile << Simulator::Now().GetSeconds() << "\tDROP\t"
                        << "Interface: " << interface << "\t"
                        << "Reason: " << static_cast<int>(reason) << "\t"
                        << "Src: " << header.GetSource() << "\t"
                        << "Dst: " << header.GetDestination() << "\t"
                        << "Packet Size: " << packet->GetSize() << " bytes" << std::endl;
    }
}

void
RegisterTrace(Ptr<StructuredTopology> topo)
{
    const NodeContainer& allNodes = topo->GetAll();
    for (uint32_t i = 0; i < allNodes.GetN(); ++i)
    {
        Ptr<Node> node = allNodes.Get(i);
        Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();

        if (ipv4)
        {
            // Connect IPv4 packet traces
            ipv4->TraceConnectWithoutContext("Tx", MakeCallback(&PacketTxTrace));
            ipv4->TraceConnectWithoutContext("Rx", MakeCallback(&PacketRxTrace));
            ipv4->TraceConnectWithoutContext("Drop", MakeCallback(&PacketDropTrace));
        }
    }
}

int
main(int argc, char* argv[])
{
    // Topology parameters
    // uint32_t numCore = 4;
    // uint32_t numAgg = 2;         // Number of aggregation layer nodes connected to each Core
    // uint32_t numEdge = 2;        // Number of edge layer nodes connected to each Aggregation
    // uint32_t numHostPerEdge = 2; // Number of host nodes connected to each Edge

    // Configure logging
    LogComponentEnable("FatTreeExample", LOG_LEVEL_INFO);

    std::filesystem::create_directory("./src/datacenter/examples/traces");
    // Open packet trace file
    packetTraceFile.open("./src/datacenter/examples/traces/fattree_packet_trace.txt");
    if (!packetTraceFile.is_open())
    {
        NS_LOG_ERROR("Failed to open packet trace file!");
        return 1;
    }

    // Write trace file header
    packetTraceFile << "# Packet Trace" << std::endl;
    packetTraceFile << "# Time(s)\tEvent\tInterface\tDetails" << std::endl;
    packetTraceFile << "# ==========================================" << std::endl;

    std::shared_ptr<TopologyHelper> topoHelper = std::make_shared<TopologyHelper>();

    RuleBasedRoutingHelper ruleBasedRoutingHelper;
    topoHelper->GetInternetStack().SetRoutingHelper(ruleBasedRoutingHelper);

    // Ipv4GlobalRoutingHelper globalRoutingHelper;
    // topoHelper->GetInternetStack().SetRoutingHelper(globalRoutingHelper);

    TopologyBuilder builder;

    ns3::LevelTemplate::LinkProfile link(ns3::DataRate("10Gbps"), ns3::MicroSeconds(1));

    Ptr<ClosInterLevelTemplate> edgeLevel =
        CreateObject<ClosInterLevelTemplate>(1, 1, 1, 2, 1, link);
    edgeLevel->SetTopologyHelper(topoHelper);
    builder.AddLevel(edgeLevel);

    Ptr<ClosInterLevelTemplate> aggLevel =
        CreateObject<ClosInterLevelTemplate>(2, 1, 2, 2, 1, link);
    aggLevel->SetTopologyHelper(topoHelper);
    builder.AddLevel(aggLevel);

    Ptr<ClosInterLevelTemplate> coreLevel =
        CreateObject<ClosInterLevelTemplate>(3, 1, 4, 4, 2, link);
    coreLevel->SetTopologyHelper(topoHelper);
    builder.AddLevel(coreLevel);

    // Build topology
    Ptr<StructuredTopology> topo = CreateObject<StructuredTopology>(topoHelper);
    builder.Build(topo);

    // Register addresses
    topo->RegisterAddresses();

    // Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    ruleBasedRoutingHelper.Initialize(*topo);

    // Setup packet tracing for all nodes
    RegisterTrace(topo);

    Ptr<StructuredAddressDirectory> dir = StructuredAddressDirectory::Get();

    // Print topology information for visualization
    topo->PrintTopologyInfo();

    // Application deployment (example)
    // Deploy EchoClient/EchoServer between nodes in the topology
    const NodeContainer& allNodes = topo->GetAll();

    if (allNodes.GetN() < 2)
    {
        NS_LOG_ERROR("Not enough nodes to deploy applications.");
        return 1;
    }

    const NodeContainer& endPoints = topo->GetLevel(0);
    if (endPoints.GetN() < 2)
    {
        NS_LOG_ERROR("Not enough nodes to deploy applications.");
        return 1;
    }
    // Select two nodes as client and server (use the last two nodes which should be at edge/host
    // level)
    Ptr<Node> serverNode = endPoints.Get(endPoints.GetN() - 1); // First node as server
    Ptr<Node> clientNode = endPoints.Get(0);                    // Last node as client

    // Server application
    UdpEchoServerHelper echoServer(9); // Port 9
    ApplicationContainer serverApps = echoServer.Install(serverNode);
    serverApps.Start(Seconds(1.0));
    serverApps.Stop(Seconds(10.0));

    // Client application
    UdpEchoClientHelper echoClient(serverNode->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal(), 9);
    echoClient.SetAttribute("MaxPackets", UintegerValue(1));
    echoClient.SetAttribute("Interval", TimeValue(Seconds(1.0)));
    echoClient.SetAttribute("PacketSize", UintegerValue(1024));
    ApplicationContainer clientApps = echoClient.Install(clientNode);
    clientApps.Start(Seconds(2.0));
    clientApps.Stop(Seconds(10.0));

    // Run simulation
    Simulator::Run();
    Simulator::Destroy();

    // Close trace file
    if (packetTraceFile.is_open())
    {
        packetTraceFile.close();
        std::cout << "Packet trace saved to: "
                     "./src/datacenter/examples/traces/fattree_packet_trace.txt"
                  << std::endl;
    }

    return 0;
}
