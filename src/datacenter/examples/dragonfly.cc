/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/datacenter-module.h"
#include "ns3/congestion-signal-provider.h"
#include "ns3/dragonfly-ugal-routing-helper.h"
#include "ns3/dragonfly-valiant-routing-helper.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-list-routing.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <regex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("DragonflyExample");

// Packet/flow trace files
std::ofstream packetTraceFile;
std::ofstream flowTraceFile;
std::ofstream routeTraceFile;

std::map<uint32_t, double> flowStartTimes;
std::map<uint32_t, uint32_t> flowSrcNodes;
std::map<uint32_t, uint32_t> flowDstNodes;
std::map<uint32_t, uint64_t> flowReceivedBytes;
std::map<uint32_t, uint64_t> flowActualSentBytes;

static std::unordered_map<uint32_t, std::string>
BuildIpToStructuredMap(Ptr<StructuredTopology> topo)
{
    std::unordered_map<uint32_t, std::string> map;
    if (!topo)
    {
        return map;
    }
    for (uint32_t lv = 0; lv < topo->GetNumLevels(); ++lv)
    {
        const NodeContainer& nodes = topo->GetLevel(lv);
        for (uint32_t i = 0; i < nodes.GetN(); ++i)
        {
            Ptr<Node> node = nodes.Get(i);
            if (!node)
            {
                continue;
            }
            StructuredAddress addr = topo->GetStructuredAddrByLocal(lv, i);
            Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
            if (!ipv4)
            {
                continue;
            }
            for (uint32_t ifIdx = 0; ifIdx < ipv4->GetNInterfaces(); ++ifIdx)
            {
                const uint32_t nAddr = ipv4->GetNAddresses(ifIdx);
                for (uint32_t addrIdx = 0; addrIdx < nAddr; ++addrIdx)
                {
                    Ipv4Address ip = ipv4->GetAddress(ifIdx, addrIdx).GetLocal();
                    map[ip.Get()] = addr.ToString();
                }
            }
        }
    }
    return map;
}

static std::string
ReplaceIpsWithStructured(const std::string& line,
                         const std::unordered_map<uint32_t, std::string>& map)
{
    static const std::regex ipRe(R"((\d{1,3}(?:\.\d{1,3}){3}))");
    std::string out;
    out.reserve(line.size());
    size_t last = 0;
    for (auto it = std::sregex_iterator(line.begin(), line.end(), ipRe);
         it != std::sregex_iterator();
         ++it)
    {
        const size_t pos = static_cast<size_t>(it->position());
        const size_t len = static_cast<size_t>(it->length());
        out.append(line.substr(last, pos - last));
        const std::string ipStr = it->str();
        Ipv4Address ip(ipStr.c_str());
        auto mit = map.find(ip.Get());
        if (mit != map.end())
        {
            out.append(mit->second);
        }
        else
        {
            out.append(ipStr);
        }
        last = pos + len;
    }
    out.append(line.substr(last));
    return out;
}

static void
RewriteRoutesFileWithStructured(const std::string& path,
                                const std::unordered_map<uint32_t, std::string>& map)
{
    std::ifstream in(path);
    if (!in.is_open())
    {
        return;
    }
    const std::string tmpPath = path + ".tmp";
    std::ofstream out(tmpPath);
    if (!out.is_open())
    {
        return;
    }
    std::string line;
    while (std::getline(in, line))
    {
        out << ReplaceIpsWithStructured(line, map) << std::endl;
    }
    in.close();
    out.close();
    std::error_code ec;
    std::filesystem::rename(tmpPath, path, ec);
    if (ec)
    {
        std::filesystem::remove(tmpPath);
    }
}

struct FlowKey
{
    Ipv4Address src;
    Ipv4Address dst;

    bool operator==(const FlowKey& o) const
    {
        return src == o.src && dst == o.dst;
    }
};

struct FlowKeyHasher
{
    std::size_t operator()(const FlowKey& k) const noexcept
    {
        return std::hash<uint32_t>()(k.src.Get()) ^ (std::hash<uint32_t>()(k.dst.Get()) << 1);
    }
};

struct FlowStats
{
    uint64_t txPackets = 0;
    uint64_t rxPackets = 0;
    uint64_t txBytes = 0;
    uint64_t rxBytes = 0;
    uint64_t drops = 0;

