/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Baseline example for TorusDetourRouting (for correctness checking vs RuleBased Detour policy).
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/datacenter-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/symmetric-degree-graph.h"

#include "ns3/torus-detour-routing.h"
#include "ns3/torus-detour-routing-helper.h"
#include "ns3/torus-intra-level-template.h"
#include "ns3/topology-builder.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("TorusDetourExample");

static std::ofstream packetTraceFile;

static bool
PeekIpv4(Ptr<const Packet> pkt, Ipv4Header& hdr)
{
    Packet copy = *pkt->Copy();
    return copy.PeekHeader(hdr);
}

static void
PacketTxTrace(Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface)
{
    if (!packetTraceFile.is_open())
    {
        return;
    }
    Ipv4Header header;
    if (PeekIpv4(packet, header))
    {
        packetTraceFile << Simulator::Now().GetSeconds() << "\tTX\t"
                        << "Interface: " << interface << "\t"
                        << "Packet Size: " << packet->GetSize() << " bytes\t"
                        << "Interface IP: " << ipv4->GetAddress(interface, 0).GetLocal() << "\t"
                        << "Src: " << header.GetSource() << "\t"
                        << "Dst: " << header.GetDestination() << std::endl;
    }
    else
    {
        packetTraceFile << Simulator::Now().GetSeconds() << "\tTX\t"
                        << "Interface: " << interface << "\t"
                        << "Packet Size: " << packet->GetSize() << " bytes\t"
                        << "Interface IP: " << ipv4->GetAddress(interface, 0).GetLocal()
                        << std::endl;
    }
}

static void
PacketRxTrace(Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface)
{
    if (!packetTraceFile.is_open())
    {
        return;
    }
    Ipv4Header header;
    if (PeekIpv4(packet, header))
    {
        packetTraceFile << Simulator::Now().GetSeconds() << "\tRX\t"
                        << "Interface: " << interface << "\t"
                        << "Packet Size: " << packet->GetSize() << " bytes\t"
                        << "Interface IP: " << ipv4->GetAddress(interface, 0).GetLocal() << "\t"
                        << "Src: " << header.GetSource() << "\t"
                        << "Dst: " << header.GetDestination() << std::endl;
    }
    else
    {
        packetTraceFile << Simulator::Now().GetSeconds() << "\tRX\t"
                        << "Interface: " << interface << "\t"
                        << "Packet Size: " << packet->GetSize() << " bytes\t"
                        << "Interface IP: " << ipv4->GetAddress(interface, 0).GetLocal()
                        << std::endl;
    }
}

static void
PacketDropTrace(const Ipv4Header& header,
                Ptr<const Packet> packet,
                Ipv4L3Protocol::DropReason reason,
                Ptr<Ipv4> ipv4,
                uint32_t interface)
{
    if (!packetTraceFile.is_open())
    {
        return;
    }
    packetTraceFile << Simulator::Now().GetSeconds() << "\tDROP\t"
                    << "Interface: " << interface << "\t"
                    << "Reason: " << static_cast<int>(reason) << "\t"
                    << "Src: " << header.GetSource() << "\t"
                    << "Dst: " << header.GetDestination() << "\t"
                    << "Packet Size: " << packet->GetSize() << " bytes" << std::endl;
}

static void
RegisterTrace(Ptr<StructuredTopology> topo)
{
    for (uint32_t lv = 0; lv < topo->GetNumLevels(); ++lv)
    {
        const NodeContainer& levelNodes = topo->GetLevel(lv);
        for (uint32_t i = 0; i < levelNodes.GetN(); ++i)
        {
            Ptr<Node> node = levelNodes.Get(i);
            Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
            if (!ipv4)
            {
                continue;
            }
            ipv4->TraceConnectWithoutContext("Tx", MakeCallback(&PacketTxTrace));
            ipv4->TraceConnectWithoutContext("Rx", MakeCallback(&PacketRxTrace));
            ipv4->TraceConnectWithoutContext("Drop", MakeCallback(&PacketDropTrace));
        }
    }
}

static std::vector<uint16_t>
ParseTransitFields(const std::string& s)
{
    std::vector<uint16_t> out;
    std::string tmp = s;
    for (char& c : tmp)
    {
        if (c == ',')
        {
            c = ' ';
        }
    }
    std::istringstream iss(tmp);
    int x = 0;
    while (iss >> x)
    {
        if (x < 0)
        {
            continue;
        }
        out.push_back(static_cast<uint16_t>(x));
    }
    return out;
}

