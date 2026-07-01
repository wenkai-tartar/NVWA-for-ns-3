/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#ifndef RULE_BASED_ROUTING_H
#define RULE_BASED_ROUTING_H

#include "ingress-classifier.h"
#include "routing-common.h"
#include "routing-rule-manager.h"
#include "routing-rule.h"
#include "structured-address.h"

#include "ns3/ipv4-header.h"
#include "ns3/ipv4-route.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/ipv4.h"
#include "ns3/log.h"
#include "ns3/net-device.h"
#include "ns3/node.h"
#include "ns3/object.h"
#include "ns3/packet.h"
#include "ns3/ptr.h"
#include "ns3/simulator.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <optional>
#include <vector>

namespace ns3
{

class CongestionSignalProvider;
class NonMinimalPolicy;

/**
 * @brief RuleBasedRouting: A rule-driven IPv4 routing protocol.
 *
 * Each node owns one instance. The instance holds:
 *  - A set of RoutingRule (with priority)
 *  - A PortSet mapping bucket names to interface indices of Ipv4
 *
 * The forwarding decision is encoded in rules (Match + Action). The Action
 * selects an egress interface from a named bucket in the PortSet.
 */
class RuleBasedRouting : public Ipv4RoutingProtocol
{
  public:
    static TypeId GetTypeId();

    RuleBasedRouting();
    ~RuleBasedRouting() override;

    // Build the table and port sets
    void AddRule(Ptr<RoutingRule> rule);
    // After all AddRule calls, call Freeze() to sort by priority (desc)
    void Freeze();

    // Replace the whole PortSet at once
    void SetPortSet(const PortSet& ports);

    // Mutable access to PortSet (e.g., fill buckets during topology build)
    PortSet& GetPortSet()
    {
        return m_ports;
    }

    const PortSet& GetPortSet() const
    {
        return m_ports;
    }

    void SetTopo(const StructuredTopology* topo)
    {
        m_topo = topo;
    }

    const StructuredTopology* GetTopo() const
    {
        return m_topo;
    }

    // Optional: set static fields used in RoutingContext for this node
    void SetNodeLevelAndIndex(uint32_t levelId, uint32_t localIdx)
    {
        m_levelId = levelId;
        m_localIdx = localIdx;
    }

    void SetSrc(const StructuredAddress& src)
    {
        m_src = src;
    }

    void SetNonMinimalPolicy(Ptr<NonMinimalPolicy> policy);
    Ptr<NonMinimalPolicy> GetNonMinimalPolicy() const;

    void SetCongestionSignalProvider(std::shared_ptr<CongestionSignalProvider> provider);

    // Ipv4RoutingProtocol interface
    void SetIpv4(Ptr<Ipv4> ipv4) override;

    /**
     * @brief Lookup in the forwarding rule table for destination.
     * @param header IPv4 header
     * @param packet The packet (needed for extracting TCP/UDP ports for 5-tuple hash)
     * @param oif output interface if any (put 0 otherwise)
     * @return Ipv4Route to route the packet to reach dest address
     */
    Ptr<Ipv4Route> Lookup(const Ipv4Header& header,
                          Ptr<const Packet> packet,
                          Ptr<NetDevice> oif = nullptr,
                          Ptr<const NetDevice> idev = nullptr,
                          bool isInject = false);

    Ptr<Ipv4Route> RouteOutput(Ptr<Packet> p,
                               const Ipv4Header& header,
                               Ptr<NetDevice> oif,
                               Socket::SocketErrno& sockerr) override;

    bool RouteInput(Ptr<const Packet> p,
                    const Ipv4Header& header,
                    Ptr<const NetDevice> idev,
                    const UnicastForwardCallback& ucb,
                    const MulticastForwardCallback& mcb,
                    const LocalDeliverCallback& lcb,
                    const ErrorCallback& ecb) override;

    void NotifyInterfaceUp(uint32_t interface) override;
    void NotifyInterfaceDown(uint32_t interface) override;