    bool sawFirstRx = false;
    Time firstRx = Seconds(0);
    Time lastRx = Seconds(0);
};

static std::unordered_map<FlowKey, FlowStats, FlowKeyHasher> g_flowStats;

static bool
PeekIpv4(Ptr<const Packet> pkt, Ipv4Header& hdr)
{
    Packet copy = *pkt->Copy();
    return copy.PeekHeader(hdr);
}

void
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

void
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
PacketTxFlowTrace(Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface)
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

        FlowKey key{header.GetSource(), header.GetDestination()};
        auto& st = g_flowStats[key];
        st.txPackets += 1;
        st.txBytes += packet->GetSize();
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

void
PacketRxFlowTrace(Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface)
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

        FlowKey key{header.GetSource(), header.GetDestination()};
        auto& st = g_flowStats[key];
        st.rxPackets += 1;
        st.rxBytes += packet->GetSize();
        if (!st.sawFirstRx)
        {
            st.sawFirstRx = true;
            st.firstRx = Simulator::Now();
        }
        st.lastRx = Simulator::Now();
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

void
PacketDropFlowTrace(const Ipv4Header& header,
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

    FlowKey key{header.GetSource(), header.GetDestination()};
    auto& st = g_flowStats[key];
    st.drops += 1;
}

void
FlowStopTrace(uint32_t flowId, uint64_t configuredBytes)
{
    if (flowTraceFile.is_open() && flowStartTimes.find(flowId) != flowStartTimes.end())
    {
        double endTime = Simulator::Now().GetSeconds();
        double startTime = flowStartTimes[flowId];
        double duration = endTime - startTime;

        uint64_t actualSentBytes = flowActualSentBytes[flowId];
        double goodput = (actualSentBytes * 8.0) / duration / 1000000.0;

        flowTraceFile << "# Flow " << flowId << " COMPLETED: Node " << flowSrcNodes[flowId]
                      << " -> Node " << flowDstNodes[flowId]
                      << " | Actual Sent: " << actualSentBytes << " bytes ("
                      << (actualSentBytes / 1024.0 / 1024.0) << " MB)"
                      << " | Configured: " << configuredBytes << " bytes ("
                      << (configuredBytes / 1024.0 / 1024.0) << " MB)"
                      << " | Duration: " << duration << "s"
                      << " | Goodput: " << goodput << " Mbps" << std::endl;

        flowStartTimes.erase(flowId);
        flowSrcNodes.erase(flowId);
        flowDstNodes.erase(flowId);
        flowReceivedBytes.erase(flowId);
        flowActualSentBytes.erase(flowId);
    }
}

void
OnOffTxPacketTrace(uint32_t flowId, Ptr<const Packet> packet)
{
    if (packet && flowTraceFile.is_open())
    {
        flowActualSentBytes[flowId] += packet->GetSize();
    }
}

void
RegisterTrace(Ptr<StructuredTopology> topo)
{
    for (uint32_t lv = 0; lv < topo->GetNumLevels(); ++lv)
    {
        const NodeContainer& levelNodes = topo->GetLevel(lv);
        for (uint32_t i = 0; i < levelNodes.GetN(); ++i)
        {
            Ptr<Node> node = levelNodes.Get(i);
            Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
            if (ipv4)
            {
                if (lv != 0)
                {
                    ipv4->TraceConnectWithoutContext("Tx", MakeCallback(&PacketTxTrace));
                    ipv4->TraceConnectWithoutContext("Rx", MakeCallback(&PacketRxTrace));
                    ipv4->TraceConnectWithoutContext("Drop", MakeCallback(&PacketDropTrace));
                }
                else
                {
                    ipv4->TraceConnectWithoutContext("Tx", MakeCallback(&PacketTxFlowTrace));
                    ipv4->TraceConnectWithoutContext("Rx", MakeCallback(&PacketRxFlowTrace));
                    ipv4->TraceConnectWithoutContext("Drop", MakeCallback(&PacketDropFlowTrace));
                }
            }
        }
    }
}

