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
#include "ns3/failure-helper.h"
#include "ns3/internet-module.h"
#include "ns3/ipv4-list-routing.h"
#include "ns3/node-bfs-routing.h"
#include "ns3/node-bfs-routing-helper.h"
#include "ns3/point-to-point-channel.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/packet.h"
#include "ns3/congestion-signal-provider.h"
#include "ns3/non-minimal-policy.h"
#include "ns3/rule-based-routing.h"
#include "ns3/rule-based-routing-helper.h"
#include "ns3/intra-server-level-template.h"
#include "ns3/rule-based-routing-helper.h"
#include "ns3/socket.h"
#include "ns3/structured-address-directory.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/channel.h"

// --- JSON parser ---
#include "ns3/json.hpp" // header-only

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>

using json = nlohmann::json;
using namespace ns3;

NS_LOG_COMPONENT_DEFINE("ConstructorExample");

// ------------------ Trace and public tools ------------------

// Packet trace file
std::ofstream packetTraceFile;
std::ofstream flowTraceFile;

// Flow trace全局变量
std::map<uint32_t, double> flowStartTimes;
std::map<uint32_t, uint32_t> flowSrcNodes;
std::map<uint32_t, uint32_t> flowDstNodes;
std::map<uint32_t, uint64_t> flowReceivedBytes;
std::map<uint32_t, uint64_t> flowActualSentBytes;

// void
// PacketTxTrace(Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface)
// {
//     if (packetTraceFile.is_open())
//     {
//         Ipv4Header header;
//         if (packet->PeekHeader(header))
//         {
//             packetTraceFile << Simulator::Now().GetSeconds() << "\tTX\t"
//                             << "Interface: " << interface << "\t"
//                             << "Packet Size: " << packet->GetSize() << " bytes\t"
//                             << "Interface IP: " << ipv4->GetAddress(interface, 0).GetLocal() <<
//                             "\t"
//                             << "Src: " << header.GetSource() << "\t"
//                             << "Dst: " << header.GetDestination() << std::endl;
//         }
//         else
//         {
//             packetTraceFile << Simulator::Now().GetSeconds() << "\tTX\t"
//                             << "Interface: " << interface << "\t"
//                             << "Packet Size: " << packet->GetSize() << " bytes\t"
//                             << "Interface IP: " << ipv4->GetAddress(interface, 0).GetLocal()
//                             << std::endl;
//         }
//     }
// }

// void
// PacketRxTrace(Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface)
// {
//     if (packetTraceFile.is_open())
//     {
//         Ipv4Header header;
//         if (packet->PeekHeader(header))
//         {
//             packetTraceFile << Simulator::Now().GetSeconds() << "\tRX\t"
//                             << "Interface: " << interface << "\t"
//                             << "Packet Size: " << packet->GetSize() << " bytes\t"
//                             << "Interface IP: " << ipv4->GetAddress(interface, 0).GetLocal() <<
//                             "\t"
//                             << "Src: " << header.GetSource() << "\t"
//                             << "Dst: " << header.GetDestination() << std::endl;
//         }
//         else
//         {
//             packetTraceFile << Simulator::Now().GetSeconds() << "\tRX\t"
//                             << "Interface: " << interface << "\t"
//                             << "Packet Size: " << packet->GetSize() << " bytes\t"
//                             << "Interface IP: " << ipv4->GetAddress(interface, 0).GetLocal()
//                             << std::endl;
//         }
//     }
// }

// void
// PacketDropTrace(const Ipv4Header& header,
//                 Ptr<const Packet> packet,
//                 Ipv4L3Protocol::DropReason reason,
//                 Ptr<Ipv4> ipv4,
//                 uint32_t interface)
// {
//     if (packetTraceFile.is_open())
//     {
//         packetTraceFile << Simulator::Now().GetSeconds() << "\tDROP\t"
//                         << "Interface: " << interface << "\t"
//                         << "Reason: " << static_cast<int>(reason) << "\t"
//                         << "Src: " << header.GetSource() << "\t"
//                         << "Dst: " << header.GetDestination() << "\t"
//                         << "Packet Size: " << packet->GetSize() << " bytes" << std::endl;
//     }
// }

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

// 辅助：安全获取 IPv4 头（有时 PeekHeader 可能失败）
static bool
PeekIpv4(Ptr<const Packet> pkt, Ipv4Header& hdr)
{
    // 复制一份再 Peek，避免影响原包
    Packet copy = *pkt->Copy();
    return copy.PeekHeader(hdr);
}