static Ptr<TorusDetourRouting>
FindTorusDetour(Ptr<Ipv4RoutingProtocol> rp)
{
    if (!rp)
    {
        return nullptr;
    }
    if (auto tdr = DynamicCast<TorusDetourRouting>(rp))
    {
        return tdr;
    }
    if (auto list = DynamicCast<Ipv4ListRouting>(rp))
    {
        for (uint32_t i = 0; i < list->GetNRoutingProtocols(); ++i)
        {
            int16_t priority = 0;
            Ptr<Ipv4RoutingProtocol> sub = list->GetRoutingProtocol(i, priority);
            if (auto tdr = DynamicCast<TorusDetourRouting>(sub))
            {
                return tdr;
            }
        }
    }
    return nullptr;
}

int
main(int argc, char* argv[])
{
    CommandLine cmd;
    uint32_t d = 5;
    uint8_t detourStages = 1;
    std::string transitFields = "0,1,2";
    std::string rate = "100Gbps";
    std::string delay = "1us";
    std::string trafficPattern = "flows";
    uint32_t numFlows = 10;
    uint64_t flowSize = 262144;
    uint32_t degree = 4;
    uint64_t dataSize = 1048576;
    std::string dataRate = "100Gbps";
    bool debug = true;
    std::string tracePrefix = "torus_detour";

    cmd.AddValue("d", "3D torus dimension (each axis)", d);
    cmd.AddValue("detourStages", "Detour stages (1 or 2)", detourStages);
    cmd.AddValue("transitFields", "Transit fields, e.g. 0,1,2", transitFields);
    cmd.AddValue("rate", "Link data rate", rate);
    cmd.AddValue("delay", "Link delay", delay);
    cmd.AddValue("trafficPattern", "Traffic pattern: flows, allreduce, or alltoall", trafficPattern);
    cmd.AddValue("numFlows", "Number of flows (flows pattern)", numFlows);
    cmd.AddValue("flowSize", "Flow size in bytes (flows pattern)", flowSize);
    cmd.AddValue("degree", "Degree of the logical graph (allreduce)", degree);
    cmd.AddValue("dataSize", "Data size in bytes (allreduce)", dataSize);
    cmd.AddValue("dataRate", "Application data rate (allreduce)", dataRate);
    cmd.AddValue("debug", "Enable packet trace", debug);
    cmd.AddValue("tracePrefix", "Trace filename prefix", tracePrefix);
    cmd.Parse(argc, argv);

    if (trafficPattern == "allreduce")
    {
        degree = 4;
    }
    else if (trafficPattern == "alltoall")
    {
        degree = 8;
    }

    std::cout << "[torus_detour] start: d=" << d << " detourStages=" << unsigned(detourStages)
              << " trafficPattern=" << trafficPattern << " numFlows=" << numFlows << std::endl;

    auto topoHelper = std::make_shared<TopologyHelper>();
    topoHelper->SetLinkAttributes(rate, delay);

    // Install TorusDetourRouting on IPv4 stack (topo will be set after build).
    TorusDetourRoutingHelper tdrHelper;
    tdrHelper.SetTorusLevel(0);
    tdrHelper.SetDetourStages(detourStages);
    tdrHelper.SetTransitFields(ParseTransitFields(transitFields));
    topoHelper->GetInternetStack().SetRoutingHelper(tdrHelper);

    std::cout << "[torus_detour] creating StructuredTopology..." << std::endl;
    Ptr<StructuredTopology> topo = CreateObject<StructuredTopology>(topoHelper);
    {
        Ptr<Node> n0 = topo->GetNodeByLocal(0, 0);
        Ptr<Ipv4> ipv4 = n0 ? n0->GetObject<Ipv4>() : nullptr;
        std::cout << "[torus_detour] sanity: level0 nodes=" << topo->GetLevel(0).GetN()
                  << " ipv4@" << (ipv4 ? "yes" : "no") << std::endl;
    }

    // Debug hook: isolate whether replication alone crashes.
    if (std::getenv("NVWA_TORUS_DETOUR_REPL_ONLY"))
    {
        std::cout << "[torus_detour] NVWA_TORUS_DETOUR_REPL_ONLY=1 -> replicate only (copies="
                  << d << ")" << std::endl;
        topo->ReplicateTopologyInPlace(d);
        uint32_t missingIpv4 = 0;
        const NodeContainer& lvl0 = topo->GetLevel(0);
        for (uint32_t i = 0; i < lvl0.GetN(); ++i)
        {
            Ptr<Ipv4> ipv4 = lvl0.Get(i)->GetObject<Ipv4>();
            if (!ipv4)
            {
                ++missingIpv4;
            }
        }
        std::cout << "[torus_detour] after replicate: level0 nodes=" << lvl0.GetN()
                  << " missingIpv4=" << missingIpv4 << std::endl;
        return 0;
    }

    if (std::getenv("NVWA_TORUS_DETOUR_ADD_DIM_ONLY"))
    {
        std::cout << "[torus_detour] NVWA_TORUS_DETOUR_ADD_DIM_ONLY=1 -> replicate(copies=" << d
                  << ") + CreateLevel(0) (add dim)" << std::endl;
        topo->ReplicateTopologyInPlace(d);
        topo->CreateLevel(0);
        uint32_t missingIpv4 = 0;
        const NodeContainer& lvl0 = topo->GetLevel(0);
        for (uint32_t i = 0; i < lvl0.GetN(); ++i)
        {
            Ptr<Ipv4> ipv4 = lvl0.Get(i)->GetObject<Ipv4>();
            if (!ipv4)
            {
                ++missingIpv4;
            }
        }
        std::cout << "[torus_detour] after add-dim: level0 nodes=" << topo->GetLevel(0).GetN()
                  << " dimCount=" << topo->GetDimCount(0)
                  << " missingIpv4=" << missingIpv4 << std::endl;
        return 0;
    }

    if (std::getenv("NVWA_TORUS_DETOUR_CONNECT_ONLY"))
    {
        std::cout << "[torus_detour] NVWA_TORUS_DETOUR_CONNECT_ONLY=1 -> replicate(copies=" << d
                  << ") + add dim + connect (0 <-> 1) on dimId=1" << std::endl;
        topo->ReplicateTopologyInPlace(std::max<uint32_t>(d, 2));
        topo->CreateLevel(0); // add dim 1
        // Connect a simple pair to isolate ConnectNodes behavior.
        topoHelper->ConnectNodes(*topo, 0, 1, 0, 0, 1);
        topoHelper->ConnectNodes(*topo, 0, 1, 1, 0, 0);
        std::cout << "[torus_detour] connect ok" << std::endl;
        return 0;
    }

    // Build 3D torus using the same template sequence as the JSON constructor:
    // three TorusIntraLevelTemplate dims on level 0 (dimId = 1..3), each with subBlockNum = d.
    std::cout << "[torus_detour] building topology (templates x3)..." << std::endl;
    TopologyBuilder builder;
    LevelTemplate::LinkProfile lp{DataRate(rate), Time(delay)};
    Ptr<TorusIntraLevelTemplate> dimX =
        CreateObject<TorusIntraLevelTemplate>(0, 1, 0, d, "SameRank", lp);
    dimX->SetTopologyHelper(topoHelper);
    builder.AddLevel(dimX);

    Ptr<TorusIntraLevelTemplate> dimY =
        CreateObject<TorusIntraLevelTemplate>(0, 2, 0, d, "SameRank", lp);
    dimY->SetTopologyHelper(topoHelper);
    builder.AddLevel(dimY);

    Ptr<TorusIntraLevelTemplate> dimZ =
        CreateObject<TorusIntraLevelTemplate>(0, 3, 0, d, "SameRank", lp);
    dimZ->SetTopologyHelper(topoHelper);
    builder.AddLevel(dimZ);

    builder.Build(topo);
    const uint32_t n = topo->GetLevel(0).GetN();
    const uint32_t expect = d * d * d;
    std::cout << "[torus_detour] built torus: nodes=" << n << " expect=" << expect << std::endl;
    if (n != expect || d <= 1)
    {
        return 1;
    }
    std::cout << "[torus_detour] registering addresses..." << std::endl;
    topo->RegisterAddresses();

    // Patch topo pointer into TorusDetourRouting instances + set parameters.
    const auto fields = ParseTransitFields(transitFields);
    std::cout << "[torus_detour] patch routing topo pointers..." << std::endl;
    for (uint32_t lv = 0; lv < topo->GetNumLevels(); ++lv)
    {
        const NodeContainer& nodes = topo->GetLevel(lv);
        for (uint32_t i = 0; i < nodes.GetN(); ++i)
        {
            Ptr<Node> node = nodes.Get(i);
            Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
            if (!ipv4)
            {
                continue;
            }
            Ptr<TorusDetourRouting> tdr = FindTorusDetour(ipv4->GetRoutingProtocol());
            if (tdr)
            {
                tdr->SetTopology(PeekPointer(topo));
                tdr->SetTorusLevel(0);
                tdr->SetTransitFields(fields);
                tdr->SetDetourStages(detourStages);
            }
        }
    }

    if (debug)
    {
        std::filesystem::create_directories("src/datacenter/examples/traces");
        std::string packetTraceFilename =
            "src/datacenter/examples/traces/" + tracePrefix + "_packet_trace.txt";
        packetTraceFile.open(packetTraceFilename);
        packetTraceFile << "# Packet Trace" << std::endl;
        packetTraceFile << "# Time(s)\tEvent\tInterface\tDetails" << std::endl;
        packetTraceFile << "# ==========================================" << std::endl;
        RegisterTrace(topo);
    }

    std::cout << "[torus_detour] installing traffic..." << std::endl;
    // Traffic: match constructor's 'flows' defaults for determinism.
    const NodeContainer& hostNodes = topo->GetLevel(0);
    const uint32_t hostNum = hostNodes.GetN();

    auto install_graph_traffic = [&](uint32_t graph_degree) {
        SymmetricDegreeGraph logicalGraph(hostNum, graph_degree, 0, false, false, 1, 1);
        logicalGraph.GenerateGraph();

        uint16_t port = 9;

        for (uint32_t i = 0; i < hostNum; i++)
        {
            Ptr<Node> host = hostNodes.Get(i);
            Ptr<Ipv4> ipv4 = host->GetObject<Ipv4>();
            if (!ipv4)
            {
                continue;
            }

            std::vector<uint32_t> dsts = logicalGraph.GetDsts(i);
            Ipv4Address clientIp = ipv4->GetAddress(1, 0).GetLocal();

            for (auto& dstId : dsts)
            {
                if (i == dstId)
                {
                    continue;
                }

                Ptr<Node> dst = hostNodes.Get(dstId);
                Ptr<Ipv4> dstIpv4 = dst->GetObject<Ipv4>();
                if (!dstIpv4)
                {
                    continue;
                }

                OnOffHelper onoff("ns3::UdpSocketFactory",
                                  InetSocketAddress(dstIpv4->GetAddress(1, 0).GetLocal(), port));
                onoff.SetConstantRate(DataRate(dataRate));
                onoff.SetAttribute("PacketSize", UintegerValue(1000));
                onoff.SetAttribute("MaxBytes", UintegerValue(dataSize));
                onoff.SetAttribute("Local",
                                   AddressValue(AddressValue(InetSocketAddress(clientIp, 0))));

                ApplicationContainer sourceApps = onoff.Install(host);
                sourceApps.Start(Seconds(1.0));
                sourceApps.Stop(Seconds(5.0));
            }

            // Packet sink for allreduce/alltoall traffic
            PacketSinkHelper sink("ns3::UdpSocketFactory",
                                  Address(InetSocketAddress(Ipv4Address::GetAny(), port)));
            ApplicationContainer apps = sink.Install(host);
            apps.Start(Seconds(1.0));
            apps.Stop(Seconds(5.5));
        }
    };

    if (trafficPattern == "allreduce")
    {
        // ========== AllReduce Traffic Pattern ==========
        install_graph_traffic(degree);
    }
    else if (trafficPattern == "alltoall")
    {
        // ========== AllToAll Traffic Pattern ==========
        install_graph_traffic(degree);
    }
    else if (trafficPattern == "flows")
    {
        if (hostNum < 2)
        {
            return 1;
        }
        std::vector<Ipv4Address> hostAddresses;
        hostAddresses.reserve(hostNum);
        for (uint32_t i = 0; i < hostNum; ++i)
        {
            Ptr<Node> host = hostNodes.Get(i);
            Ipv4Address hostIp = host->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal();
            hostAddresses.push_back(hostIp);
        }
        double simTime = 5.0;
        uint32_t actualFlows = std::min(numFlows, hostNum);
        uint16_t basePort = 5000;

        for (uint32_t flow = 0; flow < actualFlows; ++flow)
        {
            uint32_t srcIndex = flow % hostNum;
            uint32_t dstIndex = (srcIndex + hostNum / 2 + flow) % hostNum;
            if (dstIndex == srcIndex)
            {
                dstIndex = (dstIndex + 1) % hostNum;
            }

            Address sinkAddress(InetSocketAddress(hostAddresses[dstIndex], basePort + flow));

            PacketSinkHelper sinkHelper("ns3::TcpSocketFactory", sinkAddress);
            ApplicationContainer sinkApp = sinkHelper.Install(hostNodes.Get(dstIndex));
            sinkApp.Start(Seconds(0.0));
            sinkApp.Stop(Seconds(simTime));

            BulkSendHelper sourceHelper("ns3::TcpSocketFactory", sinkAddress);
            sourceHelper.SetAttribute("MaxBytes", UintegerValue(flowSize));
            ApplicationContainer sourceApp = sourceHelper.Install(hostNodes.Get(srcIndex));
            sourceApp.Start(Seconds(0.1 + 0.05 * flow));
            sourceApp.Stop(Seconds(simTime));
        }
    }
    else
    {
        return 1;
    }

    Simulator::Run();
    Simulator::Destroy();

    if (debug && packetTraceFile.is_open())
    {
        packetTraceFile.close();
    }
    return 0;
}
