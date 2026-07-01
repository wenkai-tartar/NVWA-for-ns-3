/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#include "structured-topology.h"

#include "rule-based-routing.h"
#include "structured-address-directory.h"

#include "ns3/channel.h"
#include "ns3/log.h"
#include "ns3/net-device.h"
#include "ns3/node.h"
#include "ns3/structured-topology.h"

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("StructuredTopology");

NS_OBJECT_ENSURE_REGISTERED(StructuredTopology);

TypeId
StructuredTopology::GetTypeId()
{
    static TypeId tid = TypeId("ns3::StructuredTopology")
                            .SetParent<Object>()
                            .SetGroupName("Datacenter")
                            .AddConstructor<StructuredTopology>();
    return tid;
}

StructuredTopology::StructuredTopology()
{
    NS_LOG_FUNCTION(this);

    m_totalAddrBits = 0;
    m_levelStartAddrBit.clear();

    // Initialize TopologyHelper for automatic Internet Stack management
    m_topoHelper = std::make_shared<TopologyHelper>();

    // Create initial endpoint layer (layer 0) with one node
    CreateLevel(1);
}

StructuredTopology::StructuredTopology(std::shared_ptr<TopologyHelper> topoHelper)
{
    NS_LOG_FUNCTION(this);
    m_topoHelper = topoHelper;
    CreateLevel(1);
}

StructuredTopology::~StructuredTopology()
{
    NS_LOG_FUNCTION(this);
    m_levelStartAddrBit.clear();
}

std::shared_ptr<TopologyHelper>
StructuredTopology::GetTopologyHelper() const
{
    return m_topoHelper;
}

void
StructuredTopology::SetTopologyHelper(std::shared_ptr<TopologyHelper> topoHelper)
{
    m_topoHelper = topoHelper;
}

const NodeContainer&
StructuredTopology::GetLevel(uint32_t levelId) const
{
    NS_ASSERT_MSG(levelId < m_levels.size(), "Level out of range");
    return m_levels[levelId];
}

Ptr<Node>
StructuredTopology::GetNode(uint32_t levelId, uint32_t j) const
{
    const auto& level = GetLevel(levelId);
    NS_ASSERT_MSG(j < level.GetN(), "Node index out of range");
    return level.Get(j);
}

Ptr<Node>
StructuredTopology::GetNode(uint32_t nodeId) const
{
    return m_allNodes.Get(nodeId);
}

const NodeContainer&
StructuredTopology::GetAll() const
{
    return m_allNodes;
}

uint32_t
StructuredTopology::GetNumLevels() const
{
    return static_cast<uint32_t>(m_levels.size());
}

uint32_t
StructuredTopology::GetDimCount(uint32_t levelId) const
{
    NS_ASSERT_MSG(levelId < m_dimCounts.size(), "Level out of range");
    return m_dimCounts[levelId];
}

uint32_t
StructuredTopology::GetNumGroups(uint32_t levelId) const
{
    NS_ASSERT_MSG(levelId < m_levelGroups.size(), "Level group index out of range");
    return static_cast<uint32_t>(m_levelGroups[levelId].size());
}

const std::vector<uint32_t>&
StructuredTopology::GetGroupLocalIndices(uint32_t levelId, uint32_t g) const
{
    NS_ASSERT_MSG(levelId < m_levelGroups.size(), "Level group index out of range");
    NS_ASSERT_MSG(g < m_levelGroups[levelId].size(), "Group out of range");
    return m_levelGroups[levelId][g];
}

std::pair<uint32_t, uint32_t>
StructuredTopology::CreateLevel(uint32_t nodeNum)
{
    if (nodeNum <= 0)
    {
        m_totalAddrBits++;
        m_dimCounts[m_levels.size() - 1]++;

        // Ensure m_adj has enough dimensions for the new dim count
        uint32_t levelId = m_levels.size() - 1;
        if (m_adj[levelId].size() <= m_dimCounts[levelId])
        {
            m_adj[levelId].resize(m_dimCounts[levelId] + 1);
            // Initialize the new dimension with empty vectors for each node
            m_adj[levelId].back().resize(m_levels[levelId].GetN());
        }

        return m_levels.empty()
                   ? std::pair<uint32_t, uint32_t>(0, 0)
                   : std::pair<uint32_t, uint32_t>(static_cast<uint32_t>(m_levels.size() - 1),
                                                   m_dimCounts[m_levels.size() - 1]);
    }

    NodeContainer level;
    level.Create(nodeNum);
    m_topoHelper->InstallInternetStack(level);
    m_levelStartAddrBit.push_back(static_cast<int>(m_totalAddrBits) - 1);
    m_totalAddrBits++;

    m_levels.push_back(level);
    m_dimCounts.push_back(0);

    uint32_t levelId = m_levels.size() - 1;
    uint32_t dimId = m_dimCounts[levelId];

    m_levelGroups.emplace_back();
    m_levelGids.emplace_back();
    m_originalSizes.push_back(nodeNum);

    // init adjacency list entry for this level
    m_adj.emplace_back();
    m_adj.back().resize(dimId + 1);
    m_adj.back()[dimId].resize(nodeNum); // each node has its own vector

    m_structuredAddrs.emplace_back();
    m_structuredAddrs.back().resize(nodeNum);

    uint32_t baseGid = m_allNodes.GetN();
    std::vector<uint32_t>& gids = m_levelGids[levelId];
    gids.reserve(nodeNum);
    for (uint32_t i = 0; i < nodeNum; ++i)
    {
        Ptr<Node> n = level.Get(i);
        m_allNodes.Add(n);
        gids.push_back(baseGid + i);
        // Assign local id, assuming m_localIds is global
        while (m_localIds.size() <= baseGid + i)
        {
            m_localIds.push_back(0);
        }
        m_localIds[baseGid + i] = i;
    }

    return std::make_pair(levelId, dimId);
}

