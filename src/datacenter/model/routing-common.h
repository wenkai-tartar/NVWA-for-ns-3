/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#ifndef ROUTING_COMMON_H
#define ROUTING_COMMON_H

#include "structured-topology.h"

#include "ns3/ipv4-header.h"
#include "ns3/net-device.h"
#include "ns3/packet.h"
#include "ns3/ptr.h"
#include "ns3/structured-address.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ns3
{

// ------------------- PortSet -------------------
enum class Direction
{
    Downward,
    Upward,
    SameLevel
};

enum class IngressDirection : uint8_t
{
    kUnknown = 0,
    kInject = 1,
    kUpward = 2,
    kDownward = 3,
    kSameLevel = 4,
};

struct IngressInfo
{
    IngressDirection direction{IngressDirection::kUnknown};
    int32_t dimId{-1};
    std::string bucketKey;
    std::unordered_map<std::string, uint64_t> meta;

    bool IsValid() const
    {
        return direction != IngressDirection::kUnknown;
    }

    static IngressInfo Unknown()
    {
        return IngressInfo{};
    }

    static IngressInfo Inject()
    {
        IngressInfo info;
        info.direction = IngressDirection::kInject;
        return info;
    }
};

class PortSet
{
  public:
    // Constructor with an optional parameter to initialize the size of m_sameLevelBuckets
    explicit PortSet(size_t sameLevelDimensions = 0);

    void SetUpward(const std::string& addressBit, std::vector<uint32_t> ports);
    void AddUpward(const std::string& addressBit, uint32_t port);
    const std::vector<uint32_t>& GetUpward(const std::string& addressBit) const;

    void SetDownward(const std::string& addressBit, std::vector<uint32_t> ports);
    void AddDownward(const std::string& addressBit, uint32_t port);
    const std::vector<uint32_t>& GetDownward(const std::string& addressBit) const;
    void SetDownward(uint32_t addressBit, std::vector<uint32_t> ports);
    void AddDownward(uint32_t addressBit, uint32_t port);
    const std::vector<uint32_t>& GetDownward(uint32_t addressBit) const;

    const std::vector<std::string>& GetDownwardKeysByPort(uint32_t port) const;

    void SetSameLevel(uint32_t dimension,
                      const std::string& addressBit,
                      std::vector<uint32_t> ports);
    void AddSameLevel(uint32_t dimension, const std::string& addressBit, uint32_t port);
    const std::vector<uint32_t>& GetSameLevel(uint32_t dimension,
                                              const std::string& addressBit) const;

    // Access to internal bucket maps for debugging/inspection
    const std::unordered_map<std::string, std::vector<uint32_t>>& GetUpwardBuckets() const;
    const std::unordered_map<std::string, std::vector<uint32_t>>& GetDownwardBuckets() const;
    const std::vector<std::unordered_map<std::string, std::vector<uint32_t>>>& GetSameLevelBuckets()
        const;

    // ============ Failure-aware port management ============
    /**
     * Mark a port as failed (down). Removes it from all available port lists.
     * @param port The interface index to mark as failed
     */
    void MarkPortDown(uint32_t port);

    /**
     * Mark a port as recovered (up). Adds it back to available port lists.
     * @param port The interface index to mark as recovered
     */
    void MarkPortUp(uint32_t port);

    /**
     * Get available (non-failed) upward ports for a given address bit
     */
    const std::vector<uint32_t>& GetAvailableUpward(const std::string& addressBit) const;

    /**
     * Get available (non-failed) downward ports for a given address bit
     */
    const std::vector<uint32_t>& GetAvailableDownward(const std::string& addressBit) const;
    const std::vector<uint32_t>& GetAvailableDownward(uint32_t addressBit) const;

    /**
     * Get available (non-failed) same-level ports for a given dimension and address bit
     */
    const std::vector<uint32_t>& GetAvailableSameLevel(uint32_t dimension,
                                                        const std::string& addressBit) const;

    /**
     * Check if a port is currently marked as failed
     */
    bool IsPortDown(uint32_t port) const;

  private:
    // All ports (including failed ones)
    std::unordered_map<std::string, std::vector<uint32_t>> m_upwardBuckets;
    std::unordered_map<std::string, std::vector<uint32_t>> m_downwardBuckets;
    std::unordered_map<uint32_t, std::vector<uint32_t>> m_downwardFieldBuckets;
    std::vector<std::unordered_map<std::string, std::vector<uint32_t>>> m_sameLevelBuckets;

    // Available ports (excluding failed ones)
    mutable std::unordered_map<std::string, std::vector<uint32_t>> m_availableUpwardBuckets;
    mutable std::unordered_map<std::string, std::vector<uint32_t>> m_availableDownwardBuckets;
    mutable std::unordered_map<uint32_t, std::vector<uint32_t>> m_availableDownwardFieldBuckets;
    mutable std::vector<std::unordered_map<std::string, std::vector<uint32_t>>> m_availableSameLevelBuckets;

    // Set of failed ports
    std::unordered_set<uint32_t> m_failedPorts;

    // Reverse index: port -> downward bucket keys containing this port
    std::unordered_map<uint32_t, std::vector<std::string>> m_downwardKeysByPort;

    // Helper to rebuild available lists when port status changes
    void RebuildAvailableLists();
    static bool TryParseSingleFieldKey(const std::string& key, uint32_t* value);

    static const std::vector<uint32_t> kEmpty;
    static const std::vector<std::string> kEmptyKeys;
};

// ------------------- RoutingContext -------------------
struct RoutingContext
{
    using FlowHashProvider = uint64_t (*)(const Ipv4Header&, Ptr<const Packet>, uint32_t);

    uint32_t levelId{0};
    uint32_t localIdx{0};
    mutable uint64_t flowHash{0};
    StructuredAddress src;
    const StructuredAddress* srcRef{nullptr};
    const StructuredTopology* topo{nullptr};
    mutable bool flowHashReady{false};
    FlowHashProvider flowHashProvider{nullptr};
    const Ipv4Header* flowHashHeader{nullptr};
    Ptr<const Packet> flowHashPacket;
    uint32_t flowHashSeed{0};

    const StructuredAddress& GetSrc() const
    {
        return srcRef ? *srcRef : src;
    }

    void SetSrcRef(const StructuredAddress& value)
    {
        srcRef = &value;
    }

    uint64_t GetFlowHash() const
    {
        if (!flowHashReady && flowHashProvider && flowHashHeader)
        {
            flowHash = flowHashProvider(*flowHashHeader, flowHashPacket, flowHashSeed);
            flowHashReady = true;
        }
        return flowHash;
    }

    void SetFlowHash(uint64_t hash)
    {
        flowHash = hash;
        flowHashReady = true;
        flowHashProvider = nullptr;
        flowHashHeader = nullptr;
        flowHashPacket = nullptr;
        flowHashSeed = 0;
    }

    void SetFlowHashProvider(const Ipv4Header& header,
                             Ptr<const Packet> packet,
                             uint32_t seed,
                             FlowHashProvider provider)
    {
        flowHash = 0;
        flowHashReady = false;
        flowHashProvider = provider;
        flowHashHeader = &header;
        flowHashPacket = packet;
        flowHashSeed = seed;
    }
};

// ------------------- Port selection -------------------
enum class PortSelectPolicy : uint8_t
{
    kFirst,
    kRandom,
    kByHash,
};

class PortSelector
{
  public:
    explicit PortSelector(PortSelectPolicy policy = PortSelectPolicy::kFirst)
        : m_policy(policy)
    {
    }

    void SetPolicy(PortSelectPolicy p)
    {
        m_policy = p;
    }

    PortSelectPolicy GetPolicy() const
    {
        return m_policy;
    }

    std::optional<uint32_t> Pick(const std::vector<uint32_t>& ports,
                                 const RoutingContext& ctx) const;

  private:
    PortSelectPolicy m_policy{PortSelectPolicy::kFirst};
};

} // namespace ns3

#endif // ROUTING_COMMON_H