static Ptr<DragonflyValiantRouting>
FindDragonflyValiant(Ptr<Ipv4RoutingProtocol> rp)
{
    if (!rp)
    {
        return nullptr;
    }
    if (auto dvr = DynamicCast<DragonflyValiantRouting>(rp))
    {
        return dvr;
    }
    if (auto list = DynamicCast<Ipv4ListRouting>(rp))
    {
        for (uint32_t i = 0; i < list->GetNRoutingProtocols(); ++i)
        {
            int16_t priority = 0;
            Ptr<Ipv4RoutingProtocol> sub = list->GetRoutingProtocol(i, priority);
            if (auto dvr = DynamicCast<DragonflyValiantRouting>(sub))
            {
                return dvr;
            }
        }
    }
    return nullptr;
}

static Ptr<DragonflyUgalRouting>
FindDragonflyUgal(Ptr<Ipv4RoutingProtocol> rp)
{
    if (!rp)
    {
        return nullptr;
    }
    if (auto dur = DynamicCast<DragonflyUgalRouting>(rp))
    {
        return dur;
    }
    if (auto list = DynamicCast<Ipv4ListRouting>(rp))
    {
        for (uint32_t i = 0; i < list->GetNRoutingProtocols(); ++i)
        {
            int16_t priority = 0;
            Ptr<Ipv4RoutingProtocol> sub = list->GetRoutingProtocol(i, priority);
            if (auto dur = DynamicCast<DragonflyUgalRouting>(sub))
            {
                return dur;
            }
        }
    }
    return nullptr;
}