std::vector<uint32_t>
StructuredTopology::ReplicateTopologyInPlace(uint32_t copies)
{
    NS_ASSERT_MSG(copies >= 1, "ReplicateTopologyInPlace: copies must be >= 1");

    uint32_t l = GetNumLevels();
    std::vector<uint32_t> origSizes(l, 0);

    // Store current original sizes before replication
    for (uint32_t i = 0; i < l; ++i)
    {
        origSizes[i] = m_originalSizes[i];
    }

    if (copies == 1)
    {
        return origSizes;
    }

    for (uint32_t i = 0; i < l; ++i)
    {
        uint32_t s = origSizes[i];
        if (s == 0)
        {
            continue;
        }

        NodeContainer newLevel = m_levels[i];
        uint32_t addCount = (copies - 1) * s;
        if (addCount > 0)
        {
            NodeContainer extra;
            extra.Create(addCount);
            m_topoHelper->InstallInternetStack(extra);
            for (uint32_t k = 0; k < addCount; ++k)
            {
                Ptr<Node> n = extra.Get(k);
                newLevel.Add(n);
                uint32_t newGid = m_allNodes.GetN();
                m_allNodes.Add(n);
                m_levelGids[i].emplace_back(newGid);
                while (m_localIds.size() <= newGid)
                {
                    m_localIds.push_back(0);
                }
                m_localIds[newGid] = newLevel.GetN() - 1;

                // Ensure adjacency vectors exist for the new node.
                // (Avoid relying on NS_ASSERT in optimized builds.)
                if (m_adj.size() <= i)
                {
                    m_adj.resize(i + 1);
                }
                const uint32_t needDims = m_dimCounts[i] + 1;
                if (m_adj[i].size() < needDims)
                {
                    m_adj[i].resize(needDims);
                }
                for (uint32_t d = 0; d < needDims; ++d)
                {
                    if (m_adj[i][d].size() < newLevel.GetN())
                    {
                        m_adj[i][d].resize(newLevel.GetN());
                    }
                }
            }
        }
        // Important: update m_levels before replicating connection edges so that
        // ConnectNodes boundary checks see the enlarged level size.
        m_levels[i] = newLevel;

        // Ensure structured address has enough space
        if (m_structuredAddrs.size() <= i)
        {
            m_structuredAddrs.resize(i + 1);
        }
        if (m_structuredAddrs[i].size() < m_levels[i].GetN())
        {
            m_structuredAddrs[i].resize(m_levels[i].GetN());
        }

        std::vector<StructuredAddress> originalAddrs;
        originalAddrs.reserve(s);
        for (uint32_t origIdx = 0; origIdx < s; ++origIdx)
        {
            originalAddrs.push_back(m_structuredAddrs[i][origIdx]);
        }

        if (addCount > 0)
        {
            // Append 1 to the structured address for original nodes
            for (uint32_t origIdx = 0; origIdx < s; ++origIdx)
            {
                m_structuredAddrs[i][origIdx].Append(1);
            }

            for (uint32_t d = 0; d <= m_dimCounts[i]; ++d)
            {
                for (uint32_t copyIdx = 1; copyIdx < copies; ++copyIdx)
                {
                    for (uint32_t origIdx = 0; origIdx < s; ++origIdx)
                    {
                        uint32_t curIdx = copyIdx * s + origIdx;
                        // Copy structured address from the corresponding original node and append
                        m_structuredAddrs[i][curIdx] = originalAddrs[origIdx];
                        m_structuredAddrs[i][curIdx].Append(copyIdx + 1);

                        for (const auto& neighbor : m_adj[i][d][origIdx])
                        {
                            uint32_t neighborLevel = neighbor.first;
                            uint32_t neighborLocalIdx = neighbor.second;

                            if (neighborLevel == i)
                            {
                                // intra-level connection
                                uint32_t neighborIdx = copyIdx * s + neighborLocalIdx;
                                // if (neighborIdx < m_levels[i].GetN() && curIdx < neighborIdx)
                                // {
                                m_topoHelper->ConnectNodes(*this, i, d, curIdx, i, neighborIdx);
                                // }
                            }
                            else if (neighborLevel < i)
                            {
                                // connection to lower level (already replicated)
                                uint32_t neighborOrigSize = m_originalSizes[neighborLevel];
                                uint32_t neighborIdx =
                                    copyIdx * neighborOrigSize + neighborLocalIdx;

                                // if (neighborIdx < m_levels[neighborLevel].GetN())
                                // {
                                m_topoHelper
                                    ->ConnectNodes(*this, i, d, curIdx, neighborLevel, neighborIdx);
                                // }
                            }
                            // if neighborLevel > i (upper level) do nothing now
                        }
                    }
                }
            }
        }

        // Replicate PortSets for RuleBasedRouting nodes
        if (s > 0 && m_levels[i].GetN() > s)
        {
            for (uint32_t copyIdx = 1; copyIdx < copies; ++copyIdx)
            {
                for (uint32_t origIdx = 0; origIdx < s; ++origIdx)
                {
                    uint32_t newIdx = copyIdx * s + origIdx;

                    // Get original node's routing protocol
                    Ptr<Ipv4RoutingProtocol> origProto = GetRoutingProtocolByLocal(i, origIdx);
                    Ptr<RuleBasedRouting> origRouting = DynamicCast<RuleBasedRouting>(origProto);

                    if (origRouting)
                    {
                        // Get new node's routing protocol
                        Ptr<Ipv4RoutingProtocol> newProto = GetRoutingProtocolByLocal(i, newIdx);
                        Ptr<RuleBasedRouting> newRouting = DynamicCast<RuleBasedRouting>(newProto);

                        if (newRouting)
                        {
                            // Copy the PortSet structure from original to new node
                            // Note: Port indices will be updated when connections are established
                            const auto& origUpwardBuckets =
                                origRouting->GetPortSet().GetUpwardBuckets();
                            for (const auto& bucket : origUpwardBuckets)
                            {
                                // Create empty bucket with the same key
                                newRouting->GetPortSet().SetUpward(bucket.first, bucket.second);
                            }

                            const auto& origDownwardBuckets =
                                origRouting->GetPortSet().GetDownwardBuckets();
                            for (const auto& bucket : origDownwardBuckets)
                            {
                                // Create empty bucket with the same key
                                newRouting->GetPortSet().SetDownward(bucket.first, bucket.second);
                            }

                            const auto& origSameLevelBuckets =
                                origRouting->GetPortSet().GetSameLevelBuckets();
                            for (size_t dim = 0; dim < origSameLevelBuckets.size(); ++dim)
                            {
                                for (const auto& bucket : origSameLevelBuckets[dim])
                                {
                                    // Create empty bucket with the same key
                                    newRouting->GetPortSet().SetSameLevel(dim,
                                                                          bucket.first,
                                                                          bucket.second);
                                }
                            }
                        }
                    }
                }
            }
        }

        // Replicate groups
        auto& groups = m_levelGroups[i];
        size_t baseGroupCount = groups.size();
        for (uint32_t b = 1; b < copies; ++b)
        {
            uint32_t offset = b * s;
            for (size_t g = 0; g < baseGroupCount; ++g)
            {
                std::vector<uint32_t> gNew;
                for (uint32_t idx : groups[g])
                {
                    gNew.push_back(offset + idx);
                }
                groups.emplace_back(gNew);
            }
        }
    }

    for (uint32_t i = 0; i < l; ++i)
    {
        m_originalSizes[i] = m_levels[i].GetN();
    }

    return origSizes;
}