    void NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address) override
    {
    }

    void NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress address) override
    {
    }

    static std::vector<uint32_t> ParseKeyFields(const std::string& key);

    /**
     * @brief Set the routing rule manager
     * @param manager Pointer to the routing rule manager
     * @param levelId The level this instance belongs to
     */
    void SetRoutingRuleManager(Ptr<RoutingRuleManager> manager);

    void SetLevelId(uint32_t levelId)
    {
        m_levelId = levelId;
    }

    void SetLocalIdx(uint32_t localIdx)
    {
        m_localIdx = localIdx;
    }

    void PrintRoutingTable(Ptr<OutputStreamWrapper> stream,
                           Time::Unit unit = Time::S) const override;


    // Compute a 5-tuple flow hash from header and packet (src IP, dst IP, protocol, src port, dst port)
    // Public for testing purposes
    // @param h IPv4 header
    // @param p Packet (for extracting ports)
    // @param seed Hash seed (typically node ID for per-node diversity)
    static uint64_t ComputeFlowHash(const Ipv4Header& h, Ptr<const Packet> p, uint32_t seed = 0);


    // Forward declare (or define) RegionKey before any use.
    struct RegionKey;

    // Map a failed downward port to affected destination regions (RegionKey).
    std::vector<RegionKey> BuildImpactedRegionsForDownwardPort(uint32_t interface) const;

    /**
     * @brief RegionKey: globally-recognizable destination-region key.
     *
     * A RegionKey is a prefix of the destination StructuredAddress:
     *   (prefixLen, prefixFields[0..prefixLen-1]).
     *
     * Containment is prefix-of; exception lookup uses longest-prefix match (LPM).
     */
    struct RegionKey
    {
        uint32_t prefixLen{0};
        std::vector<StructuredAddress::Field> prefixFields;

        bool Matches(const StructuredAddress& dst) const;
        std::string Encode() const;
    };

    /**
     * @brief Exception entry keyed by RegionKey.
     *
     * When matched, the entry overrides the default hierarchical rule evaluation
     * by directly selecting among the candidate interface indices.
     */
    struct ExceptionEntry
    {
        RegionKey key;
        std::vector<uint32_t> candidateIfs;
        std::optional<Ipv4Address> gw;
        std::string pfx;

        std::optional<uint32_t> PickEgress(uint64_t flowHash) const;
    };

  private:
    struct ExceptionTrieNode
    {
        std::unordered_map<StructuredAddress::Field, std::unique_ptr<ExceptionTrieNode>> children;
        std::optional<ExceptionEntry> entry;
    };

    struct EmptyExceptionUpdate
    {
        RegionKey key;
        std::string pfx;
    };

    std::vector<EmptyExceptionUpdate> UpdateExceptionWithPrefixIntersections(
        const RegionKey& rk,
        const std::string& pfx,
        const std::vector<uint32_t>& candidate,
        const std::vector<uint32_t>* prevCandidateFallback);
    ExceptionTrieNode* FindOrCreateExceptionNode(const RegionKey& rk);
    ExceptionTrieNode* FindExceptionNode(const RegionKey& rk);
    const ExceptionTrieNode* FindExceptionNode(const RegionKey& rk) const;
    ExceptionEntry* FindExactException(const RegionKey& rk);
    const ExceptionEntry* FindExactException(const RegionKey& rk) const;
    bool RemoveExceptionEntry(const RegionKey& rk);
    void SetExceptionEntry(const RegionKey& rk,
                           const std::string& pfx,
                           const std::vector<uint32_t>& candidate,
                           std::optional<Ipv4Address> gw = std::nullopt);

    struct Decision
    {
        uint32_t outIf;                // egress interface index on this node
        std::optional<Ipv4Address> gw; // optional L3 next-hop; empty => on-link
    };

    std::optional<Decision> Evaluate(const StructuredAddress& dst,
                                     const RoutingContext& ctx) const;

    RoutingContext BuildRoutingContext(uint64_t flowHash) const;
    IngressInfo BuildIngressInfo(Ptr<const NetDevice> idev, bool isInject);
    std::optional<uint32_t> GetIncomingPeerNodeIdOwningIp(Ptr<const NetDevice> idev,
                                                          Ipv4Address ip) const;
    void EnsureIngressClassifier();
    bool RulesRequireFlowHash() const;
    std::optional<Ipv4Address> GetPeerGateway(uint32_t outIf) const;

    // Exception table (LPM by destination StructuredAddress prefix).
    const ExceptionEntry* LookupException(const StructuredAddress& dst) const;
    static std::string EncodePrefix(const StructuredAddress& dst, uint32_t prefixLen);

    // Control-plane: withdrawal propagation (task 5)
    // Withdrawal is sent to all directly-connected neighbors (not just Clos-upward links) to avoid black holes.
    // Messages carry (originNodeId, epoch) to prevent repeated/looping propagation.
    void SendWithdrawalToNeighbors(const RegionKey& rk,
                                   const std::string& pfx,
                                   uint32_t originNodeId,
                                   uint64_t epoch,
                                   std::optional<uint32_t> excludeNeighborId = std::nullopt) const;
    void HandleWithdrawalFromNeighbor(const RegionKey& rk,
                                      const std::string& pfx,
                                      uint32_t originNodeId,
                                      uint64_t epoch,
                                      uint32_t fromNeighborNodeId);
    void EnqueueWithdrawalImmediate(Ptr<RuleBasedRouting> target,
                                    const RegionKey& rk,
                                    const std::string& pfx,
                                    uint32_t originNodeId,
                                    uint64_t epoch,
                                    uint32_t fromNeighborNodeId) const;

    // Neighbor helpers based on local NetDevices/Channels
    std::vector<uint32_t> GetUpstreamNeighborNodeIds() const; // returns ALL directly-connected neighbor NodeIds
    std::optional<uint32_t> GetInterfaceToNeighbor(uint32_t neighborNodeId) const;
    std::optional<std::string> ExtractDownwardBucketKeyFromRegion(const RegionKey& rk) const;

    // Debug: dump local exception table after control-plane updates.
    void DumpExceptionTable(const std::string& reason) const;

    // Dedup/version state
    mutable std::unordered_map<std::string, uint64_t> m_lastEpochSeen; // key = "<origin>|<plen>|<pfx>" -> max epoch seen
    // Per-sender dedup for a single failure update. Enforce:
    // for a given (origin, epoch), accept at most one withdrawal from the same sender node.
    // key = "<from>|<origin>" -> max epoch accepted from that sender for that origin
    mutable std::unordered_map<std::string, uint64_t> m_lastEpochSeenFromSender;
    mutable uint64_t m_withdrawEpochCounter{0};

    Ptr<Ipv4> m_ipv4;
    PortSet m_ports;
    PortSetIngressClassifier m_ingressClassifier;
    bool m_ingressReady{false};
    Ptr<NonMinimalPolicy> m_nonMinimalPolicy;
    std::shared_ptr<CongestionSignalProvider> m_congestionProvider;

    // Per-node exception entries stored in a prefix trie (MSB-first fields).
    ExceptionTrieNode m_exceptionTrieRoot;
    size_t m_exceptionEntryCount{0};

    // Per-node static context fields
    uint32_t m_levelId{0};
    uint32_t m_localIdx{0};
    StructuredAddress m_src;
    const StructuredTopology* m_topo{nullptr};
    mutable bool m_rulesRequireFlowHashCached{false};
    mutable bool m_rulesRequireFlowHash{false};
    mutable bool m_peerGatewayCacheReady{false};
    mutable std::vector<std::optional<Ipv4Address>> m_peerGatewayByIf;
};

} // namespace ns3

#endif /* RULE_BASED_ROUTING_H */