int
main(int argc, char* argv[])
{
    uint32_t groups = 9;
    uint32_t routersPerGroup = 4;
    uint32_t hostsPerRouter = 2;
    uint32_t globalLinksPerRouter = 2;
    uint64_t seed = 1;
    std::string routing = "valiant";
    double ugalAlpha = 1.0;
    double ugalDetourPenalty = 1.0;
    std::string ugalMetric = "bytes";

    std::string rate = "10Gbps";
    std::string delay = "1us";

    std::string trafficPattern = "allreduce";
    uint32_t degree = 4;
    uint64_t dataSize = 1048576;
    std::string dataRate = "100Gbps";
    uint32_t numFlows = 10;
    uint64_t flowSize = 1048576;

    bool debug = false;
    uint32_t routerLevel = 1;
    uint32_t groupDimId = 2;
    std::string tracePrefix = "dragonfly";
    bool dumpCaches = false;
    bool routeTrace = false;
    bool routeTraceVerbose = false;
    bool rewriteRoutes = true;

    CommandLine cmd;
    cmd.AddValue("groups", "Number of router groups (g)", groups);
    cmd.AddValue("routersPerGroup", "Routers per group (a)", routersPerGroup);
    cmd.AddValue("hostsPerRouter", "Hosts per router (p)", hostsPerRouter);
    cmd.AddValue("globalLinksPerRouter", "Global links per router (h)", globalLinksPerRouter);
    cmd.AddValue("seed", "Seed for deterministic transit selection", seed);
    cmd.AddValue("routing", "Routing algorithm: valiant or ugal", routing);
    cmd.AddValue("ugalAlpha", "UGAL alpha coefficient", ugalAlpha);
    cmd.AddValue("ugalDetourPenalty", "UGAL detour penalty", ugalDetourPenalty);
    cmd.AddValue("ugalMetric", "UGAL queue metric: none, packets, bytes", ugalMetric);
    cmd.AddValue("rate", "Link data rate", rate);
    cmd.AddValue("delay", "Link delay", delay);
    cmd.AddValue("trafficPattern", "Traffic pattern: allreduce, alltoall, or flows", trafficPattern);
    cmd.AddValue("degree", "Degree for allreduce/alltoall logical graph", degree);
    cmd.AddValue("dataSize", "Allreduce flow size in bytes", dataSize);
    cmd.AddValue("dataRate", "Allreduce sending data rate", dataRate);
    cmd.AddValue("numFlows", "Number of flows (for flows pattern)", numFlows);
    cmd.AddValue("flowSize", "Flow size in bytes (for flows pattern)", flowSize);
    cmd.AddValue("debug", "Enable debug traces", debug);
    cmd.AddValue("routerLevel", "StructuredTopology router level", routerLevel);
    cmd.AddValue("groupDimId", "StructuredAddress dim for group id", groupDimId);
    cmd.AddValue("tracePrefix", "Trace filename prefix", tracePrefix);
    cmd.AddValue("dumpCaches", "Dump Dragonfly routing caches", dumpCaches);
    cmd.AddValue("routeTrace", "Enable per-packet routing decision trace", routeTrace);
    cmd.AddValue("routeTraceVerbose", "Include extra details in route trace", routeTraceVerbose);
    cmd.AddValue("rewriteRoutes",
                 "Rewrite *_routes.txt IPs to structured addresses (if file exists)",
                 rewriteRoutes);
    cmd.Parse(argc, argv);

    if (trafficPattern == "allreduce")
    {
        degree = 4;
    }
    else if (trafficPattern == "alltoall")
    {
        degree = 8;
    }

    if (groups == 0 || routersPerGroup == 0 || hostsPerRouter == 0)
    {
        NS_FATAL_ERROR("groups/routersPerGroup/hostsPerRouter must be > 0");
    }
    std::transform(routing.begin(), routing.end(), routing.begin(), ::tolower);
    std::transform(ugalMetric.begin(), ugalMetric.end(), ugalMetric.begin(), ::tolower);
    if (routing != "valiant" && routing != "ugal")
    {
        NS_FATAL_ERROR("routing must be 'valiant' or 'ugal'");
    }
    QueueMetric ugalQueueMetric = QueueMetric::kBytes;
    if (ugalMetric == "none")
    {
        ugalQueueMetric = QueueMetric::kNone;
    }
    else if (ugalMetric == "packets")
    {
        ugalQueueMetric = QueueMetric::kPackets;
    }
    else if (ugalMetric == "bytes")
    {
        ugalQueueMetric = QueueMetric::kBytes;
    }
    else
    {
        NS_FATAL_ERROR("ugalMetric must be none, packets, or bytes");
    }
    if (groups != routersPerGroup * globalLinksPerRouter + 1)
    {
        NS_FATAL_ERROR("full-dragonfly requires g = a*h + 1 for Absolute global links");
    }

    std::shared_ptr<TopologyHelper> topoHelper = std::make_shared<TopologyHelper>();
    topoHelper->SetLinkAttributes(rate, delay);

    if (routing == "ugal")
    {
        DragonflyUgalRoutingHelper durHelper;
        durHelper.SetRouterLevel(routerLevel);
        durHelper.SetGroupDimId(groupDimId);
        durHelper.SetSeed(seed);
        durHelper.SetAlpha(ugalAlpha);
        durHelper.SetDetourPenalty(ugalDetourPenalty);
        durHelper.SetQueueMetric(ugalQueueMetric);
        topoHelper->GetInternetStack().SetRoutingHelper(durHelper);
    }
    else
    {
        DragonflyValiantRoutingHelper dvrHelper;
        dvrHelper.SetRouterLevel(routerLevel);
        dvrHelper.SetGroupDimId(groupDimId);
        dvrHelper.SetSeed(seed);
        topoHelper->GetInternetStack().SetRoutingHelper(dvrHelper);
    }

    LevelTemplate::LinkProfile lp{DataRate(rate), Time(delay)};
    TopologyBuilder builder;

    Ptr<ClosInterLevelTemplate> hostLevel =
        CreateObject<ClosInterLevelTemplate>(1, 0, 1, hostsPerRouter, 1, lp);
    hostLevel->SetTopologyHelper(topoHelper);
    builder.AddLevel(hostLevel);

    Ptr<FullIntraLevelTemplate> localLevel = CreateObject<FullIntraLevelTemplate>(
        1, 1, 0, routersPerGroup, 1, "SameRank", lp);
    localLevel->SetTopologyHelper(topoHelper);
    builder.AddLevel(localLevel);

    Ptr<FullIntraLevelTemplate> globalLevel = CreateObject<FullIntraLevelTemplate>(
        1, 2, 0, groups, globalLinksPerRouter, "Absolute", lp);
    globalLevel->SetTopologyHelper(topoHelper);
    builder.AddLevel(globalLevel);

    Ptr<StructuredTopology> topo = CreateObject<StructuredTopology>(topoHelper);
    builder.Build(topo);
    topo->RegisterAddresses();

    uint32_t actualRouterLevel = topo->GetNumLevels() - 1;
    if (routerLevel != actualRouterLevel)
    {
        std::cout << "routerLevel overridden to " << actualRouterLevel
                  << " (input=" << routerLevel << ")" << std::endl;
        routerLevel = actualRouterLevel;
    }

    const auto& routerAddrs = topo->GetStructuredAddrs()[routerLevel];
    if (!routerAddrs.empty())
    {
        size_t maxSize = 0;
        for (const auto& addr : routerAddrs)
        {
            maxSize = std::max(maxSize, addr.Size());
        }
        int levelAddrBit = topo->GetLevelAddrBit(routerLevel);
        size_t detectedIndex = std::numeric_limits<size_t>::max();
        for (size_t idx = 0; idx < maxSize; ++idx)
        {
            std::unordered_set<uint32_t> values;
            for (const auto& addr : routerAddrs)
            {
                if (idx < addr.Size())
                {
                    values.insert(addr[idx]);
                }
            }
            if (values.size() == groups)
            {
                detectedIndex = idx;
                break;
            }
        }
        if (detectedIndex != std::numeric_limits<size_t>::max())
        {
            uint32_t detectedDim = static_cast<uint32_t>(
                detectedIndex >= static_cast<size_t>(std::max(levelAddrBit, 0)) ?
                    detectedIndex - static_cast<size_t>(std::max(levelAddrBit, 0)) :
                    detectedIndex);
            if (detectedDim != groupDimId)
            {
                std::cout << "groupDimId overridden to " << detectedDim
                          << " (input=" << groupDimId << ")" << std::endl;
                groupDimId = detectedDim;
            }
        }
    }

    // Bind topology into routing protocols
    const NodeContainer& allNodes = topo->GetAll();
    bool dumped = false;
    for (uint32_t i = 0; i < allNodes.GetN(); ++i)
    {
        Ptr<Node> node = allNodes.Get(i);
        Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
        if (!ipv4)
        {
            continue;
        }
        if (routing == "ugal")
        {
            Ptr<DragonflyUgalRouting> dur = FindDragonflyUgal(ipv4->GetRoutingProtocol());
            if (dur)
            {
                dur->SetTopology(topo);
                dur->SetRouterLevel(routerLevel);
                dur->SetGroupDimId(groupDimId);
                dur->SetSeed(seed);
                dur->SetAlpha(ugalAlpha);
                dur->SetDetourPenalty(ugalDetourPenalty);
                dur->SetQueueMetric(ugalQueueMetric);
                if (dumpCaches && !dumped)
                {
                    dur->DumpCaches(std::cout);
                    dumped = true;
                }
            }
        }
        else
        {
            Ptr<DragonflyValiantRouting> dvr = FindDragonflyValiant(ipv4->GetRoutingProtocol());
            if (dvr)
            {
                dvr->SetTopology(topo);
                dvr->SetRouterLevel(routerLevel);
                dvr->SetGroupDimId(groupDimId);
                dvr->SetSeed(seed);
                if (dumpCaches && !dumped)
                {
                    dvr->DumpCaches(std::cout);
                    dumped = true;
                }
            }
        }
    }

    std::string packetTraceFilename;
    std::string flowTraceFilename;
    if (debug || routeTrace)
    {
        std::filesystem::create_directories("src/datacenter/examples/traces");
    }

    if (debug)
    {
        packetTraceFilename =
            "src/datacenter/examples/traces/" + tracePrefix + "_packet_trace.txt";
        flowTraceFilename =
            "src/datacenter/examples/traces/" + tracePrefix + "_flow_trace.txt";

        packetTraceFile.open(packetTraceFilename);
        flowTraceFile.open(flowTraceFilename);
        if (!packetTraceFile.is_open() || !flowTraceFile.is_open())
        {
            NS_LOG_ERROR("Failed to open packet trace file or flow trace file!");
            return 1;
        }

        packetTraceFile << "# Packet Trace" << std::endl;
        packetTraceFile << "# Time(s)\tEvent\tInterface\tDetails" << std::endl;
        packetTraceFile << "# ==========================================" << std::endl;
        RegisterTrace(topo);
        topo->PrintTopologyInfo();
    }

    if (routeTrace)
    {
        std::string routeTraceFilename =
            "src/datacenter/examples/traces/" + tracePrefix + "_route_trace.txt";
        routeTraceFile.open(routeTraceFilename);
        if (!routeTraceFile.is_open())
        {
            NS_LOG_ERROR("Failed to open route trace file!");
            return 1;
        }
        std::string routeTag = (routing == "ugal") ? "DUR" : "DVR";
        routeTraceFile << "# Dragonfly " << (routing == "ugal" ? "UGAL" : "Valiant")
                       << " routing decision trace" << std::endl;
        routeTraceFile << "# Time(s)\t" << routeTag
                       << "\tnode\tinject\tsrcAddr\tdstAddr\tsrcG\tdstG\tcurG\tmidG\t"
                          "targetG\ttag\taction\treason\tif\tnh"
                       << std::endl;
        if (routeTraceVerbose)
        {
            routeTraceFile << "# detail=... appears only when routeTraceVerbose=1" << std::endl;
        }

        for (uint32_t i = 0; i < allNodes.GetN(); ++i)
        {
            Ptr<Node> node = allNodes.Get(i);
            Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
            if (!ipv4)
            {
                continue;
            }
            if (routing == "ugal")
            {
                Ptr<DragonflyUgalRouting> dur = FindDragonflyUgal(ipv4->GetRoutingProtocol());
                if (dur)
                {
                    dur->SetTraceRouting(true);
                    dur->SetTraceStream(&routeTraceFile);
                    dur->SetTraceVerbose(routeTraceVerbose);
                }
            }
            else
            {
                Ptr<DragonflyValiantRouting> dvr =
                    FindDragonflyValiant(ipv4->GetRoutingProtocol());
                if (dvr)
                {
                    dvr->SetTraceRouting(true);
                    dvr->SetTraceStream(&routeTraceFile);
                    dvr->SetTraceVerbose(routeTraceVerbose);
                }
            }
        }
    }

    const NodeContainer& hostNodes = topo->GetLevel(0);
    uint32_t hostNum = hostNodes.GetN();

    std::cout << "Host number: " << hostNum << std::endl;
    std::cout << "Traffic pattern: " << trafficPattern << std::endl;

    auto install_graph_traffic = [&](uint32_t graph_degree) {
        SymmetricDegreeGraph logicalGraph(hostNum, graph_degree, 0, false, false, 1, 1);
        logicalGraph.GenerateGraph();

        uint16_t port = 9;
        uint32_t flowIdCounter = 0;

        for (uint32_t i = 0; i < hostNum; i++)
        {
            Ptr<Node> host = hostNodes.Get(i);
            Ptr<Ipv4> ipv4 = host->GetObject<Ipv4>();

            std::vector<uint32_t> dsts = logicalGraph.GetDsts(i);

            Ipv4Address clientIp = host->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal();

            for (auto& dstId : dsts)
            {
                Ptr<Node> dst = hostNodes.Get(dstId);
                Ptr<Ipv4> dstIpv4 = dst->GetObject<Ipv4>();
                OnOffHelper onoff("ns3::UdpSocketFactory",
                                  InetSocketAddress(dstIpv4->GetAddress(1, 0).GetLocal(), port));
                onoff.SetConstantRate(DataRate(dataRate));
                onoff.SetAttribute("PacketSize", UintegerValue(1000));
                onoff.SetAttribute("MaxBytes", UintegerValue(dataSize));
                onoff.SetAttribute("Local", AddressValue(InetSocketAddress(clientIp, 0)));

                ApplicationContainer sourceApps = onoff.Install(host);

                if (debug && flowTraceFile.is_open())
                {
                    for (uint32_t j = 0; j < sourceApps.GetN(); ++j)
                    {
                        Ptr<Application> app = sourceApps.Get(j);
                        Ptr<OnOffApplication> onoffApp = DynamicCast<OnOffApplication>(app);
                        if (onoffApp)
                        {
                            uint32_t currentFlowId = flowIdCounter++;

                            double startTime = Simulator::Now().GetSeconds();
                            flowStartTimes[currentFlowId] = startTime;
                            flowSrcNodes[currentFlowId] = host->GetId();
                            flowDstNodes[currentFlowId] = dst->GetId();
                            flowActualSentBytes[currentFlowId] = 0;

                            flowTraceFile << "# Flow " << currentFlowId << " STARTED: Node "
                                          << host->GetId() << " -> Node " << dst->GetId()
                                          << " | Configured Size: " << dataSize << " bytes ("
                                          << (dataSize / 1024.0 / 1024.0) << " MB)"
                                          << " | Start Time: " << startTime << "s" << std::endl;

                            onoffApp->TraceConnectWithoutContext(
                                "Tx",
                                MakeBoundCallback(&OnOffTxPacketTrace, currentFlowId));

                            Simulator::Schedule(Seconds(4.9),
                                                MakeEvent(&FlowStopTrace, currentFlowId, dataSize));
                        }
                    }
                }

                sourceApps.Start(Seconds(1.0));
                sourceApps.Stop(Seconds(5.0));
            }

            PacketSinkHelper sink("ns3::UdpSocketFactory",
                                  Address(InetSocketAddress(Ipv4Address::GetAny(), port)));
            ApplicationContainer apps = sink.Install(host);
            apps.Start(Seconds(1.0));
            apps.Stop(Seconds(5.5));
        }
    };

    if (trafficPattern == "allreduce")
    {
        install_graph_traffic(degree);
    }
    else if (trafficPattern == "alltoall")
    {
        install_graph_traffic(degree);
    }
    else if (trafficPattern == "flows")
    {
        if (hostNum < 2)
        {
            NS_LOG_ERROR("Not enough hosts to deploy applications.");
            return 1;
        }

        std::vector<Ipv4Address> hostAddresses;
        for (uint32_t i = 0; i < hostNum; ++i)
        {
            Ptr<Node> host = hostNodes.Get(i);
            Ipv4Address hostIp = host->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal();
            hostAddresses.push_back(hostIp);
        }

        double simTime = 5.0;
        uint32_t actualFlows = std::min(numFlows, hostNum);
        uint16_t basePort = 5000;

        std::cout << "Number of flows: " << actualFlows << std::endl;
        std::cout << "Flow size: " << flowSize << " bytes ("
                  << (flowSize / 1024.0 / 1024.0) << " MB)" << std::endl;

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

            if (debug)
            {
                std::cout << "Flow " << flow << ": Host " << srcIndex
                          << " (Node " << hostNodes.Get(srcIndex)->GetId() << ") -> Host "
                          << dstIndex << " (Node " << hostNodes.Get(dstIndex)->GetId()
                          << "), Port " << (basePort + flow) << std::endl;
            }
        }
    }
    else
    {
        NS_FATAL_ERROR("Unknown traffic pattern: " << trafficPattern
                       << ". Use 'allreduce', 'alltoall', or 'flows'.");
    }

    Simulator::Run();

    if (debug && flowTraceFile.is_open())
    {
        flowTraceFile << std::endl;
        flowTraceFile
            << "# Flow_ID\tSource_IP\tDest_IP\tTx_Packets\tRx_Packets\tLost_Packets\t"
               "Tx_Bytes\tRx_Bytes\tThroughput(Mbps)\tDuration(s)"
            << std::endl;

        uint32_t flowId = 0;
        for (const auto& kv : g_flowStats)
        {
            const auto& key = kv.first;
            const auto& st = kv.second;
            const uint64_t lost =
                (st.txPackets >= st.rxPackets) ? (st.txPackets - st.rxPackets) : 0;

            double dur = 0.0;
            if (st.sawFirstRx && st.lastRx > st.firstRx)
            {
                dur = (st.lastRx - st.firstRx).GetSeconds();
            }

            double thrMbps = (dur > 0.0) ? (st.rxBytes * 8.0 / dur / 1e6) : 0.0;

            flowTraceFile << flowId << "\t" << key.src << "\t" << key.dst << "\t"
                          << st.txPackets << "\t" << st.rxPackets << "\t" << lost << "\t"
                          << st.txBytes << "\t" << st.rxBytes << "\t" << std::fixed
                          << std::setprecision(2) << thrMbps << "\t" << std::fixed
                          << std::setprecision(6) << dur << std::endl;
            ++flowId;
        }
    }

    Simulator::Destroy();

    if (rewriteRoutes)
    {
        const std::string routesPath =
            "src/datacenter/examples/traces/" + tracePrefix + "_routes.txt";
        auto ipMap = BuildIpToStructuredMap(topo);
        RewriteRoutesFileWithStructured(routesPath, ipMap);
    }

    if (debug)
    {
        if (packetTraceFile.is_open())
        {
            packetTraceFile.close();
            std::cout << "Packet trace saved to: " << packetTraceFilename << std::endl;
        }
        if (flowTraceFile.is_open())
        {
            flowTraceFile.close();
            std::cout << "Flow completion trace saved to: " << flowTraceFilename << std::endl;
        }
    }

    return 0;
}