const std::vector<uint32_t>&
StructuredTopology::GetGidsOfLevel(uint32_t levelId) const
{
    NS_ASSERT_MSG(levelId < m_levelGids.size(), "Level out of range");
    return m_levelGids[levelId];
}

uint32_t
StructuredTopology::GetGidByLocal(uint32_t levelId, uint32_t localIdx) const
{
    NS_ASSERT_MSG(levelId < m_levelGids.size(), "GetGidByLocal: levelId out of range");
    const auto& gids = m_levelGids[levelId];
    NS_ASSERT_MSG(localIdx < gids.size(), "GetGidByLocal: localIdx out of range");
    return gids[localIdx];
}

std::vector<uint32_t>
StructuredTopology::GetGidsByLocalList(uint32_t levelId,
                                       const std::vector<uint32_t>& localIdxList) const
{
    NS_ASSERT_MSG(levelId < m_levelGids.size(), "GetGidsByLocalList: levelId out of range");
    const auto& gids = m_levelGids[levelId];

    std::vector<uint32_t> out;
    out.reserve(localIdxList.size());
    for (uint32_t li : localIdxList)
    {
        NS_ASSERT_MSG(li < gids.size(), "GetGidsByLocalList: local index out of range");
        out.push_back(gids[li]);
    }
    return out;
}

Ptr<Node>
StructuredTopology::GetNodeByLocal(uint32_t levelId, uint32_t localIdx) const
{
    return m_levels[levelId].Get(localIdx);
}

uint32_t
StructuredTopology::GetLocalIdx(uint32_t levelId, Ptr<Node> node) const
{
    NS_ASSERT_MSG(levelId < m_levels.size(), "GetLocalIdx: levelId out of range");
    const NodeContainer& lvl = m_levels[levelId];
    for (uint32_t idx = 0; idx < lvl.GetN(); ++idx)
    {
        if (lvl.Get(idx) == node)
        {
            return idx;
        }
    }
    return UINT32_MAX; // not found
}