void
PacketTxTrace(Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface)
{
    if (packetTraceFile.is_open())
    {
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
}

void
PacketRxTrace(Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface)
{
    if (packetTraceFile.is_open())
    {
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
    if (packetTraceFile.is_open())
    {
        Ipv4Header header;
        if (PeekIpv4(packet, header))
        {
            packetTraceFile << Simulator::Now().GetSeconds() << "\tTX\t"
                            << "Interface: " << interface << "\t"
                            << "Packet Size: " << packet->GetSize() << " bytes\t"
                            << "Interface IP: " << ipv4->GetAddress(interface, 0).GetLocal() << "\t"
                            << "Src: " << header.GetSource() << "\t"
                            << "Dst: " << header.GetDestination() << std::endl;

            // —— 更新聚合统计 —— //
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
}

void
PacketRxFlowTrace(Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface)
{
    if (packetTraceFile.is_open())
    {
        Ipv4Header header;
        if (PeekIpv4(packet, header))
        {
            packetTraceFile << Simulator::Now().GetSeconds() << "\tRX\t"
                            << "Interface: " << interface << "\t"
                            << "Packet Size: " << packet->GetSize() << " bytes\t"
                            << "Interface IP: " << ipv4->GetAddress(interface, 0).GetLocal() << "\t"
                            << "Src: " << header.GetSource() << "\t"
                            << "Dst: " << header.GetDestination() << std::endl;

            // —— 更新聚合统计 —— //
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

    // —— 更新聚合统计 —— //
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

        // 获取实际发送的字节数
        uint64_t actualSentBytes = flowActualSentBytes[flowId];
        double goodput = (actualSentBytes * 8.0) / duration / 1000000.0; // Mbps

        flowTraceFile << "# Flow " << flowId << " COMPLETED: Node " << flowSrcNodes[flowId]
                      << " -> Node " << flowDstNodes[flowId]
                      << " | Actual Sent: " << actualSentBytes << " bytes ("
                      << (actualSentBytes / 1024.0 / 1024.0) << " MB)"
                      << " | Configured: " << configuredBytes << " bytes ("
                      << (configuredBytes / 1024.0 / 1024.0) << " MB)"
                      << " | Duration: " << duration << "s"
                      << " | Goodput: " << goodput << " Mbps" << std::endl;

        // Clean up
        flowStartTimes.erase(flowId);
        flowSrcNodes.erase(flowId);
        flowDstNodes.erase(flowId);
        flowReceivedBytes.erase(flowId);
        flowActualSentBytes.erase(flowId);
    }
}

// 全局静态函数用于跟踪OnOff应用的Tx数据包
void
OnOffTxPacketTrace(uint32_t flowId, Ptr<const Packet> packet)
{
    if (packet && flowTraceFile.is_open())
    {
        uint32_t packetSize = packet->GetSize();
        flowActualSentBytes[flowId] += packetSize;

        // 可选：实时记录每个数据包的发送（如果需要详细跟踪）
        // flowTraceFile << "# Flow " << flowId << " PACKET: " << packetSize
        //               << " bytes at " << Simulator::Now().GetSeconds() << "s" << std::endl;
    }
}

static void
SendTraceReplayPacket(Ptr<Socket> socket,
                      Ipv4Address dstIp,
                      uint16_t port,
                      uint64_t remainingBytes,
                      uint32_t packetSize,
                      DataRate dataRate)
{
    if (!socket || remainingBytes == 0)
    {
        return;
    }


    uint32_t sendSize = static_cast<uint32_t>(
        std::min<uint64_t>(remainingBytes, static_cast<uint64_t>(packetSize)));
    Ptr<Packet> packet = Create<Packet>(sendSize);
    socket->SendTo(packet, 0, InetSocketAddress(dstIp, port));

    uint64_t nextRemaining = remainingBytes - sendSize;
    if (nextRemaining > 0)
    {
        Time nextDelay = dataRate.CalculateBytesTxTime(sendSize);
        Simulator::Schedule(nextDelay,
                            &SendTraceReplayPacket,
                            socket,
                            dstIp,
                            port,
                            nextRemaining,
                            packetSize,
                            dataRate);
    }
}


static uint32_t
RoundRobinAllToAllDst(uint32_t src, uint32_t round, uint32_t hostNum)
{
    if (hostNum < 2 || round == 0 || round >= hostNum)
    {
        return hostNum;
    }
    return (src + round) % hostNum;
}

struct AllToAllStreamState
{
    AllToAllStreamState(Ptr<Socket> socket,
                        std::shared_ptr<const std::vector<Ipv4Address>> hostIps,
                        uint32_t src,
                        uint64_t chunkBytes,
                        uint32_t packetSize,
                        DataRate dataRate,
                        uint16_t port,
                        bool roundRobin,
                        Time roundGap)
        : socket(socket),
          hostIps(std::move(hostIps)),
          src(src),
          remainingBytes(chunkBytes),
          chunkBytes(chunkBytes),
          packetSize(packetSize),
          dataRate(dataRate),
          port(port),
          roundRobin(roundRobin),
          roundGap(roundGap)
    {
        const uint32_t hostNum = this->hostIps ? this->hostIps->size() : 0;
        dst = roundRobin ? RoundRobinAllToAllDst(src, round, hostNum) : (src == 0 ? 1 : 0);
    }

    Ptr<Socket> socket;
    std::shared_ptr<const std::vector<Ipv4Address>> hostIps;
    uint32_t src{0};
    uint32_t dst{0};
    uint32_t round{1};
    uint64_t remainingBytes{0};
    uint64_t chunkBytes{0};
    uint32_t packetSize{0};
    DataRate dataRate;
    uint16_t port{0};
    bool roundRobin{false};
    Time roundGap;
};

static uint32_t
NextAllToAllDst(uint32_t src, uint32_t dst, uint32_t hostNum)
{
    do
    {
        ++dst;
    } while (dst < hostNum && dst == src);
    return dst;
}

static bool
AdvanceAllToAllDestination(const std::shared_ptr<AllToAllStreamState>& state, uint32_t hostNum)
{
    if (state->roundRobin)
    {
        ++state->round;
        state->dst = RoundRobinAllToAllDst(state->src, state->round, hostNum);
    }
    else
    {
        state->dst = NextAllToAllDst(state->src, state->dst, hostNum);
    }
    return state->dst < hostNum;
}


static void
SendAllToAllStreamPacket(std::shared_ptr<AllToAllStreamState> state)
{

    if (!state || !state->socket || !state->hostIps || state->hostIps->empty())
    {
        return;
    }

    const uint32_t hostNum = state->hostIps->size();
    if (state->dst >= hostNum || state->remainingBytes == 0)
    {
        return;
    }


    uint32_t sendSize = static_cast<uint32_t>(
        std::min<uint64_t>(state->remainingBytes, static_cast<uint64_t>(state->packetSize)));
    Ptr<Packet> packet = Create<Packet>(sendSize);
    state->socket->SendTo(packet, 0, InetSocketAddress((*state->hostIps)[state->dst], state->port));

    state->remainingBytes -= sendSize;
    Time nextDelay = state->dataRate.CalculateBytesTxTime(sendSize);
    if (state->remainingBytes == 0)
    {
        if (!AdvanceAllToAllDestination(state, hostNum))
        {
            return;
        }
        state->remainingBytes = state->chunkBytes;
        nextDelay += state->roundGap;
    }

    Simulator::Schedule(nextDelay, &SendAllToAllStreamPacket, state);
}

static void
CloseTraceReplaySocket(Ptr<Socket> socket)
{
    if (socket)
    {
        socket->Close();
    }
}

struct TrafficFlow
{
    double startSeconds{1.0};
    uint32_t src{0};
    uint32_t dst{0};
    uint64_t bytes{0};
    uint64_t tag{0};
};

static std::string
Trim(const std::string& input)
{
    size_t begin = 0;
    while (begin < input.size() &&
           std::isspace(static_cast<unsigned char>(input[begin])))
    {
        ++begin;
    }
    size_t end = input.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(input[end - 1])))
    {
        --end;
    }
    return input.substr(begin, end - begin);
}

static std::string
NormalizeColumnName(const std::string& input)
{
    std::string out = Trim(input);
    for (char& ch : out)
    {
        unsigned char c = static_cast<unsigned char>(ch);
        ch = static_cast<char>(std::tolower(c));
        if (ch == '-' || ch == '.' || std::isspace(c))
        {
            ch = '_';
        }
    }
    return out;
}

static std::vector<std::string>
SplitCsvLine(const std::string& line)
{
    std::vector<std::string> cells;
    std::string cell;
    bool inQuotes = false;

    for (char ch : line)
    {
        if (ch == '"')
        {
            inQuotes = !inQuotes;
        }
        else if (ch == ',' && !inQuotes)
        {
            cells.push_back(Trim(cell));
            cell.clear();
        }
        else
        {
            cell.push_back(ch);
        }
    }
    cells.push_back(Trim(cell));
    return cells;
}

static bool
LooksLikeTrafficTraceHeader(const std::vector<std::string>& cells)
{
    for (const std::string& raw : cells)
    {
        std::string col = NormalizeColumnName(raw);
        if (col == "start_s" || col == "time_s" || col == "start_time_s" ||
            col == "src" || col == "source" || col == "from" || col == "src_rank" ||
            col == "dst" || col == "dest" || col == "destination" || col == "to" ||
            col == "dst_rank" || col == "target" || col == "bytes" ||
            col == "size" || col == "size_bytes" || col == "flow_size" ||
            col == "nbytes" || col == "tag")
        {
            return true;
        }
    }
    return false;
}

static int
FindTrafficTraceColumn(const std::vector<std::string>& header,
                       std::initializer_list<const char*> names)
{
    for (uint32_t i = 0; i < header.size(); ++i)
    {
        std::string col = NormalizeColumnName(header[i]);
        for (const char* name : names)
        {
            if (col == name)
            {
                return static_cast<int>(i);
            }
        }
    }
    return -1;
}

static TrafficFlow
ParseTrafficTraceRow(const std::vector<std::string>& cells,
                     const std::vector<std::string>& header,
                     uint32_t lineNo)
{
    auto requireCell = [&](int idx, const std::string& name) -> std::string {
        if (idx < 0 || static_cast<size_t>(idx) >= cells.size())
        {
            NS_FATAL_ERROR("Traffic trace line " << lineNo << " is missing required column "
                                                 << name);
        }
        return cells[static_cast<size_t>(idx)];
    };

    int startIdx = 0;
    int srcIdx = 1;
    int dstIdx = 2;
    int bytesIdx = 3;
    int tagIdx = -1;
    if (!header.empty())
    {
        startIdx = FindTrafficTraceColumn(header,
                                          {"start_s", "time_s", "start_time_s", "start"});
        srcIdx = FindTrafficTraceColumn(header, {"src", "source", "from", "src_rank"});
        dstIdx = FindTrafficTraceColumn(header,
                                        {"dst", "dest", "destination", "to", "dst_rank", "target"});
        bytesIdx = FindTrafficTraceColumn(header,
                                          {"bytes", "size", "size_bytes", "flow_size", "nbytes"});
        tagIdx = FindTrafficTraceColumn(header, {"tag", "flow_id", "id"});
    }

    TrafficFlow flow;
    try
    {
        flow.startSeconds = std::stod(requireCell(startIdx, "start_s"));
        flow.src = static_cast<uint32_t>(std::stoul(requireCell(srcIdx, "src")));
        flow.dst = static_cast<uint32_t>(std::stoul(requireCell(dstIdx, "dst")));
        flow.bytes = static_cast<uint64_t>(std::stoull(requireCell(bytesIdx, "bytes")));
        if (tagIdx >= 0 && static_cast<size_t>(tagIdx) < cells.size() && !cells[tagIdx].empty())
        {
            flow.tag = static_cast<uint64_t>(std::stoull(cells[tagIdx]));
        }
    }
    catch (const std::exception& ex)
    {
        NS_FATAL_ERROR("Failed to parse traffic trace line " << lineNo << ": " << ex.what());
    }
    return flow;
}

static std::vector<TrafficFlow>
LoadTrafficTrace(const std::string& traceFile,
                 uint32_t hostNum,
                 uint32_t maxFlows,
                 double timeScale,
                 double startOffsetSeconds)
{
    std::ifstream in(traceFile);
    if (!in.is_open())
    {
        NS_FATAL_ERROR("Cannot open traffic trace file: " << traceFile);
    }

    std::vector<TrafficFlow> flows;
    std::vector<std::string> header;
    bool decidedHeader = false;
    std::string line;
    uint32_t lineNo = 0;
    while (std::getline(in, line))
    {
        ++lineNo;
        std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#')
        {
            continue;
        }

        std::vector<std::string> cells = SplitCsvLine(trimmed);
        if (!decidedHeader)
        {
            decidedHeader = true;
            if (LooksLikeTrafficTraceHeader(cells))
            {
                header = cells;
                continue;
            }
        }

        TrafficFlow flow = ParseTrafficTraceRow(cells, header, lineNo);
        flow.startSeconds = flow.startSeconds * timeScale + startOffsetSeconds;

        if (flow.startSeconds < 0.0)
        {
            NS_FATAL_ERROR("Traffic trace line " << lineNo << " has negative replay start time.");
        }
        if (flow.bytes == 0)
        {
            continue;
        }
        if (flow.src >= hostNum || flow.dst >= hostNum)
        {
            NS_FATAL_ERROR("Traffic trace line "
                           << lineNo << " references host index outside [0, "
                           << (hostNum == 0 ? 0 : hostNum - 1) << "]: src=" << flow.src
                           << " dst=" << flow.dst);
        }
        if (flow.src == flow.dst)
        {
            continue;
        }

        flows.push_back(flow);
        if (maxFlows > 0 && flows.size() >= maxFlows)
        {
            break;
        }
    }

    std::sort(flows.begin(), flows.end(), [](const TrafficFlow& a, const TrafficFlow& b) {
        if (a.startSeconds != b.startSeconds)
        {
            return a.startSeconds < b.startSeconds;
        }
        if (a.src != b.src)
        {
            return a.src < b.src;
        }
        return a.dst < b.dst;
    });

    if (flows.empty())
    {
        NS_FATAL_ERROR("Traffic trace file produced no usable flows: " << traceFile);
    }
    return flows;
}

static uint64_t
ChunkBytesPerRank(uint64_t dataSize, uint32_t hostNum)
{
    if (hostNum == 0)
    {
        return 0;
    }
    return std::max<uint64_t>(1, dataSize / hostNum);
}

static std::vector<TrafficFlow>
GenerateDenseAllToAllTraffic(uint32_t hostNum, uint64_t dataSize)
{
    if (hostNum < 2)
    {
        NS_FATAL_ERROR("alltoall traffic requires at least two hosts.");
    }

    const uint64_t chunkBytes = ChunkBytesPerRank(dataSize, hostNum);
    std::vector<TrafficFlow> flows;
    flows.reserve(static_cast<size_t>(hostNum) * static_cast<size_t>(hostNum - 1));

    uint64_t tag = 0;
    for (uint32_t src = 0; src < hostNum; ++src)
    {
        for (uint32_t dst = 0; dst < hostNum; ++dst)
        {
            if (src == dst)
            {
                continue;
            }
            flows.push_back(TrafficFlow{1.0, src, dst, chunkBytes, tag++});
        }
    }

    std::cout << "Traffic generated: pattern=dense-alltoall"
              << " hosts=" << hostNum
              << " flows=" << flows.size()
              << " chunk_bytes=" << chunkBytes << std::endl;
    return flows;
}

static std::vector<TrafficFlow>
GenerateFlowTraffic(uint32_t hostNum, uint32_t numFlows, uint64_t flowSize)
{
    if (hostNum < 2)
    {
        NS_FATAL_ERROR("flows traffic requires at least two hosts.");
    }

    std::vector<TrafficFlow> flows;
    flows.reserve(numFlows);
    for (uint32_t flowIdx = 0; flowIdx < numFlows; ++flowIdx)
    {
        uint32_t src = flowIdx % hostNum;
        uint32_t dstOffset = 1 + ((flowIdx / hostNum) % (hostNum - 1));
        uint32_t dst = (src + dstOffset) % hostNum;
        flows.push_back(TrafficFlow{1.0, src, dst, flowSize, flowIdx});
    }

    std::cout << "Traffic generated: pattern=flows"
              << " hosts=" << hostNum
              << " flows=" << flows.size()
              << " flow_bytes=" << flowSize << std::endl;
    return flows;
}

static std::vector<TrafficFlow>
GenerateRingAllReduceTraffic(uint32_t hostNum, uint64_t dataSize)
{
    if (hostNum < 2)
    {
        NS_FATAL_ERROR("allreduce traffic requires at least two hosts.");
    }

    const uint32_t steps = 2 * (hostNum - 1);
    const uint64_t chunkBytes = ChunkBytesPerRank(dataSize, hostNum);
    const uint64_t aggregatedBytes = chunkBytes * static_cast<uint64_t>(steps);
    std::vector<TrafficFlow> flows;
    flows.reserve(hostNum);

    for (uint32_t src = 0; src < hostNum; ++src)
    {
        uint32_t dst = (src + 1) % hostNum;
        flows.push_back(TrafficFlow{1.0, src, dst, aggregatedBytes, src});
    }

    std::cout << "Traffic generated: pattern=ring-allreduce-aggregated"
              << " hosts=" << hostNum
              << " flows=" << flows.size()
              << " logical_steps=" << steps
              << " chunk_bytes=" << chunkBytes
              << " aggregated_bytes=" << aggregatedBytes << std::endl;
    return flows;
}

static uint32_t
ResolveAllReduceGroupSize(uint32_t requestedGroupSize, uint32_t hostNum)
{
    if (hostNum < 2)
    {
        return hostNum;
    }
    if (requestedGroupSize == 0 || requestedGroupSize > hostNum)
    {
        return hostNum;
    }
    return std::max<uint32_t>(2, requestedGroupSize);
}

static std::vector<std::vector<uint32_t>>
BuildAllReduceGroups(uint32_t hostNum, uint32_t groupSize, const std::string& placement)
{
    std::vector<std::vector<uint32_t>> groups;
    if (hostNum < 2)
    {
        return groups;
    }

    const uint32_t resolvedGroupSize = ResolveAllReduceGroupSize(groupSize, hostNum);
    const uint32_t groupCount = (hostNum + resolvedGroupSize - 1) / resolvedGroupSize;
    groups.resize(groupCount);

    if (placement == "strided")
    {
        for (uint32_t rank = 0; rank < hostNum; ++rank)
        {
            groups[rank % groupCount].push_back(rank);
        }
    }
    else
    {
        for (uint32_t rank = 0; rank < hostNum; ++rank)
        {
            groups[rank / resolvedGroupSize].push_back(rank);
        }
    }

    groups.erase(std::remove_if(groups.begin(),
                                groups.end(),
                                [](const auto& group) { return group.size() < 2; }),
                 groups.end());
    return groups;
}

static std::vector<TrafficFlow>
GenerateGroupedRingAllReduceTraffic(uint32_t hostNum,
                                     uint64_t dataSize,
                                     uint32_t groupSize,
                                     const std::string& placement,
                                     double stepGapSeconds)
{
    if (hostNum < 2)
    {
        NS_FATAL_ERROR("grouped-allreduce traffic requires at least two hosts.");
    }
    if (placement != "contiguous" && placement != "strided")
    {
        NS_FATAL_ERROR("grouped-allreduce placement must be contiguous or strided.");
    }
    if (stepGapSeconds < 0.0)
    {
        NS_FATAL_ERROR("grouped-allreduce step gap must be non-negative.");
    }

    auto groups = BuildAllReduceGroups(hostNum, groupSize, placement);
    if (groups.empty())
    {
        NS_FATAL_ERROR("grouped-allreduce produced no valid groups.");
    }

    uint64_t logicalFlowCount = 0;
    uint64_t maxSteps = 0;
    uint32_t minGroupSize = groups.front().size();
    uint32_t maxGroupSize = groups.front().size();
    for (const auto& group : groups)
    {
        const uint64_t steps = 2ull * static_cast<uint64_t>(group.size() - 1);
        logicalFlowCount += static_cast<uint64_t>(group.size()) * steps;
        maxSteps = std::max(maxSteps, steps);
        minGroupSize = std::min<uint32_t>(minGroupSize, group.size());
        maxGroupSize = std::max<uint32_t>(maxGroupSize, group.size());
    }

    std::vector<TrafficFlow> flows;
    flows.reserve(static_cast<size_t>(logicalFlowCount));

    uint64_t tag = 0;
    for (const auto& group : groups)
    {
        const uint64_t steps = 2ull * static_cast<uint64_t>(group.size() - 1);
        const uint64_t chunkBytes = ChunkBytesPerRank(dataSize, group.size());
        for (uint64_t step = 0; step < steps; ++step)
        {
            const double startSeconds = 1.0 + static_cast<double>(step) * stepGapSeconds;
            for (uint32_t idx = 0; idx < group.size(); ++idx)
            {
                const uint32_t src = group[idx];
                const uint32_t dst = group[(idx + 1) % group.size()];
                flows.push_back(TrafficFlow{startSeconds, src, dst, chunkBytes, tag++});
            }
        }
    }

    std::cout << "Traffic generated: pattern=grouped-ring-allreduce"
              << " hosts=" << hostNum
              << " groups=" << groups.size()
              << " requested_group_size=" << groupSize
              << " min_group_size=" << minGroupSize
              << " max_group_size=" << maxGroupSize
              << " placement=" << placement
              << " max_logical_steps=" << maxSteps
              << " flows=" << flows.size()
              << " chunk_bytes_first_group=" << ChunkBytesPerRank(dataSize, groups.front().size())
              << " aggregate=0" << std::endl;
    return flows;
}

static PortSelectPolicy
BuildFromJson(const json& cfg,
              TopologyBuilder& builder,
              std::shared_ptr<TopologyHelper> topoHelper)
{
    // Default link profile. Individual dims may override bandwidth/delay.
    std::string bwStr = cfg.at("link").value("bandwidth", "10Gbps");
    std::string delayStr = cfg.at("link").value("delay", "1us");
    auto getLinkProfile = [&](const json& dim) {
        return LevelTemplate::LinkProfile{DataRate(dim.value("bandwidth", bwStr)),
                                          Time(dim.value("delay", delayStr))};
    };

    // Track an aggregate ECMP policy for NodeBfs (kRandom > kByHash > kFirst)
    bool sawRandom = false;
    bool sawByHash = false;

    uint32_t levelId = 0;
    // Create template level by level
    for (const auto& lv : cfg.at("levels"))
    {
        uint32_t dimId = 0;
        // Each level contains a dims array
        for (const auto& dim : lv.at("dims"))
        {
            std::string type = dim.at("template");

            // Read load-balance policy from JSON if specified
            PortSelectPolicy portPolicy = PortSelectPolicy::kFirst;  // Default policy
            bool hasLoadBalance = false;
            if (dim.contains("loadBalance"))
            {
                std::string loadBalanceStr = dim.at("loadBalance");
                hasLoadBalance = true;
                if (loadBalanceStr == "kFirst")
                {
                    portPolicy = PortSelectPolicy::kFirst;
                }
                else if (loadBalanceStr == "kRandom")
                {
                    portPolicy = PortSelectPolicy::kRandom;
                    sawRandom = true;
                }
                else if (loadBalanceStr == "kByHash" || loadBalanceStr == "kHash")
                {
                    portPolicy = PortSelectPolicy::kByHash;
                    sawByHash = true;
                }
                else
                {
                    NS_ABORT_MSG("Unknown loadBalance policy in JSON: " << loadBalanceStr);
                }
            }
            else
            {
                // Default policy for RuleBased is kFirst if loadBalance is omitted.
                // sawByHash = true;
            }

            LevelTemplate::LinkProfile dimLink = getLinkProfile(dim);

            if (type == "IntraServerLevel" || type == "RailOptimizedHostLevel")
            {
                uint32_t endpointsPerServer = 0;
                if (dim.contains("endpointsPerServer"))
                {
                    endpointsPerServer = dim.at("endpointsPerServer");
                }
                else if (dim.contains("gpuPerServer"))
                {
                    endpointsPerServer = dim.at("gpuPerServer");
                }
                else
                {
                    NS_ABORT_MSG("IntraServerLevel requires endpointsPerServer");
                }

                uint32_t serverNum = 0;
                if (dim.contains("serverNum"))
                {
                    serverNum = dim.at("serverNum");
                }
                else
                {
                    uint32_t endpointNum = 0;
                    if (dim.contains("endpointNum"))
                    {
                        endpointNum = dim.at("endpointNum");
                    }
                    else if (dim.contains("gpuNum"))
                    {
                        endpointNum = dim.at("gpuNum");
                    }
                    else
                    {
                        NS_ABORT_MSG("IntraServerLevel requires serverNum or endpointNum");
                    }
                    NS_ASSERT_MSG(endpointsPerServer > 0 && endpointNum % endpointsPerServer == 0,
                                  "IntraServerLevel: endpointNum must be divisible by endpointsPerServer");
                    serverNum = endpointNum / endpointsPerServer;
                }

                std::string linkArrangement = dim.value("linkArrangement", "FullMesh");
                Ptr<IntraServerLevelTemplate> t = CreateObject<IntraServerLevelTemplate>(serverNum,
                                                                                         endpointsPerServer,
                                                                                         linkArrangement,
                                                                                         dimLink);
                if (hasLoadBalance)
                {
                    t->SetPortSelectPolicy(portPolicy);
                }
                t->SetTopologyHelper(topoHelper);
                builder.AddLevel(t);
            }
            else if (type == "ClosInterLevel")
            {
                ++levelId;
                uint32_t nodeNum = dim.at("nodeNum");
                uint32_t subBlockNum = dim.at("subBlockNum");
                uint32_t groupNum = dim.at("groupNum");
                std::string linkArrangement = dim.value("linkArrangement", "Contiguous");

                Ptr<ClosInterLevelTemplate> t;
                if (linkArrangement == "RailOptimized")
                {
                    uint32_t endpointsPerServer = 0;
                    if (dim.contains("endpointsPerServer"))
                    {
                        endpointsPerServer = dim.at("endpointsPerServer");
                    }
                    else if (dim.contains("gpuPerServer"))
                    {
                        endpointsPerServer = dim.at("gpuPerServer");
                    }
                    else
                    {
                        NS_ABORT_MSG("RailOptimized ClosInterLevel requires endpointsPerServer");
                    }
                    uint32_t nicsPerAswitch = dim.at("nicsPerAswitch");
                    t = CreateObject<ClosInterLevelTemplate>(levelId,
                                                             dimId,
                                                             nodeNum,
                                                             subBlockNum,
                                                             groupNum,
                                                             linkArrangement,
                                                             endpointsPerServer,
                                                             nicsPerAswitch,
                                                             dimLink);
                }
                else if (linkArrangement == "Contiguous" || linkArrangement == "Default")
                {
                    t = CreateObject<ClosInterLevelTemplate>(levelId,
                                                             dimId,
                                                             nodeNum,
                                                             subBlockNum,
                                                             groupNum,
                                                             dimLink);
                }
                else
                {
                    NS_ABORT_MSG("Unknown ClosInterLevel linkArrangement: " << linkArrangement);
                }
                if (hasLoadBalance)
                {
                    t->SetPortSelectPolicy(portPolicy);
                }
                t->SetTopologyHelper(topoHelper);
                builder.AddLevel(t);
            }
            else if (type == "TorusIntraLevel")
            {
                // Important: levelId should not be incremented here
                ++dimId;
                uint32_t nodeNum = dim.at("nodeNum");
                uint32_t subBlockNum = dim.at("subBlockNum");
                std::string linkArrangement = dim.at("LinkArrangement");
                Ptr<TorusIntraLevelTemplate> t =
                    CreateObject<TorusIntraLevelTemplate>(levelId,
                                                          dimId,
                                                          nodeNum,
                                                          subBlockNum,
                                                          linkArrangement,
                                                          dimLink);
                if (hasLoadBalance)
                {
                    t->SetPortSelectPolicy(portPolicy);
                }
                t->SetTopologyHelper(topoHelper);
                builder.AddLevel(t);
            }
            else if (type == "FullIntraLevel")
            {
                // Important: levelId should not be incremented here
                ++dimId;
                uint32_t nodeNum = dim.at("nodeNum");
                uint32_t subBlockNum = dim.at("subBlockNum");
                uint32_t outLinkNum = dim.at("outLinkNum");
                std::string linkArrangement = dim.at("linkArrangement");
                Ptr<FullIntraLevelTemplate> t =
                    CreateObject<FullIntraLevelTemplate>(levelId,
                                                         dimId,
                                                         nodeNum,
                                                         subBlockNum,
                                                         outLinkNum,
                                                         linkArrangement,
                                                         dimLink);
                if (hasLoadBalance)
                {
                    t->SetPortSelectPolicy(portPolicy);
                }
                t->SetTopologyHelper(topoHelper);
                builder.AddLevel(t);
            }
            else
            {
                NS_ABORT_MSG("Unknown template: " << type);
            }
        }
    }

    if (sawRandom)
    {
        return PortSelectPolicy::kRandom;
    }
    if (sawByHash)
    {
        return PortSelectPolicy::kByHash;
    }
    return PortSelectPolicy::kFirst;
}

static Ptr<RuleBasedRouting>
FindRuleBasedRouting(Ptr<Ipv4RoutingProtocol> rp)
{
    if (!rp)
    {
        return nullptr;
    }
    if (auto rbr = DynamicCast<RuleBasedRouting>(rp))
    {
        return rbr;
    }
    if (auto list = DynamicCast<Ipv4ListRouting>(rp))
    {
        for (uint32_t i = 0; i < list->GetNRoutingProtocols(); ++i)
        {
            int16_t priority = 0;
            Ptr<Ipv4RoutingProtocol> sub = list->GetRoutingProtocol(i, priority);
            if (auto rbr = DynamicCast<RuleBasedRouting>(sub))
            {
                return rbr;
            }
        }
    }
    return nullptr;
}

static void
ApplyNonMinimalPolicyFromJson(const json& cfg, Ptr<StructuredTopology> topo)
{
    if (!cfg.contains("nonMinimal"))
    {
        return;
    }
    const auto& nm = cfg.at("nonMinimal");
    bool enabled = nm.value("enable", true);
    if (!enabled)
    {
        return;
    }

    std::string algorithm = nm.value("algorithm", "Valiant");
    Ptr<NonMinimalPolicy> policy;
    if (algorithm == "Valiant" || algorithm == "VAL" || algorithm == "valiant")
    {
        policy = CreateObject<ValiantPolicy>();
    }
    else if (algorithm == "UGAL" || algorithm == "Ugal" || algorithm == "ugal")
    {
        Ptr<UgalPolicy> ugal = CreateObject<UgalPolicy>();
        double alpha = nm.value("alpha", 1.0);
        ugal->SetAlpha(alpha);
        policy = ugal;
    }
    else if (algorithm == "Detour" || algorithm == "DET" || algorithm == "detour")
    {
        Ptr<DetourPolicy> detour = CreateObject<DetourPolicy>();
        uint32_t stages = nm.value("detourStages", 1);
        if (stages == 0)
        {
            stages = 1;
        }
        detour->SetStages(static_cast<uint8_t>(std::min<uint32_t>(stages, 255)));
        policy = detour;
    }
    else
    {
        NS_ABORT_MSG("Unknown nonMinimal algorithm: " << algorithm);
    }

    uint64_t nmSeed = 1;
    if (nm.contains("seed"))
    {
        nmSeed = nm.at("seed").get<uint64_t>();
    }
    policy->SetSeed(nmSeed);

    if (nm.contains("transitFields"))
    {
        std::vector<uint16_t> fields;
        for (const auto& f : nm.at("transitFields"))
        {
            fields.push_back(f.get<uint16_t>());
        }
        policy->SetTransitFields(fields);
    }

    if (auto ugal = DynamicCast<UgalPolicy>(policy))
    {
        double detourPenalty = nm.value("detourPenalty", 1.0);
        ugal->SetDetourPenalty(detourPenalty);
    }

    auto provider = std::make_shared<NetDeviceQueueSignalProvider>();
    std::string metric = nm.value("metric", "bytes");
    if (metric == "bytes")
    {
        provider->SetMetric(QueueMetric::kBytes);
    }
    else if (metric == "packets")
    {
        provider->SetMetric(QueueMetric::kPackets);
    }
    else if (metric == "none")
    {
        provider->SetMetric(QueueMetric::kNone);
    }
    else
    {
        NS_ABORT_MSG("Unknown nonMinimal metric: " << metric);
    }

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
            Ptr<RuleBasedRouting> rbr = FindRuleBasedRouting(ipv4->GetRoutingProtocol());
            if (!rbr)
            {
                continue;
            }
            rbr->SetNonMinimalPolicy(policy);
            rbr->SetCongestionSignalProvider(provider);
        }
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

static Time
ParseTimeValue(double value, const std::string& unit)
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
    NS_FATAL_ERROR("Unknown time unit: " << unit);
    return Seconds(0);
}

struct LinkEndpoint
{
    uint32_t level{0};
    uint32_t local{0};
    uint32_t nodeId{0};
};

struct Link
{
    LinkEndpoint a;
    LinkEndpoint b;
};

static std::vector<Link>
CollectAllLinks(Ptr<StructuredTopology> topo)
{
    std::vector<Link> links;
    if (!topo)
    {
        return links;
    }

    std::unordered_map<uint32_t, LinkEndpoint> nodeMap;
    for (uint32_t levelId = 0; levelId < topo->GetNumLevels(); ++levelId)
    {
        const NodeContainer& levelNodes = topo->GetLevel(levelId);
        for (uint32_t localIdx = 0; localIdx < levelNodes.GetN(); ++localIdx)
        {
            Ptr<Node> node = levelNodes.Get(localIdx);
            if (!node)
            {
                continue;
            }
            nodeMap[node->GetId()] = LinkEndpoint{levelId, localIdx, node->GetId()};
        }
    }

    std::unordered_set<uint64_t> seen;
    auto makeKey = [](uint32_t a, uint32_t b) -> uint64_t {
        uint32_t lo = std::min(a, b);
        uint32_t hi = std::max(a, b);
        return (static_cast<uint64_t>(lo) << 32) | hi;
    };

    for (uint32_t levelId = 0; levelId < topo->GetNumLevels(); ++levelId)
    {
        const NodeContainer& levelNodes = topo->GetLevel(levelId);
        for (uint32_t localIdx = 0; localIdx < levelNodes.GetN(); ++localIdx)
        {
            Ptr<Node> node = levelNodes.Get(localIdx);
            if (!node)
            {
                continue;
            }
            uint32_t nodeId = node->GetId();
            for (uint32_t ifIdx = 1; ifIdx < node->GetNDevices(); ++ifIdx)
            {
                Ptr<NetDevice> dev = node->GetDevice(ifIdx);
                Ptr<PointToPointNetDevice> p2p = DynamicCast<PointToPointNetDevice>(dev);
                if (!p2p)
                {
                    continue;
                }
                Ptr<PointToPointChannel> ch = DynamicCast<PointToPointChannel>(p2p->GetChannel());
                if (!ch)
                {
                    continue;
                }
                Ptr<NetDevice> peerDev = nullptr;
                if (ch->GetNDevices() >= 2)
                {
                    peerDev = (ch->GetDevice(0) == dev) ? ch->GetDevice(1) : ch->GetDevice(0);
                }
                if (!peerDev)
                {
                    continue;
                }
                Ptr<Node> peerNode = peerDev->GetNode();
                if (!peerNode)
                {
                    continue;
                }
                uint32_t peerId = peerNode->GetId();
                if (peerId == nodeId)
                {
                    continue;
                }
                uint64_t key = makeKey(nodeId, peerId);
                if (!seen.insert(key).second)
                {
                    continue;
                }
                auto itA = nodeMap.find(nodeId);
                auto itB = nodeMap.find(peerId);
                if (itA == nodeMap.end() || itB == nodeMap.end())
                {
                    continue;
                }
                // Skip host-switch links: only allow switch-switch failures (level > 0 on both ends).
                if (itA->second.level == 0 || itB->second.level == 0)
                {
                    continue;
                }
                links.push_back(Link{itA->second, itB->second});
            }
        }
    }

    return links;
}

int
main(int argc, char* argv[])
{
    // Configure logging
    LogComponentEnable("ConstructorExample", LOG_LEVEL_INFO);
    LogComponentEnable("FailureHelper", LOG_LEVEL_INFO);

    CommandLine cmd;
    std::string configFile = "clos.json";
    std::string failureConfig = ""; // Optional failure configuration file
    bool failurePreApply = false;
    const std::filesystem::path defaultConfDir = "src/datacenter/examples/inputs";
    const std::filesystem::path defaultFailureDir = "src/datacenter/examples/inputs/failures";
    std::string routing = "RuleBased";
    bool isDebug = false;
    // uint64_t dataSize = 1000; // 1MB
    uint64_t dataSize = 1048576; // 1MB
   //  uint64_t dataSize = 409600; // 400KB
    std::string dataRate = "100Gbps";
    std::string trafficPattern = "allreduce"; // "allreduce", "grouped-allreduce", "alltoall", "flows", or "trace"
    std::string trafficReplayMode = "batch"; // "onoff" or "batch"
    uint32_t numFlows = 10; // Number of flows for "flows" pattern
    uint64_t flowSize = 1048576; // Flow size in bytes (1MB)
    uint32_t packetSize = 1000;
    uint32_t allreduceGroupSize = 8;
    std::string allreducePlacement = "strided";
    double allreduceStepGap = 0.0;
    std::string alltoallPattern = "sequential";
    uint32_t alltoallSourceBatchSize = 0;
    double alltoallBatchGap = 0.0;
    std::string alltoallBatchGapUnit = "us";
    double alltoallRoundGap = 0.0;
    std::string alltoallRoundGapUnit = "us";
    std::string trafficTrace = "";
    uint32_t trafficTraceMaxFlows = 0;
    double trafficTraceTimeScale = 1.0;
    double trafficStartOffset = 1.0;
    double trafficTraceStopPadding = 10.0;
    double randomFailureRate = 0.0; // fraction of links to fail (e.g., 0.0001 for 0.01%)
    double randomFailureTime = 0.5;
    std::string randomFailureTimeUnit = "s";
    uint32_t randomFailureSeed = 1;
    std::string randomFailureOut = "";
    cmd.AddValue("config", "Path to JSON topology config", configFile);
    cmd.AddValue("failure", "Path to JSON failure config (optional)", failureConfig);
    cmd.AddValue("failurePreApply",
                 "Apply failure config immediately before routing computation (NodeBfs only)",
                 failurePreApply);
    cmd.AddValue("debug", "Enable debug mode", isDebug);
    cmd.AddValue("routing", "Routing algorithm", routing);
    cmd.AddValue("dataSize", "Data size", dataSize);
    cmd.AddValue("dataRate", "Data rate", dataRate);
    cmd.AddValue("trafficPattern",
                 "Traffic pattern: allreduce, grouped-allreduce, alltoall, flows, or trace",
                 trafficPattern);
    cmd.AddValue("trafficReplayMode",
                 "Trace replay mode for trafficPattern=trace: onoff or batch",
                 trafficReplayMode);
    cmd.AddValue("numFlows", "Number of flows (for flows pattern)", numFlows);
    cmd.AddValue("flowSize", "Flow size in bytes (for flows pattern)", flowSize);
    cmd.AddValue("packetSize", "Application packet size in bytes", packetSize);
    cmd.AddValue("allreduceGroupSize",
                 "Group size for trafficPattern=grouped-allreduce (0 means all hosts)",
                 allreduceGroupSize);
    cmd.AddValue("allreducePlacement",
                 "Rank placement for trafficPattern=grouped-allreduce: contiguous or strided",
                 allreducePlacement);
    cmd.AddValue("allreduceStepGap",
                 "Seconds between grouped-allreduce logical ring steps",
                 allreduceStepGap);
    cmd.AddValue("alltoallPattern",
                 "AllToAll batch replay pattern: sequential or round_robin",
                 alltoallPattern);
    cmd.AddValue("alltoallSourceBatchSize",
                 "Number of AllToAll source ranks to start per batch in batch replay (0 means all sources)",
                 alltoallSourceBatchSize);
    cmd.AddValue("alltoallBatchGap",
                 "Gap between AllToAll source batches, interpreted with alltoallBatchGapUnit",
                 alltoallBatchGap);
    cmd.AddValue("alltoallBatchGapUnit",
                 "AllToAll source batch gap unit (s/ms/us/ns)",
                 alltoallBatchGapUnit);
    cmd.AddValue("alltoallRoundGap",
                 "Extra gap between AllToAll destination rounds, interpreted with alltoallRoundGapUnit",
                 alltoallRoundGap);
    cmd.AddValue("alltoallRoundGapUnit",
                 "AllToAll destination round gap unit (s/ms/us/ns)",
                 alltoallRoundGapUnit);
    cmd.AddValue("trafficTrace",
                 "CSV traffic trace for trafficPattern=trace: start_s,src,dst,bytes[,tag]",
                 trafficTrace);
    cmd.AddValue("trafficTraceMaxFlows",
                 "Maximum number of traffic trace flows to load (0 means all)",
                 trafficTraceMaxFlows);
    cmd.AddValue("trafficTraceTimeScale",
                 "Scale applied to start_s values from trafficTrace",
                 trafficTraceTimeScale);
    cmd.AddValue("trafficStartOffset",
                 "Seconds added to traffic start times after scaling",
                 trafficStartOffset);
    cmd.AddValue("trafficTraceStopPadding",
                 "Seconds after the last trace start time before trace applications stop",
                 trafficTraceStopPadding);
    cmd.AddValue("randomFailureRate", "Random failure ratio over links (0-1)", randomFailureRate);
    cmd.AddValue("randomFailureTime", "Random failure trigger time value", randomFailureTime);
    cmd.AddValue("randomFailureTimeUnit", "Random failure time unit (s/ms/us/ns)", randomFailureTimeUnit);
    cmd.AddValue("randomFailureSeed", "Random failure RNG seed", randomFailureSeed);
    cmd.AddValue("randomFailureOut",
                 "Output JSON filename for random failures (saved under inputs/failures if relative)",
                 randomFailureOut);
    cmd.Parse(argc, argv);



    if (trafficPattern != "allreduce" && trafficPattern != "grouped-allreduce" &&
        trafficPattern != "alltoall" &&
        trafficPattern != "flows" && trafficPattern != "trace")
    {
        NS_FATAL_ERROR("Unknown traffic pattern: " << trafficPattern
                                                  << ". Use allreduce, grouped-allreduce, alltoall, flows, or trace.");
    }
    if (trafficReplayMode != "onoff" && trafficReplayMode != "batch")
    {
        NS_FATAL_ERROR("Unknown traffic replay mode: " << trafficReplayMode
                                                       << ". Use onoff or batch.");
    }
    if (packetSize == 0)
    {
        NS_FATAL_ERROR("packetSize must be greater than zero.");
    }
    if (alltoallPattern != "sequential" && alltoallPattern != "round_robin")
    {
        NS_FATAL_ERROR("Unknown alltoallPattern: " << alltoallPattern
                                                   << ". Use sequential or round_robin.");
    }
    if (trafficTraceTimeScale < 0.0 || trafficStartOffset < 0.0 || trafficTraceStopPadding < 0.0)
    {
        NS_FATAL_ERROR("Trace timing arguments must be non-negative.");
    }
    if (alltoallBatchGap < 0.0)
    {
        NS_FATAL_ERROR("alltoallBatchGap must be non-negative.");
    }
    if (alltoallRoundGap < 0.0)
    {
        NS_FATAL_ERROR("alltoallRoundGap must be non-negative.");
    }
    Time alltoallBatchGapTime = ParseTimeValue(alltoallBatchGap, alltoallBatchGapUnit);
    Time alltoallRoundGapTime = ParseTimeValue(alltoallRoundGap, alltoallRoundGapUnit);

    std::cout << "Routing algorithm: " << routing << std::endl;

    // Generate trace filename based on config file
    std::string packetTraceFilename;
    std::string flowTraceFilename;
    if (isDebug)
    {
        // Extract filename without path and extension from configFile
        std::filesystem::path configPath(configFile);
        std::string configStem = configPath.stem().string(); // Get filename without extension

        // Include failure config in filename if provided
        std::string failureSuffix = "";
        if (!failureConfig.empty())
        {
            std::filesystem::path failurePath(failureConfig);
            failureSuffix = "_" + failurePath.stem().string();
        }

        packetTraceFilename =
            "./src/datacenter/examples/traces/" + configStem + "_" + routing + failureSuffix + "_packet_trace.txt";
        flowTraceFilename =
            "./src/datacenter/examples/traces/" + configStem + "_" + routing + failureSuffix + "_flow_trace.txt";
        std::filesystem::create_directory("./src/datacenter/examples/traces");
        // Open packet trace file
        packetTraceFile.open(packetTraceFilename);
        flowTraceFile.open(flowTraceFilename);
        if (!packetTraceFile.is_open() || !flowTraceFile.is_open())
        {
            NS_LOG_ERROR("Failed to open packet trace file or flow trace file!");
            return 1;
        }

        // Write trace file header
        packetTraceFile << "# Packet Trace" << std::endl;
        packetTraceFile << "# Time(s)\tEvent\tInterface\tDetails" << std::endl;
        packetTraceFile << "# ==========================================" << std::endl;
    }

    // Parse JSON. When the user passes only a filename, fall back to the default inputs dir.
    std::filesystem::path confPath(configFile);
    if (!confPath.is_absolute() && confPath.parent_path().empty())
    {
        confPath = defaultConfDir / confPath;
    }
    std::ifstream ifs(confPath);
    if (!ifs.is_open())
    {
        NS_FATAL_ERROR("Cannot open config file: " << confPath);
    }
    json cfg;
    ifs >> cfg;

    // Prepare base components
    std::shared_ptr<TopologyHelper> topoHelper = std::make_shared<TopologyHelper>();
    RuleBasedRoutingHelper ruleBasedRoutingHelper;
    NodeBfsRoutingHelper nodeBfsRoutingHelper;
    Ipv4GlobalRoutingHelper globalRoutingHelper;

    std::cout << routing << std::endl;

    if (routing == "Global")
    {
        topoHelper->GetInternetStack().SetRoutingHelper(globalRoutingHelper);
    }
    else if (routing == "NodeBfs" || routing == "NodeBfsWithHost" || routing == "NodeBfsStrict")
    {
        topoHelper->GetInternetStack().SetRoutingHelper(nodeBfsRoutingHelper);
    }
    else if (routing == "RuleBased")
    {
        topoHelper->GetInternetStack().SetRoutingHelper(ruleBasedRoutingHelper);
    }
    TopologyBuilder builder;
    PortSelectPolicy nodeBfsPolicy = BuildFromJson(cfg, builder, topoHelper);
    Ptr<StructuredTopology> topo = CreateObject<StructuredTopology>(topoHelper);
    builder.Build(topo);
    topo->RegisterAddresses();

    std::filesystem::path failurePath;
    const bool hasFailureConfig = !failureConfig.empty();
    if (hasFailureConfig)
    {
        failurePath = std::filesystem::path(failureConfig);
        if (!failurePath.is_absolute() && failurePath.parent_path().empty())
        {
            failurePath = defaultFailureDir / failurePath;
        }
    }

    if (failurePreApply && hasFailureConfig)
    {
        std::cout << "Pre-applying failure configuration from: " << failurePath << std::endl;
        const bool suppressBfs =
            (routing == "NodeBfs" || routing == "NodeBfsWithHost" || routing == "NodeBfsStrict");
        if (suppressBfs)
        {
            NodeBfsRoutingHelper::SuppressRecalculate(true);
        }
        FailureHelper::ApplyFailuresFromJsonNow(failurePath.string(), topo);
        if (suppressBfs)
        {
            NodeBfsRoutingHelper::SuppressRecalculate(false);
        }
    }
    if (routing == "RuleBased")
    {
        ruleBasedRoutingHelper.Initialize(*topo);
        ApplyNonMinimalPolicyFromJson(cfg, topo);
    }
    else if (routing == "Global")
    {
        Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    }
    else if (routing == "NodeBfs")
    {
        NodeBfsRoutingHelper::CalculateRoutes(topo);
        NodeBfsRoutingHelper::SetRoutingEntries(topo);
        NodeBfsRoutingHelper::SetEcmpPolicy(topo, nodeBfsPolicy);
    }
    else if (routing == "NodeBfsStrict")
    {
        NodeBfsRoutingHelper::EnableStrictBaseline(topo, true, false);
        NodeBfsRoutingHelper::CalculateRoutes(topo);
        NodeBfsRoutingHelper::SetRoutingEntries(topo);
        NodeBfsRoutingHelper::SetEcmpPolicy(topo, nodeBfsPolicy);
    }
    else if (routing == "NodeBfsWithHost")
    {
        NodeBfsRoutingHelper::CalculateRoutesWithHost(topo);
        NodeBfsRoutingHelper::SetRoutingEntries(topo);
        NodeBfsRoutingHelper::SetEcmpPolicy(topo, nodeBfsPolicy);
    }

    // Load failure configuration if provided (skip if pre-applied)
    if (hasFailureConfig && !failurePreApply)
    {
        std::cout << "Loading failure configuration from: " << failurePath << std::endl;
        FailureHelper failureHelper;
        failureHelper.LoadFailuresFromJson(failurePath.string(), topo);
        std::cout << "Failure events loaded and scheduled." << std::endl;
    }

    if (randomFailureRate > 0.0)
    {
        auto links = CollectAllLinks(topo);
        if (links.empty())
        {
            NS_LOG_WARN("Random failure requested but no links found.");
        }
        else
        {
            std::mt19937 rng(randomFailureSeed);
            std::shuffle(links.begin(), links.end(), rng);
            const double rate = std::max(0.0, std::min(1.0, randomFailureRate));
            const size_t total = links.size();
            size_t failCount = static_cast<size_t>(std::ceil(total * rate));
            if (failCount > total)
            {
                failCount = total;
            }
            Time failTime = ParseTimeValue(randomFailureTime, randomFailureTimeUnit);
            std::cout << "Random failure: total_links=" << total
                      << " rate=" << rate
                      << " selected=" << failCount
                      << " time=" << failTime.GetSeconds() << "s"
                      << " seed=" << randomFailureSeed << std::endl;

            for (size_t i = 0; i < failCount; ++i)
            {
                const Link& link = links[i];
                Simulator::Schedule(failTime,
                                    &FailureHelper::SetLinkDown,
                                    topo,
                                    link.a.level,
                                    link.a.local,
                                    link.b.level,
                                    link.b.local);
            }

            if (!randomFailureOut.empty())
            {
                std::filesystem::path outPath(randomFailureOut);
                if (!outPath.is_absolute() && outPath.parent_path().empty())
                {
                    outPath = defaultFailureDir / outPath;
                }
                if (!outPath.parent_path().empty())
                {
                    std::filesystem::create_directories(outPath.parent_path());
                }

                json failures;
                failures["failures"] = json::array();
                for (size_t i = 0; i < failCount; ++i)
                {
                    const Link& link = links[i];
                    json entry;
                    entry["link"] = {
                        {"src", {{"level", link.a.level}, {"local_index", link.a.local}}},
                        {"dst", {{"level", link.b.level}, {"local_index", link.b.local}}}
                    };
                    entry["failure_time"] = randomFailureTime;
                    entry["failure_time_unit"] = randomFailureTimeUnit;
                    failures["failures"].push_back(std::move(entry));
                }

                std::ofstream ofs(outPath);
                if (ofs.is_open())
                {
                    ofs << failures.dump(2) << std::endl;
                    std::cout << "Random failure JSON saved to: " << outPath << std::endl;
                }
                else
                {
                    NS_LOG_WARN("Failed to write random failure JSON to " << outPath);
                }
            }
        }
    }

    // Optional: Trace, application, run simulation
    if (isDebug)
    {
        RegisterTrace(topo);

        // Print topology information for visualization
        topo->PrintTopologyInfo();
    }

    // Ptr<StructuredAddressDirectory> dir = StructuredAddressDirectory::Get();

    NS_LOG_INFO("Create allreduce traffic.");
    const NodeContainer& hostNodes = topo->GetLevel(0);
    uint32_t hostNum = hostNodes.GetN();

    std::cout << "Host number: " << hostNum << std::endl;
    std::cout << "Traffic pattern: " << trafficPattern << std::endl;
    std::cout << "Traffic replay mode: " << trafficReplayMode << std::endl;

    uint16_t port = 9;
    uint32_t flowIdCounter = 0;
    std::vector<TrafficFlow> trafficFlows;
    const bool streamAllToAll = trafficPattern == "alltoall" && trafficReplayMode == "batch";
    const bool roundRobinAllToAll = streamAllToAll && alltoallPattern == "round_robin";
    uint64_t logicalTrafficFlowCount = 0;
    uint64_t allToAllChunkBytes = 0;
    uint32_t allToAllEffectiveSourceBatchSize = 0;
    uint32_t allToAllSourceBatchCount = 0;

    if (trafficPattern == "trace")
    {
        if (trafficTrace.empty())
        {
            NS_FATAL_ERROR("trafficPattern=trace requires --trafficTrace=<csv>");
        }
        trafficFlows = LoadTrafficTrace(trafficTrace,
                                        hostNum,
                                        trafficTraceMaxFlows,
                                        trafficTraceTimeScale,
                                        trafficStartOffset);
        logicalTrafficFlowCount = trafficFlows.size();
        std::cout << "Traffic trace: " << trafficTrace << std::endl;
        std::cout << "Traffic trace flows: " << trafficFlows.size() << std::endl;
    }
    else if (trafficPattern == "allreduce")
    {
        trafficFlows = GenerateRingAllReduceTraffic(hostNum, dataSize);
        logicalTrafficFlowCount = trafficFlows.size();
    }
    else if (trafficPattern == "grouped-allreduce")
    {
        trafficFlows = GenerateGroupedRingAllReduceTraffic(hostNum,
                                                           dataSize,
                                                           allreduceGroupSize,
                                                           allreducePlacement,
                                                           allreduceStepGap);
        logicalTrafficFlowCount = trafficFlows.size();
    }
    else if (trafficPattern == "alltoall")
    {
        if (hostNum < 2)
        {
            NS_FATAL_ERROR("alltoall traffic requires at least two hosts.");
        }
        logicalTrafficFlowCount = static_cast<uint64_t>(hostNum) * static_cast<uint64_t>(hostNum - 1);
        allToAllChunkBytes = ChunkBytesPerRank(dataSize, hostNum);
        if (streamAllToAll)
        {
            allToAllEffectiveSourceBatchSize = alltoallSourceBatchSize == 0
                                                  ? hostNum
                                                  : std::min(alltoallSourceBatchSize, hostNum);
            allToAllSourceBatchCount =
                (hostNum + allToAllEffectiveSourceBatchSize - 1) / allToAllEffectiveSourceBatchSize;
            const std::string generatedPattern = roundRobinAllToAll
                                                     ? "round-robin-alltoall-streaming"
                                                     : "dense-alltoall-streaming";
            std::cout << "Traffic generated: pattern=" << generatedPattern
                      << " hosts=" << hostNum
                      << " flows=" << logicalTrafficFlowCount
                      << " chunk_bytes=" << allToAllChunkBytes
                      << " source_streams=" << hostNum
                      << " alltoall_pattern=" << alltoallPattern
                      << " source_batch_size=" << allToAllEffectiveSourceBatchSize
                      << " source_batches=" << allToAllSourceBatchCount
                      << " batch_gap_s=" << alltoallBatchGapTime.GetSeconds()
                      << " round_gap_s=" << alltoallRoundGapTime.GetSeconds() << std::endl;
        }
        else
        {
            trafficFlows = GenerateDenseAllToAllTraffic(hostNum, dataSize);
        }
    }
    else if (trafficPattern == "flows")
    {
        trafficFlows = GenerateFlowTraffic(hostNum, numFlows, flowSize);
        logicalTrafficFlowCount = trafficFlows.size();
    }
    else
    {
        NS_FATAL_ERROR("Unsupported traffic pattern after validation: " << trafficPattern);
    }
    double maxStart = 1.0;
    for (const auto& flow : trafficFlows)
    {
        maxStart = std::max(maxStart, flow.startSeconds);
    }
    if (streamAllToAll && allToAllSourceBatchCount > 1)
    {
        maxStart += alltoallBatchGapTime.GetSeconds() *
                    static_cast<double>(allToAllSourceBatchCount - 1);
    }
    if (streamAllToAll && hostNum > 2)
    {
        maxStart += alltoallRoundGapTime.GetSeconds() * static_cast<double>(hostNum - 2);
    }
    const double sourceStopTime = trafficPattern == "trace"
                                      ? (maxStart + trafficTraceStopPadding)
                                      : std::max(5.0, maxStart + 4.0);
    const double sinkStopTime = sourceStopTime + 0.5;

    for (uint32_t i = 0; i < hostNum; i++)
    {
        Ptr<Node> host = hostNodes.Get(i);
        PacketSinkHelper sink("ns3::UdpSocketFactory",
                              Address(InetSocketAddress(Ipv4Address::GetAny(), port)));
        ApplicationContainer apps = sink.Install(host);
        for (uint32_t appIdx = 0; appIdx < apps.GetN(); ++appIdx)
        {
        }
        apps.Start(Seconds(0.0));
        apps.Stop(Seconds(sinkStopTime));
    }

    if (trafficReplayMode == "batch")
    {
        std::vector<Ptr<Socket>> sourceSockets(hostNum);
        DataRate replayRate(dataRate);

        if (streamAllToAll)
        {
            auto hostIps = std::make_shared<std::vector<Ipv4Address>>();
            hostIps->reserve(hostNum);
            for (uint32_t i = 0; i < hostNum; ++i)
            {
                Ptr<Node> host = hostNodes.Get(i);
                Ptr<Ipv4> ipv4 = host->GetObject<Ipv4>();
                hostIps->push_back(ipv4->GetAddress(1, 0).GetLocal());
            }

            for (uint32_t src = 0; src < hostNum; ++src)
            {
                Ptr<Node> host = hostNodes.Get(src);
                Ptr<Ipv4> ipv4 = host->GetObject<Ipv4>();
                Ipv4Address clientIp = ipv4->GetAddress(1, 0).GetLocal();

                Ptr<Socket> socket = Socket::CreateSocket(host, UdpSocketFactory::GetTypeId());
                if (socket->Bind(InetSocketAddress(clientIp, 0)) == -1)
                {
                    NS_FATAL_ERROR("Failed to bind alltoall stream UDP socket for host " << src);
                }
                socket->SetAllowBroadcast(true);
                socket->ShutdownRecv();
                sourceSockets[src] = socket;
                Simulator::Schedule(Seconds(sinkStopTime),
                                    &CloseTraceReplaySocket,
                                    sourceSockets[src]);

                auto streamState = std::make_shared<AllToAllStreamState>(sourceSockets[src],
                                                                         hostIps,
                                                                         src,
                                                                         allToAllChunkBytes,
                                                                         packetSize,
                                                                         replayRate,
                                                                         port,
                                                                         roundRobinAllToAll,
                                                                         alltoallRoundGapTime);
                uint32_t sourceBatch = src / allToAllEffectiveSourceBatchSize;
                Time streamStart = Seconds(1.0) +
                                   NanoSeconds(alltoallBatchGapTime.GetNanoSeconds() *
                                               static_cast<int64_t>(sourceBatch));
                Simulator::ScheduleWithContext(host->GetId(),
                                               streamStart,
                                               &SendAllToAllStreamPacket,
                                               streamState);
            }
        }
        else
        {
            for (const auto& flow : trafficFlows)
            {
                if (!sourceSockets[flow.src])
                {
                    Ptr<Node> host = hostNodes.Get(flow.src);
                    Ptr<Ipv4> ipv4 = host->GetObject<Ipv4>();
                    Ipv4Address clientIp = ipv4->GetAddress(1, 0).GetLocal();

                    Ptr<Socket> socket = Socket::CreateSocket(host, UdpSocketFactory::GetTypeId());
                    if (socket->Bind(InetSocketAddress(clientIp, 0)) == -1)
                    {
                        NS_FATAL_ERROR("Failed to bind trace replay UDP socket for host " << flow.src);
                    }
                    socket->SetAllowBroadcast(true);
                    socket->ShutdownRecv();
                    sourceSockets[flow.src] = socket;
                    Simulator::Schedule(Seconds(sinkStopTime),
                                        &CloseTraceReplaySocket,
                                        sourceSockets[flow.src]);
                }

                Ptr<Node> srcHost = hostNodes.Get(flow.src);
                Ptr<Node> dst = hostNodes.Get(flow.dst);
                Ptr<Ipv4> dstIpv4 = dst->GetObject<Ipv4>();
                Ipv4Address dstIp = dstIpv4->GetAddress(1, 0).GetLocal();
                Simulator::ScheduleWithContext(srcHost->GetId(),
                                               Seconds(flow.startSeconds),
                                               &SendTraceReplayPacket,
                                               sourceSockets[flow.src],
                                               dstIp,
                                               port,
                                               flow.bytes,
                                               packetSize,
                                               replayRate);
            }
        }
    }
    else
    {
        for (const auto& flow : trafficFlows)
        {
            Ptr<Node> host = hostNodes.Get(flow.src);
            Ptr<Ipv4> ipv4 = host->GetObject<Ipv4>();
            Ipv4Address clientIp = ipv4->GetAddress(1, 0).GetLocal();

            Ptr<Node> dst = hostNodes.Get(flow.dst);
            Ptr<Ipv4> dstIpv4 = dst->GetObject<Ipv4>();
            OnOffHelper onoff("ns3::UdpSocketFactory",
                              InetSocketAddress(dstIpv4->GetAddress(1, 0).GetLocal(), port));
            onoff.SetConstantRate(DataRate(dataRate));
            onoff.SetAttribute("PacketSize", UintegerValue(packetSize));
            onoff.SetAttribute("MaxBytes", UintegerValue(flow.bytes));
            onoff.SetAttribute("Local", AddressValue(AddressValue(InetSocketAddress(clientIp, 0))));

            ApplicationContainer sourceApps = onoff.Install(host);

            // 为每个OnOffApplication连接Tx trace以记录流开始
            if (isDebug && flowTraceFile.is_open())
            {
                for (uint32_t j = 0; j < sourceApps.GetN(); ++j)
                {
                    Ptr<Application> app = sourceApps.Get(j);
                    Ptr<OnOffApplication> onoffApp = DynamicCast<OnOffApplication>(app);
                    if (onoffApp)
                    {
                        uint32_t currentFlowId = flowIdCounter++;

                        flowStartTimes[currentFlowId] = flow.startSeconds;
                        flowSrcNodes[currentFlowId] = host->GetId();
                        flowDstNodes[currentFlowId] = dst->GetId();
                        flowActualSentBytes[currentFlowId] = 0;

                        flowTraceFile << "# Flow " << currentFlowId << " STARTED: Node "
                                      << host->GetId() << " -> Node " << dst->GetId()
                                      << " | Configured Size: " << flow.bytes << " bytes ("
                                      << (flow.bytes / 1024.0 / 1024.0) << " MB)"
                                      << " | Start Time: " << flow.startSeconds << "s"
                                      << " | Trace Src/Dst: " << flow.src << " -> " << flow.dst
                                      << " | Tag: " << flow.tag << std::endl;

                        onoffApp->TraceConnectWithoutContext(
                            "Tx",
                            MakeBoundCallback(&OnOffTxPacketTrace, currentFlowId));

                        Simulator::Schedule(Seconds(std::max(flow.startSeconds, sourceStopTime - 0.1)),
                                            MakeEvent(&FlowStopTrace, currentFlowId, flow.bytes));
                    }
                }
            }

            sourceApps.Start(Seconds(flow.startSeconds));
            sourceApps.Stop(Seconds(sourceStopTime));
        }
    }

    // auto install_graph_traffic = [&](uint32_t graph_degree) {
    //     SymmetricDegreeGraph logicalGraph(hostNum,
    //                                       graph_degree, // degree means logical topology degree
    //                                       0,
    //                                       false,
    //                                       false,
    //                                       1,
    //                                       1);

    //     logicalGraph.GenerateGraph();

    //     uint16_t port = 9;
    //     uint32_t flowIdCounter = 0;

    //     for (uint32_t i = 0; i < hostNum; i++)
    //     {
    //         Ptr<Node> host = hostNodes.Get(i);
    //         Ptr<Ipv4> ipv4 = host->GetObject<Ipv4>();

    //         std::vector<uint32_t> dsts = logicalGraph.GetDsts(i);

    //         Ipv4Address clientIp = host->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal();

    //         for (auto& dstId : dsts)
    //         {
    //             NS_ASSERT_MSG(i != dstId,
    //                           "Self-loop detected! Nodes should not send traffic to themselves.");

    //             Ptr<Node> dst = hostNodes.Get(dstId);
    //             Ptr<Ipv4> dstIpv4 = dst->GetObject<Ipv4>();
    //             OnOffHelper onoff("ns3::UdpSocketFactory",
    //                               InetSocketAddress(dstIpv4->GetAddress(1, 0).GetLocal(), port));
    //             onoff.SetConstantRate(DataRate(dataRate));
    //             onoff.SetAttribute("PacketSize", UintegerValue(1000));
    //             onoff.SetAttribute("MaxBytes", UintegerValue(dataSize));
    //             onoff.SetAttribute("Local", AddressValue(AddressValue(InetSocketAddress(clientIp, 0))));

    //             ApplicationContainer sourceApps = onoff.Install(host);

    //             // 为每个OnOffApplication连接Tx trace以记录流开始
    //             if (isDebug && flowTraceFile.is_open())
    //             {
    //                 for (uint32_t j = 0; j < sourceApps.GetN(); ++j)
    //                 {
    //                     Ptr<Application> app = sourceApps.Get(j);
    //                     Ptr<OnOffApplication> onoffApp = DynamicCast<OnOffApplication>(app);
    //                     if (onoffApp)
    //                     {
    //                         uint32_t currentFlowId = flowIdCounter++;

    //                         double startTime = Simulator::Now().GetSeconds();
    //                         flowStartTimes[currentFlowId] = startTime;
    //                         flowSrcNodes[currentFlowId] = host->GetId();
    //                         flowDstNodes[currentFlowId] = dst->GetId();
    //                         flowActualSentBytes[currentFlowId] = 0;

    //                         flowTraceFile << "# Flow " << currentFlowId << " STARTED: Node "
    //                                       << host->GetId() << " -> Node " << dst->GetId()
    //                                       << " | Configured Size: " << dataSize << " bytes ("
    //                                       << (dataSize / 1024.0 / 1024.0) << " MB)"
    //                                       << " | Start Time: " << startTime << "s" << std::endl;

    //                         onoffApp->TraceConnectWithoutContext(
    //                             "Tx",
    //                             MakeBoundCallback(&OnOffTxPacketTrace, currentFlowId));

    //                         Simulator::Schedule(Seconds(4.9),
    //                                             MakeEvent(&FlowStopTrace, currentFlowId, dataSize));
    //                     }
    //                 }
    //             }

    //             sourceApps.Start(Seconds(1.0));
    //             sourceApps.Stop(Seconds(5.0));
    //         }

    //         // Create an optional packet sink to receive these packets
    //         PacketSinkHelper sink("ns3::UdpSocketFactory",
    //                               Address(InetSocketAddress(Ipv4Address::GetAny(), port)));
    //         ApplicationContainer apps = sink.Install(host);
    //         apps.Start(Seconds(1.0));
    //         apps.Stop(Seconds(5.5));
    //     }
    // };

    // if (trafficPattern == "allreduce")
    // {
    //     // ========== AllReduce Traffic Pattern ==========
    //     NS_LOG_INFO("Create allreduce traffic.");
    //     install_graph_traffic(degree);
    // }
    // else if (trafficPattern == "alltoall")
    // {
    //     // ========== AllToAll Traffic Pattern ==========
    //     NS_LOG_INFO("Create alltoall traffic.");
    //     install_graph_traffic(degree);
    // }
    // else if (trafficPattern == "flows")
    // {
    //     // ========== Simple Flows Traffic Pattern ==========
    //     NS_LOG_INFO("Create flows traffic.");

    //     if (hostNum < 2)
    //     {
    //         NS_LOG_ERROR("Not enough hosts to deploy applications.");
    //         return 1;
    //     }

    //     // Collect host IP addresses
    //     std::vector<Ipv4Address> hostAddresses;
    //     for (uint32_t i = 0; i < hostNum; ++i)
    //     {
    //         Ptr<Node> host = hostNodes.Get(i);
    //         Ipv4Address hostIp = host->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal();
    //         hostAddresses.push_back(hostIp);
    //     }

    //     // Traffic generation parameters
    //     double simTime = 5.0;
    //     uint32_t actualFlows = std::min(numFlows, hostNum);
    //     uint16_t basePort = 5000;

    //     std::cout << "Number of flows: " << actualFlows << std::endl;
    //     std::cout << "Flow size: " << flowSize << " bytes ("
    //               << (flowSize / 1024.0 / 1024.0) << " MB)" << std::endl;

    //     for (uint32_t flow = 0; flow < actualFlows; ++flow)
    //     {
    //         uint32_t srcIndex = flow % hostNum;
    //         uint32_t dstIndex = (srcIndex + hostNum / 2 + flow) % hostNum;
    //         if (dstIndex == srcIndex)
    //         {
    //             dstIndex = (dstIndex + 1) % hostNum;
    //         }

    //         Address sinkAddress(InetSocketAddress(hostAddresses[dstIndex], basePort + flow));

    //         // Install packet sink on destination
    //         PacketSinkHelper sinkHelper("ns3::TcpSocketFactory", sinkAddress);
    //         ApplicationContainer sinkApp = sinkHelper.Install(hostNodes.Get(dstIndex));
    //         sinkApp.Start(Seconds(0.0));
    //         sinkApp.Stop(Seconds(simTime));

    //         // Install bulk send source
    //         BulkSendHelper sourceHelper("ns3::TcpSocketFactory", sinkAddress);
    //         sourceHelper.SetAttribute("MaxBytes", UintegerValue(flowSize));
    //         ApplicationContainer sourceApp = sourceHelper.Install(hostNodes.Get(srcIndex));
    //         sourceApp.Start(Seconds(0.1 + 0.05 * flow));
    //         sourceApp.Stop(Seconds(simTime));

    //         if (isDebug)
    //         {
    //             std::cout << "Flow " << flow << ": Host " << srcIndex
    //                       << " (Node " << hostNodes.Get(srcIndex)->GetId() << ") -> Host "
    //                       << dstIndex << " (Node " << hostNodes.Get(dstIndex)->GetId()
    //                       << "), Port " << (basePort + flow) << std::endl;
    //         }
    //     }
    // }
    // else
    // {
    //     NS_FATAL_ERROR("Unknown traffic pattern: " << trafficPattern
    //                    << ". Use 'allreduce', 'alltoall', or 'flows'.");
    // }


    // Run simulation
    Simulator::Run();

    {
        if (isDebug && flowTraceFile.is_open())
        {
            flowTraceFile << std::endl;
            flowTraceFile << "# "
                             "Flow_ID\tSource_IP\tDest_IP\tTx_Packets\tRx_Packets\tLost_"
                             "Packets\tTx_Bytes\tRx_Bytes\tThroughput(Mbps)\tDuration(s)"
                          << std::endl;
        }

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

            if (isDebug && flowTraceFile.is_open())
            {
                flowTraceFile << flowId << "\t" << key.src << "\t" << key.dst << "\t"
                              << st.txPackets << "\t" << st.rxPackets << "\t" << lost << "\t"
                              << st.txBytes << "\t" << st.rxBytes << "\t" << std::fixed
                              << std::setprecision(2) << thrMbps << "\t" << std::fixed
                              << std::setprecision(6) << dur << std::endl;
            }
            ++flowId;
        }
    }

    Simulator::Destroy();

    if (isDebug)
    {
        // Close trace file
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
