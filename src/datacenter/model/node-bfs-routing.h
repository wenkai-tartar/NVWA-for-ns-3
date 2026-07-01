/*
 * Author: Wenkai Li (v-wenkaili@microsoft.com)
 */

#ifndef NODE_BFS_ROUTING_H
#define NODE_BFS_ROUTING_H

#include "ns3/ipv4-routing-protocol.h"
#include "ns3/node.h"
#include "routing-common.h"
#include "ns3/structured-topology.h"

#include <map>
#include <string>
#include <vector>

namespace ns3
{

class NodeBfsRouting : public Ipv4RoutingProtocol
{
  public:
    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId();

    /**
     * \brief Construct an empty UnifiedRouting routing protocol,
     */
    NodeBfsRouting();
    NodeBfsRouting(Ptr<Node> node, Ptr<StructuredTopology> topo);
    ~NodeBfsRouting() override;

    // These methods inherited from base class
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
    void NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address) override;
    void NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress address) override;
    void SetIpv4(Ptr<Ipv4> ipv4) override;
    void SetTopology(Ptr<StructuredTopology> topo);
    void PrintRoutingTable(Ptr<OutputStreamWrapper> stream,
                           Time::Unit unit = Time::S) const override;

    void AddTableEntry(Ipv4Address& dstAddr, uint32_t interface);
    void ClearRoutingTable();
    void PrintRoutingTable() const;

    void SetPortSelectPolicy(PortSelectPolicy policy);
    PortSelectPolicy GetPortSelectPolicy() const;

    // Legacy toggle: true -> kByHash, false -> kFirst
    void SetRandomEcmp(bool enable);
    bool GetRandomEcmp() const;


  protected:
    void DoDispose() override;

  private:
    Ptr<Ipv4Route> LookupNodeBfs(const Ipv4Header& header, Ptr<const Packet> p, Ptr<NetDevice> oif = nullptr);
    uint32_t ComputeFlowHash(const Ipv4Header& h, Ptr<const Packet> p);

    Ptr<Ipv4> m_ipv4;
    Ptr<Node> m_node;
    uint32_t m_nodeId;
    Ptr<StructuredTopology> m_topo;
    PortSelectPolicy m_portSelectPolicy{PortSelectPolicy::kFirst};
    std::unordered_map<uint32_t, std::vector<uint32_t>> m_rtTable;
};

} /* namespace ns3 */

#endif /* NODE_ROUTING_H */