uint32_t
StructuredTopology::GetNumSubTopologies(uint32_t levelId) const
{
    NS_LOG_FUNCTION(this);
    NS_ASSERT_MSG(levelId < m_levels.size(), "GetNumSubTopologies: levelId out of range");

    if (m_originalSizes[levelId] == 0)
    {
        return 0;
    }

    // Number of sub-topologies = total nodes in level / original size per sub-topology
    return m_levels[levelId].GetN() / m_originalSizes[levelId];
}

Ptr<Node>
StructuredTopology::GetNodeInSubTopology(uint32_t subTopologyIndex,
                                         uint32_t levelId,
                                         uint32_t localNodeIndex) const
{
    NS_LOG_FUNCTION(this);

    // Validate parameters
    NS_ASSERT_MSG(levelId < m_levels.size(), "GetNodeInSubTopology: levelId out of range");
    NS_ASSERT_MSG(localNodeIndex < m_originalSizes[levelId],
                  "GetNodeInSubTopology: localNodeIndex out of range for original topology");

    // Get the original size of this level (before replication)
    uint32_t originalSize = m_originalSizes[levelId];

    // Calculate the global index within this level:
    // Each sub-topology has 'originalSize' nodes, so sub-topology i starts at index i *
    // originalSize
    uint32_t globalIndexInLevel = subTopologyIndex * originalSize + localNodeIndex;

    // Check if this sub-topology exists in the current level
    uint32_t maxSubTopologies = GetNumSubTopologies(levelId);
    NS_ASSERT_MSG(subTopologyIndex < maxSubTopologies,
                  "GetNodeInSubTopology: subTopologyIndex out of range");
    NS_ASSERT_MSG(globalIndexInLevel < m_levels[levelId].GetN(),
                  "GetNodeInSubTopology: calculated index out of range");

    // Return the node
    return m_levels[levelId].Get(globalIndexInLevel);
}

const StructuredAddress&
StructuredTopology::GetStructuredAddrByLocal(uint32_t levelId, uint32_t localIdx) const
{
    NS_ASSERT_MSG(levelId < m_structuredAddrs.size(),
                  "GetStructuredAddrByLocal: levelId out of range");
    NS_ASSERT_MSG(localIdx < m_structuredAddrs[levelId].size(),
                  "GetStructuredAddrByLocal: localIdx out of range");
    return m_structuredAddrs[levelId][localIdx];
}

void
StructuredTopology::SetStructuredAddrByLocal(uint32_t levelId,
                                             uint32_t localIdx,
                                             const StructuredAddress& addr)
{
    NS_ASSERT_MSG(levelId < m_structuredAddrs.size(),
                  "SetStructuredAddrByLocal: levelId out of range");
    NS_ASSERT_MSG(localIdx < m_structuredAddrs[levelId].size(),
                  "SetStructuredAddrByLocal: localIdx out of range");
    m_structuredAddrs[levelId][localIdx] = addr;
}

const std::vector<std::vector<StructuredAddress>>&
StructuredTopology::GetStructuredAddrs() const
{
    return m_structuredAddrs;
}

const std::vector<std::pair<uint32_t, uint32_t>>&
StructuredTopology::GetAdjacency(uint32_t levelId, uint32_t dimId, uint32_t localId) const
{
    static const std::vector<std::pair<uint32_t, uint32_t>> kEmpty;
    if (levelId >= m_adj.size())
    {
        return kEmpty;
    }
    if (dimId >= m_adj[levelId].size())
    {
        return kEmpty;
    }
    if (localId >= m_adj[levelId][dimId].size())
    {
        return kEmpty;
    }
    return m_adj[levelId][dimId][localId];
}

Ptr<Ipv4RoutingProtocol>
StructuredTopology::GetRoutingProtocolByLocal(uint32_t levelId, uint32_t localIdx)
{
    NS_ASSERT_MSG(levelId < m_levels.size(), "GetRoutingProtocolByLocal: levelId out of range");
    NS_ASSERT_MSG(localIdx < m_levels[levelId].GetN(),
                  "GetRoutingProtocolByLocal: localIdx out of range");
    return m_levels[levelId].Get(localIdx)->GetObject<Ipv4>()->GetRoutingProtocol();
}

const Ptr<Ipv4RoutingProtocol>
StructuredTopology::GetRoutingProtocolByLocal(uint32_t levelId, uint32_t localIdx) const
{
    NS_ASSERT_MSG(levelId < m_levels.size(), "GetRoutingProtocolByLocal: levelId out of range");
    NS_ASSERT_MSG(localIdx < m_levels[levelId].GetN(),
                  "GetRoutingProtocolByLocal: localIdx out of range");
    return m_levels[levelId].Get(localIdx)->GetObject<Ipv4>()->GetRoutingProtocol();
}

uint32_t
StructuredTopology::GetTotalAddrBits() const
{
    return m_totalAddrBits;
}

