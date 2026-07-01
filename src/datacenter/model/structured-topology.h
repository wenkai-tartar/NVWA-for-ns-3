/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#ifndef STRUCTURED_TOPOLOGY_H
#define STRUCTURED_TOPOLOGY_H

#include "structured-address.h"

#include "ns3/node-container.h"
#include "ns3/node.h"
#include "ns3/object.h"
#include "ns3/ptr.h"
#include "ns3/topology-helper.h"

#include <cstdint>
#include <sys/types.h>

namespace ns3
{

class StructuredTopology : public Object
{
  public:
    /**
     * @brief Get the type ID.
     * @return the object TypeId
     */
    static TypeId GetTypeId();

    StructuredTopology();

    StructuredTopology(std::shared_ptr<TopologyHelper> topoHelper);

    ~StructuredTopology() override;

    const NodeContainer& GetLevel(uint32_t levelId) const; // Nodes at the level
    Ptr<Node> GetNode(uint32_t levelId, uint32_t j) const; // The j-th node at level
    Ptr<Node> GetNode(uint32_t nodeId) const;              // The node with the given id
    const NodeContainer& GetAll() const;                   // All nodes
    uint32_t GetNumLevels() const;
    uint32_t GetNumGroups(uint32_t levelId) const;
    uint32_t GetDimCount(uint32_t levelId) const;
    /**
     * @return The local indexs of nodes in the g-th group at level l
     */
    const std::vector<uint32_t>& GetGroupLocalIndices(uint32_t levelId, uint32_t groupId) const;

    /**
     * @return level id
     */
    std::pair<uint32_t, uint32_t> CreateLevel(uint32_t nodeNum);

    std::vector<uint32_t> ReplicateTopologyInPlace(uint32_t copies);

    // Helper function to find the corresponding replicated node in the same sub-topology
    Ptr<Node> FindCorrespondingReplicatedNode(uint32_t levelId,
                                              Ptr<Node> originalNode,
                                              uint32_t copyIndex,
                                              uint32_t originalSize);

    /**
     * Access the level
     */
    const std::vector<uint32_t>& GetGidsOfLevel(uint32_t levelId) const;

    // Get the GID by (globalLevelId, localIdx)
    uint32_t GetGidByLocal(uint32_t levelId, uint32_t localIdx) const;

    // Batch: map a list of local indices to GIDs (globalLevelId)
    std::vector<uint32_t> GetGidsByLocalList(uint32_t levelId,
                                             const std::vector<uint32_t>& localIdxList) const;

    // Directly fetch node pointer by (globalLevelId, localIdx)
    Ptr<Node> GetNodeByLocal(uint32_t levelId, uint32_t localIdx) const;

    // Access node in a specific sub-topology: (subTopologyIndex, levelId, localNodeIndex)
    Ptr<Node> GetNodeInSubTopology(uint32_t subTopologyIndex,
                                   uint32_t levelId,
                                   uint32_t localNodeIndex) const;

    // Get the number of sub-topologies for a given level
    uint32_t GetNumSubTopologies(uint32_t levelId) const;

    // Replicate nodes of a specific level (without copying edges)
    void ReplicateLevelNodes(uint32_t levelId, uint32_t copies);

    // Return original (pre-replication) node count of given level
    uint32_t GetOriginalSize(uint32_t levelId) const
    {
        return levelId < m_originalSizes.size() ? m_originalSizes[levelId] : 0;
    }

    // Get the shared TopologyHelper
    std::shared_ptr<TopologyHelper> GetTopologyHelper() const;
    void SetTopologyHelper(std::shared_ptr<TopologyHelper> topoHelper);

    uint32_t GetLocalIdx(uint32_t levelId, Ptr<Node> node) const;

    const StructuredAddress& GetStructuredAddrByLocal(uint32_t levelId, uint32_t localIdx) const;
    void SetStructuredAddrByLocal(uint32_t levelId,
                                  uint32_t localIdx,
                                  const StructuredAddress& addr);

    const std::vector<std::vector<StructuredAddress>>& GetStructuredAddrs() const;

    // Access adjacency list: adjacency[levelId][dimId][localId] -> vector of {dstLevelId, dstLocalId}
    const std::vector<std::pair<uint32_t, uint32_t>>& GetAdjacency(uint32_t levelId,
                                                                   uint32_t dimId,
                                                                   uint32_t localId) const;

    Ptr<Ipv4RoutingProtocol> GetRoutingProtocolByLocal(uint32_t levelId, uint32_t localIdx);

    const Ptr<Ipv4RoutingProtocol> GetRoutingProtocolByLocal(uint32_t levelId,
                                                             uint32_t localIdx) const;

    uint32_t GetTotalAddrBits() const;
    int GetLevelAddrBit(uint32_t levelId) const;

    void PrintTopologyInfo() const;

    void RegisterAddresses();

  private:
    friend class TopologyHelper;
    std::vector<NodeContainer> m_levels;
    std::vector<uint32_t> m_dimCounts;
    std::vector<std::vector<StructuredAddress>> m_structuredAddrs;
    std::vector<std::vector<std::vector<uint32_t>>> m_levelGroups; // level -> groups -> local ids
    std::vector<std::vector<uint32_t>> m_levelGids;                // level -> gids
    std::vector<uint32_t> m_localIds;      // local id within its layer, index by global gid
    std::vector<uint32_t> m_originalSizes; // store original size per level for replication logic

    uint32_t m_totalAddrBits{0};
    std::vector<int> m_levelStartAddrBit; // the start address bit of each level

    // adjacency[levelId][dimId][localId] = vector of {dstLevelId, dstLocalId}
    std::vector<std::vector<std::vector<std::vector<std::pair<uint32_t, uint32_t>>>>> m_adj;

    NodeContainer m_allNodes; // All nodes at this block

    // TopologyHelper for automatic Internet Stack installation
    std::shared_ptr<TopologyHelper> m_topoHelper;
};

} // namespace ns3

#endif /* STRUCTURED_TOPOLOGY_H */