int
StructuredTopology::GetLevelAddrBit(uint32_t levelId) const
{
    NS_ASSERT_MSG(levelId < m_levelStartAddrBit.size(), "GetLevelAddrBit: levelId out of range");
    return m_levelStartAddrBit[levelId];
}

void
StructuredTopology::PrintTopologyInfo() const
{
    std::cout << "\n========================================" << std::endl;
    std::cout << "          TOPOLOGY DETAILS" << std::endl;
    std::cout << "========================================" << std::endl;

    std::cout << "\nTOPOLOGY SUMMARY:" << std::endl;
    std::cout << "├─ Total Levels: " << GetNumLevels() << std::endl;
    std::cout << "└─ Total Nodes: " << GetAll().GetN() << std::endl;

    std::unordered_map<uint32_t, StructuredAddress> nodeAddrMap;
    nodeAddrMap.reserve(m_allNodes.GetN());
    for (uint32_t lv = 0; lv < GetNumLevels(); ++lv)
    {
        const NodeContainer& levelNodes = GetLevel(lv);
        for (uint32_t i = 0; i < levelNodes.GetN(); ++i)
        {
            uint32_t nodeId = levelNodes.Get(i)->GetId();
            nodeAddrMap[nodeId] = m_structuredAddrs[lv][i];
        }
    }

    auto formatPeerAddr = [&nodeAddrMap](Ptr<NetDevice> dev) -> std::string {
        if (!dev)
        {
            return "(none)";
        }
        Ptr<Channel> channel = dev->GetChannel();
        if (!channel || channel->GetNDevices() < 2)
        {
            return "(none)";
        }
        Ptr<NetDevice> peerDev = channel->GetDevice(0);
        if (peerDev == dev)
        {
            peerDev = channel->GetDevice(1);
        }
        if (!peerDev)
        {
            return "(none)";
        }
        Ptr<Node> peerNode = peerDev->GetNode();
        if (!peerNode)
        {
            return "(none)";
        }
        uint32_t peerId = peerNode->GetId();
        auto it = nodeAddrMap.find(peerId);
        if (it != nodeAddrMap.end())
        {
            return "Node " + std::to_string(peerId) + " " + it->second.ToString();
        }
        return "Node " + std::to_string(peerId);
    };

    std::cout << "\nLAYER-BY-LAYER BREAKDOWN (Bottom-Up):" << std::endl;

    // 自底向上打印层（Layer 0 在最底）
    for (int levelId = static_cast<int>(GetNumLevels()) - 1; levelId >= 0; --levelId)
    {
        const NodeContainer& levelNodes = GetLevel(levelId);
        uint32_t layerNumber = static_cast<uint32_t>(levelId);

        std::cout << "\n├─ Layer " << layerNumber << ":" << std::endl;
        std::cout << "│  ├─ Node Count: " << levelNodes.GetN() << std::endl;
        std::cout << "│  ├─ Dim Count: " << m_dimCounts[layerNumber] << std::endl;

        // 节点 ID 列表
        std::cout << "│  ├─ Node IDs: [";
        for (uint32_t i = 0; i < levelNodes.GetN(); ++i)
        {
            std::cout << levelNodes.Get(i)->GetId();
            if (i + 1 < levelNodes.GetN())
            {
                std::cout << ", ";
            }
        }
        std::cout << "]" << std::endl;

        // 每个节点的 StructuredAddress 和 IP 地址
        std::cout << "│  └─ Node Addresses:" << std::endl;
        for (uint32_t i = 0; i < levelNodes.GetN(); ++i)
        {
            Ptr<Node> node = levelNodes.Get(i);
            uint32_t nodeId = node->GetId();

            std::string addrStr = m_structuredAddrs[layerNumber][i].ToString();

            // 获取 IP 地址（如果有）
            Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
            std::string ipStr;
            if (ipv4 && ipv4->GetNInterfaces() > 1)
            {
                std::ostringstream oss;
                oss << ipv4->GetAddress(1, 0).GetLocal();
                ipStr = oss.str();
            }
            else
            {
                ipStr = "(No IP)";
            }

            std::cout << "│     • Node " << nodeId << " : " << addrStr << " , IP: " << ipStr
                      << std::endl;

            // 遍历并打印节点的 NetDevice 信息
            std::cout << "│       └─ Devices:" << std::endl;
            Ptr<Ipv4> ipv4Obj = node->GetObject<Ipv4>();
            if (ipv4Obj)
            {
                uint32_t nIfaces = ipv4Obj->GetNInterfaces();
                for (uint32_t ifaceIdx = 0; ifaceIdx < nIfaces; ++ifaceIdx)
                {
                    uint32_t nAddrs = ipv4Obj->GetNAddresses(ifaceIdx);
                    for (uint32_t addrIdx = 0; addrIdx < nAddrs; ++addrIdx)
                    {
                        Ipv4Address ipAddr = ipv4Obj->GetAddress(ifaceIdx, addrIdx).GetLocal();
                        Ptr<NetDevice> dev = ipv4Obj->GetNetDevice(ifaceIdx);
                        if (dev)
                        {
                            std::cout << "│         • NetDeviceId: " << dev->GetIfIndex()
                                      << " , type: " << dev->GetInstanceTypeId().GetName()
                                      << " , IP: " << ipAddr
                                      << " , Peer: " << formatPeerAddr(dev) << std::endl;
                        }
                    }
                }
            }
            else
            {
                // 如果没有 Ipv4 对象，直接遍历 Node 的设备
                for (uint32_t devIdx = 0; devIdx < node->GetNDevices(); ++devIdx)
                {
                    Ptr<NetDevice> dev = node->GetDevice(devIdx);
                    if (dev)
                    {
                        std::cout << "│         • NetDeviceId: " << dev->GetIfIndex()
                                  << " , type: " << dev->GetInstanceTypeId().GetName()
                                  << " , Peer: " << formatPeerAddr(dev) << std::endl;
                    }
                }
            }

            // 打印 PortSet 信息
            Ptr<Ipv4RoutingProtocol> routingProto = GetRoutingProtocolByLocal(layerNumber, i);
            Ptr<RuleBasedRouting> ruleRouting = DynamicCast<RuleBasedRouting>(routingProto);
            if (ruleRouting)
            {
                std::cout << "│       └─ PortSet:" << std::endl;

                // 打印 Upward buckets
                const auto& upwardBuckets = ruleRouting->GetPortSet().GetUpwardBuckets();
                if (!upwardBuckets.empty())
                {
                    std::cout << "│         ├─ Upward:" << std::endl;
                    for (const auto& bucket : upwardBuckets)
                    {
                        std::cout << "│         │  • " << bucket.first << ": [";
                        for (size_t j = 0; j < bucket.second.size(); ++j)
                        {
                            Ptr<NetDevice> dev = ipv4Obj->GetNetDevice(bucket.second[j]);
                            if (dev)
                            {
                                std::cout << "Dev " << bucket.second[j] << " "
                                          << dev->GetInstanceTypeId().GetName();

                                // 获取对侧节点信息
                                Ptr<Channel> channel = dev->GetChannel();
                                if (channel)
                                {
                                    std::cout << " -> ";
                                    bool firstPeer = true;
                                    for (uint32_t k = 0; k < channel->GetNDevices(); ++k)
                                    {
                                        Ptr<NetDevice> peerDev = channel->GetDevice(k);
                                        if (peerDev != dev)
                                        {
                                            Ptr<Node> peerNode = peerDev->GetNode();
                                            if (peerNode)
                                            {
                                                if (!firstPeer)
                                                {
                                                    std::cout << ", ";
                                                }
                                                std::cout << "Node " << peerNode->GetId();
                                                firstPeer = false;
                                            }
                                        }
                                    }
                                }
                                else
                                {
                                    std::cout << " (no channel)";
                                }
                            }
                            else
                            {
                                std::cout << "Dev " << bucket.second[j] << " (null)";
                            }

                            if (j + 1 < bucket.second.size())
                            {
                                std::cout << ", ";
                            }
                        }
                        std::cout << "]" << std::endl;
                    }
                }

                // 打印 Downward buckets
                const auto& downwardBuckets = ruleRouting->GetPortSet().GetDownwardBuckets();
                if (!downwardBuckets.empty())
                {
                    std::cout << "│         ├─ Downward:" << std::endl;
                    for (const auto& bucket : downwardBuckets)
                    {
                        std::cout << "│         │  • " << bucket.first << ": [";
                        for (size_t j = 0; j < bucket.second.size(); ++j)
                        {
                            Ptr<NetDevice> dev = ipv4Obj->GetNetDevice(bucket.second[j]);
                            if (dev)
                            {
                                std::cout << "Dev " << bucket.second[j] << " "
                                          << dev->GetInstanceTypeId().GetName();

                                // 获取对侧节点信息
                                Ptr<Channel> channel = dev->GetChannel();
                                if (channel)
                                {
                                    std::cout << " -> ";
                                    bool firstPeer = true;
                                    for (uint32_t k = 0; k < channel->GetNDevices(); ++k)
                                    {
                                        Ptr<NetDevice> peerDev = channel->GetDevice(k);
                                        if (peerDev != dev)
                                        {
                                            Ptr<Node> peerNode = peerDev->GetNode();
                                            if (peerNode)
                                            {
                                                if (!firstPeer)
                                                {
                                                    std::cout << ", ";
                                                }
                                                std::cout << "Node " << peerNode->GetId();
                                                firstPeer = false;
                                            }
                                        }
                                    }
                                }
                                else
                                {
                                    std::cout << " (no channel)";
                                }
                            }
                            else
                            {
                                std::cout << "Dev " << bucket.second[j] << " (null)";
                            }

                            if (j + 1 < bucket.second.size())
                            {
                                std::cout << ", ";
                            }
                        }
                        std::cout << "]" << std::endl;
                    }
                }

                // 打印 SameLevel buckets
                const auto& sameLevelBuckets = ruleRouting->GetPortSet().GetSameLevelBuckets();
                if (!sameLevelBuckets.empty())
                {
                    std::cout << "│         └─ SameLevel:" << std::endl;
                    for (size_t dim = 0; dim < sameLevelBuckets.size(); ++dim)
                    {
                        if (!sameLevelBuckets[dim].empty())
                        {
                            std::cout << "│           ├─ Dimension " << dim << ":" << std::endl;
                            for (const auto& bucket : sameLevelBuckets[dim])
                            {
                                std::cout << "│           │  • " << bucket.first << ": [";
                                for (size_t j = 0; j < bucket.second.size(); ++j)
                                {
                                    Ptr<NetDevice> dev = ipv4Obj->GetNetDevice(bucket.second[j]);
                                    if (dev)
                                    {
                                        std::cout << "Dev " << bucket.second[j] << " "
                                                  << dev->GetInstanceTypeId().GetName();

                                        // 获取对侧节点信息
                                        Ptr<Channel> channel = dev->GetChannel();
                                        if (channel)
                                        {
                                            std::cout << " -> ";
                                            bool firstPeer = true;
                                            for (uint32_t k = 0; k < channel->GetNDevices(); ++k)
                                            {
                                                Ptr<NetDevice> peerDev = channel->GetDevice(k);
                                                if (peerDev != dev)
                                                {
                                                    Ptr<Node> peerNode = peerDev->GetNode();
                                                    if (peerNode)
                                                    {
                                                        if (!firstPeer)
                                                        {
                                                            std::cout << ", ";
                                                        }
                                                        std::cout << "Node " << peerNode->GetId();
                                                        firstPeer = false;
                                                    }
                                                }
                                            }
                                        }
                                        else
                                        {
                                            std::cout << " (no channel)";
                                        }
                                    }
                                    else
                                    {
                                        std::cout << "Dev " << bucket.second[j] << " (null)";
                                    }

                                    if (j + 1 < bucket.second.size())
                                    {
                                        std::cout << ", ";
                                    }
                                }
                                std::cout << "]" << std::endl;
                            }
                        }
                    }
                }
            }
            else
            {
                std::cout << "│       └─ PortSet: (No RuleBasedRouting)" << std::endl;
            }
            // // 打印 PortSet 信息
            // Ptr<Ipv4RoutingProtocol> routingProto = GetRoutingProtocolByLocal(layerNumber, i);
            // Ptr<RuleBasedRouting> ruleRouting = DynamicCast<RuleBasedRouting>(routingProto);
            // if (ruleRouting)
            // {
            //     std::cout << "│       └─ PortSet:" << std::endl;

            //     // 打印 Upward buckets
            //     const auto& upwardBuckets = ruleRouting->GetPortSet().GetUpwardBuckets();
            //     if (!upwardBuckets.empty())
            //     {
            //         std::cout << "│         ├─ Upward:" << std::endl;
            //         for (const auto& bucket : upwardBuckets)
            //         {
            //             std::cout << "│         │  • " << bucket.first << ": [";
            //             for (size_t j = 0; j < bucket.second.size(); ++j)
            //             {
            //                 Ptr<NetDevice> dev = ipv4Obj->GetNetDevice(bucket.second[j]);
            //                 if (dev)
            //                 {
            //                     std::cout << "Dev " << bucket.second[j] << " "
            //                               << dev->GetInstanceTypeId().GetName();
            //                 }
            //                 else
            //                 {
            //                     std::cout << "Dev " << bucket.second[j] << " (null)";
            //                 }

            //                 if (j + 1 < bucket.second.size())
            //                 {
            //                     std::cout << ", ";
            //                 }
            //             }
            //             std::cout << "]" << std::endl;
            //         }
            //     }

            //     // 打印 Downward buckets
            //     const auto& downwardBuckets = ruleRouting->GetPortSet().GetDownwardBuckets();
            //     if (!downwardBuckets.empty())
            //     {
            //         std::cout << "│         ├─ Downward:" << std::endl;
            //         for (const auto& bucket : downwardBuckets)
            //         {
            //             std::cout << "│         │  • " << bucket.first << ": [";
            //             for (size_t j = 0; j < bucket.second.size(); ++j)
            //             {
            //                 Ptr<NetDevice> dev = ipv4Obj->GetNetDevice(bucket.second[j]);
            //                 if (dev)
            //                 {
            //                     std::cout << "Dev " << bucket.second[j] << " "
            //                               << dev->GetInstanceTypeId().GetName();
            //                 }
            //                 else
            //                 {
            //                     std::cout << "Dev " << bucket.second[j] << " (null)";
            //                 }

            //                 if (j + 1 < bucket.second.size())
            //                 {
            //                     std::cout << ", ";
            //                 }
            //             }
            //             std::cout << "]" << std::endl;
            //         }
            //     }

            //     // 打印 SameLevel buckets
            //     const auto& sameLevelBuckets = ruleRouting->GetPortSet().GetSameLevelBuckets();
            //     if (!sameLevelBuckets.empty())
            //     {
            //         std::cout << "│         └─ SameLevel:" << std::endl;
            //         for (size_t dim = 0; dim < sameLevelBuckets.size(); ++dim)
            //         {
            //             if (!sameLevelBuckets[dim].empty())
            //             {
            //                 std::cout << "│           ├─ Dimension " << dim << ":" << std::endl;
            //                 for (const auto& bucket : sameLevelBuckets[dim])
            //                 {
            //                     std::cout << "│           │  • " << bucket.first << ": [";
            //                     for (size_t j = 0; j < bucket.second.size(); ++j)
            //                     {
            //                         Ptr<NetDevice> dev = ipv4Obj->GetNetDevice(bucket.second[j]);
            //                         if (dev)
            //                         {
            //                             std::cout << "Dev " << bucket.second[j] << " "
            //                                       << dev->GetInstanceTypeId().GetName();
            //                         }
            //                         else
            //                         {
            //                             std::cout << "Dev " << bucket.second[j] << " (null)";
            //                         }

            //                         if (j + 1 < bucket.second.size())
            //                         {
            //                             std::cout << ", ";
            //                         }
            //                     }
            //                     std::cout << "]" << std::endl;
            //                 }
            //             }
            //         }
            //     }
            // }
            // else
            // {
            //     std::cout << "│       └─ PortSet: (No RuleBasedRouting)" << std::endl;
            // }
        }
    }

    std::cout << "\nCONNECTION DETAILS (Bottom-Up):" << std::endl;
    uint32_t totalConnections = 0;

    for (int levelId = static_cast<int>(GetNumLevels()) - 1; levelId >= 0; --levelId)
    {
        const NodeContainer& levelNodes = GetLevel(levelId);
        uint32_t layerNumber = static_cast<uint32_t>(levelId);

        std::cout << "\n├─ Layer " << layerNumber << " Connections:" << std::endl;

        for (uint32_t i = 0; i < levelNodes.GetN(); ++i)
        {
            Ptr<Node> node = levelNodes.Get(i);
            uint32_t nodeId = node->GetId();
            uint32_t deviceCount = node->GetNDevices();

            // 行首附带地址，便于阅读
            std::string addrStr = m_structuredAddrs[layerNumber][i].ToString();
            std::cout << "│  ├─ Node " << nodeId << " " << addrStr << ": ";

            std::vector<uint32_t> connectedNodes;
            connectedNodes.reserve(deviceCount > 0 ? deviceCount - 1 : 0);

            for (uint32_t j = 1; j < deviceCount; ++j) // 跳过 0 号（loopback）
            {
                Ptr<NetDevice> device = node->GetDevice(j);
                Ptr<Channel> channel = device->GetChannel();
                if (channel && channel->GetNDevices() == 2)
                {
                    // p2p 信道两端各 1 个 device
                    for (uint32_t k = 0; k < 2; ++k)
                    {
                        Ptr<NetDevice> otherDevice = channel->GetDevice(k);
                        if (otherDevice != device)
                        {
                            Ptr<Node> otherNode = otherDevice->GetNode();
                            connectedNodes.push_back(otherNode->GetId());
                            totalConnections++;
                        }
                    }
                }
            }

            if (connectedNodes.empty())
            {
                std::cout << "No connections" << std::endl;
            }
            else
            {
                std::cout << "Connected to [";
                for (size_t k = 0; k < connectedNodes.size(); ++k)
                {
                    std::cout << connectedNodes[k];
                    if (k + 1 < connectedNodes.size())
                    {
                        std::cout << ", ";
                    }
                }
                std::cout << "]" << std::endl;
            }
        }
    }

    std::cout << "\nCONNECTION SUMMARY:" << std::endl;
    std::cout << "├─ Total P2P Links: " << (totalConnections / 2) << " (bidirectional)"
              << std::endl;
    std::cout << "└─ Total Directed Connections: " << totalConnections << std::endl;

    std::cout << "\nTOPOLOGY STRUCTURE:" << std::endl;
    std::cout << "├─ Total Layers: " << GetNumLevels() << std::endl;
    std::cout << "├─ Layer Numbering: Bottom-up (Layer 0 = Bottom/Host Layer)" << std::endl;

    std::cout << "\n========================================" << std::endl;
}

void
StructuredTopology::RegisterAddresses()
{
    for (uint32_t i = 0; i < GetNumLevels(); ++i)
    {
        for (uint32_t j = 0; j < GetLevel(i).GetN(); ++j)
        {
            Ptr<Ipv4> ipv4 = GetNodeByLocal(i, j)->GetObject<Ipv4>();
            const uint32_t nIf = ipv4 ? ipv4->GetNInterfaces() : 0;
            for (uint32_t ifIdx = 0; ifIdx < nIf; ++ifIdx)
            {
                const uint32_t nAddr = ipv4->GetNAddresses(ifIdx);
                for (uint32_t addrIdx = 0; addrIdx < nAddr; ++addrIdx)
                {
                    StructuredAddressDirectory::Get()->Register(
                        ipv4->GetAddress(ifIdx, addrIdx).GetLocal(),
                        m_structuredAddrs[i][j]);
                }
            }
        }
    }
}

} // namespace ns3
