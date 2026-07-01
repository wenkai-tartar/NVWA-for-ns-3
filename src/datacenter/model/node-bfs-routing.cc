/*
 * Author: Wenkai Li (v-wenkaili@microsoft.com)
 */

#include "node-bfs-routing.h"
#include "ns3/node-bfs-routing-helper.h"
#include "ns3/enum.h"
#include "ns3/point-to-point-channel.h"

#include <algorithm>
#include <iostream>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("NodeBfsRouting");


TypeId
NodeBfsRouting::GetTypeId()
{
    static TypeId tid = TypeId("ns3::NodeBfsRouting")
                            .SetParent<Object>()
                            .SetGroupName("Datacenter")
                            .AddAttribute("RandomEcmp",
                                          "Legacy ECMP toggle (true->kByHash, false->kFirst)",
                                          BooleanValue(false),
                                          MakeBooleanAccessor(&NodeBfsRouting::SetRandomEcmp,
                                                              &NodeBfsRouting::GetRandomEcmp),
                                          MakeBooleanChecker())
                            .AddAttribute(
                                "EcmpPolicy",
                                "Port selection policy for ECMP",
                                EnumValue(PortSelectPolicy::kFirst),
                                MakeEnumAccessor<PortSelectPolicy>(
                                    &NodeBfsRouting::GetPortSelectPolicy,
                                    &NodeBfsRouting::SetPortSelectPolicy),
                                MakeEnumChecker(PortSelectPolicy::kFirst,
                                                "kFirst",
                                                PortSelectPolicy::kRandom,
                                                "kRandom",
                                                PortSelectPolicy::kByHash,
                                                "kByHash"));
    return tid;
}

NodeBfsRouting::NodeBfsRouting()
    : m_ipv4(nullptr),
      m_node(nullptr),
      m_topo(nullptr)
{
    NS_LOG_FUNCTION(this);
}

NodeBfsRouting::NodeBfsRouting(Ptr<Node> node, Ptr<StructuredTopology> topo)
    : m_ipv4(nullptr),
      m_node(node),
      m_topo(topo)
{
    NS_LOG_FUNCTION(this);
    m_nodeId = node->GetId();
}

NodeBfsRouting::~NodeBfsRouting()
{
    NS_LOG_FUNCTION(this);
    m_ipv4 = nullptr;
    m_topo = nullptr;
    m_node = nullptr;
}

uint32_t
NodeBfsRouting::ComputeFlowHash(const Ipv4Header& h, Ptr<const Packet> p)
{
    // Extract 5-tuple for flow identification
    union {
        uint8_t u8[12];  // 12 bytes: SIP(4) + DIP(4) + Sport(2) + Dport(2)
        uint32_t u32[3];
    } buf;

    buf.u32[0] = h.GetSource().Get();       // Source IP
    buf.u32[1] = h.GetDestination().Get();  // Destination IP

    // Extract ports from TCP/UDP header
    uint16_t sport = 0;
    uint16_t dport = 0;

    if (p && (h.GetProtocol() == 6 || h.GetProtocol() == 17)) {  // TCP or UDP
        if (p->GetSize() >= 4) {
            uint8_t portBuf[4];
            uint32_t copied = p->CopyData(portBuf, 4);
            if (copied >= 4) {
                sport = (portBuf[0] << 8) | portBuf[1];
                dport = (portBuf[2] << 8) | portBuf[3];
            }
        }
    }

    buf.u32[2] = sport | ((uint32_t)dport << 16);

    // MurmurHash3 algorithm
    // Use node ID as seed for per-node path diversity (consistent with RuleBased routing)
    uint32_t hash = m_nodeId;

    // Process 32-bit blocks
    for (int i = 0; i < 3; i++) {
        uint32_t k = buf.u32[i];
        k *= 0xcc9e2d51;
        k = (k << 15) | (k >> 17);
        k *= 0x1b873593;
        hash ^= k;
        hash = (hash << 13) | (hash >> 19);
        hash = hash * 5 + 0xe6546b64;
    }

    // Finalization
    hash ^= 12;
    hash ^= hash >> 16;
    hash *= 0x85ebca6b;
    hash ^= hash >> 13;
    hash *= 0xc2b2ae35;
    hash ^= hash >> 16;

    return hash;
}

Ptr<Ipv4Route>
NodeBfsRouting::LookupNodeBfs(const Ipv4Header& header, Ptr<const Packet> p, Ptr<NetDevice> oif)
{
    NS_LOG_FUNCTION(this << header.GetDestination() << oif);
    NS_LOG_LOGIC("Looking for route for destination " << header.GetDestination());


    Ptr<Ipv4Route> rtentry = nullptr;

    auto entry = m_rtTable.find(header.GetDestination().Get());

    // no matching entry
    if (entry == m_rtTable.end())
    {
        return nullptr;
    }

    // entry found
    auto& nexthops = entry->second;
    RoutingContext ctx;
    if (m_portSelectPolicy != PortSelectPolicy::kFirst)
    {
        // Compute flow hash using 5-tuple (similar to RuleBasedRouting)
        ctx.SetFlowHash(ComputeFlowHash(header, p));
    }
    PortSelector selector(m_portSelectPolicy);
    auto maybeOut = selector.Pick(nexthops, ctx);
    if (!maybeOut.has_value())
    {
        return nullptr;
    }
    uint32_t outInterface = *maybeOut;

    // create a Ipv4Route object from the selected routing table entry
    rtentry = Create<Ipv4Route>();
    rtentry->SetDestination(header.GetDestination());
    /// \todo handle multi-address case
    Ptr<NetDevice> outDevice = m_node->GetDevice(outInterface);
    rtentry->SetSource(m_ipv4->GetAddress(outInterface, 0).GetLocal());

    Ptr<PointToPointChannel> channel = DynamicCast<PointToPointChannel>(outDevice->GetChannel());
    Ptr<NetDevice> remoteDevice = nullptr;
    for (uint32_t i = 0; i < channel->GetNDevices(); i++)
    {
        if (channel->GetDevice(i) != outDevice)
        {
            remoteDevice = channel->GetDevice(i);
            break;
        }
    }
    Ptr<Ipv4> remoteIpv4 = remoteDevice->GetNode()->GetObject<Ipv4>();
    uint32_t remoteInterfaceIndex = remoteIpv4->GetInterfaceForDevice(remoteDevice);
    Ipv4Address nextHop = remoteIpv4->GetAddress(remoteInterfaceIndex, 0).GetLocal();
    rtentry->SetGateway(nextHop);
    rtentry->SetOutputDevice(outDevice);
    return rtentry;
}

Ptr<Ipv4Route>
NodeBfsRouting::RouteOutput(Ptr<Packet> p,
                            const Ipv4Header& header,
                            Ptr<NetDevice> oif,
                            Socket::SocketErrno& sockerr)
{
    NS_LOG_FUNCTION(this << p << &header << oif << &sockerr);
    //
    // First, see if this is a multicast packet we have a route for.  If we
    // have a route, then send the packet down each of the specified interfaces.
    //
    if (header.GetDestination().IsMulticast())
    {
        NS_LOG_LOGIC("Multicast destination-- returning false");
        return nullptr; // Let other routing protocols try to handle this
    }
    //
    // See if this is a unicast packet we have a route for.
    //
    NS_LOG_LOGIC("Unicast destination- looking up");

    Ptr<Ipv4Route> rtentry = LookupNodeBfs(header, p, oif);

    if (rtentry)
    {
        sockerr = Socket::ERROR_NOTERROR;
    }
    else
    {
        sockerr = Socket::ERROR_NOROUTETOHOST;
    }
    return rtentry;
}

bool
NodeBfsRouting::RouteInput(Ptr<const Packet> p,
                           const Ipv4Header& header,
                           Ptr<const NetDevice> idev,
                           const UnicastForwardCallback& ucb,
                           const MulticastForwardCallback& mcb,
                           const LocalDeliverCallback& lcb,
                           const ErrorCallback& ecb)
{
    NS_LOG_FUNCTION(this << p << header << header.GetSource() << header.GetDestination() << idev
                         << &lcb << &ecb);
    // Check if input device supports IP
    NS_ASSERT(m_ipv4->GetInterfaceForDevice(idev) >= 0);
    uint32_t iif = m_ipv4->GetInterfaceForDevice(idev);

    if (m_ipv4->IsDestinationAddress(header.GetDestination(), iif))
    {
        if (!lcb.IsNull())
        {
            NS_LOG_LOGIC("Local delivery to " << header.GetDestination());
            lcb(p, header, iif);
            return true;
        }
        else
        {
            // The local delivery callback is null.  This may be a multicast
            // or broadcast packet, so return false so that another
            // multicast routing protocol can handle it.  It should be possible
            // to extend this to explicitly check whether it is a unicast
            // packet, and invoke the error callback if so
            return false;
        }
    }

    // Check if input device supports IP forwarding
    if (!m_ipv4->IsForwarding(iif))
    {
        NS_LOG_LOGIC("Forwarding disabled for this interface");
        ecb(p, header, Socket::ERROR_NOROUTETOHOST);
        return true;
    }
    // Next, try to find a route
    NS_LOG_LOGIC("Unicast destination- looking up global route");

    Ptr<Ipv4Route> rtentry = LookupNodeBfs(header, p, nullptr);

    if (rtentry)
    {
        NS_LOG_LOGIC("Found unicast destination- calling unicast callback");
        ucb(rtentry, p, header);
        return true;
    }
    else
    {
        NS_LOG_LOGIC("Did not find unicast destination- returning false");
        return false; // Let other routing protocols try to handle this
                      // route request.
    }
}

void
NodeBfsRouting::NotifyInterfaceUp(uint32_t i)
{
    NS_LOG_FUNCTION(this << i);
    if (m_topo)
    {
        NodeBfsRoutingHelper::RecalculateRoutes(m_topo);
    }
}

void
NodeBfsRouting::NotifyInterfaceDown(uint32_t i)
{
    NS_LOG_FUNCTION(this << i);
    if (m_topo)
    {
        NodeBfsRoutingHelper::RecalculateRoutes(m_topo);
    }
}

void
NodeBfsRouting::NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address)
{
    NS_LOG_FUNCTION(this << interface << address);
}

void
NodeBfsRouting::NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress address)
{
    NS_LOG_FUNCTION(this << interface << address);
}

void
NodeBfsRouting::DoDispose()
{
    NS_LOG_FUNCTION(this);
    m_topo = nullptr;

    Ipv4RoutingProtocol::DoDispose();
}

void
NodeBfsRouting::SetIpv4(Ptr<Ipv4> ipv4)
{
    NS_LOG_FUNCTION(this << ipv4);
    NS_ASSERT(!m_ipv4 && ipv4);
    m_ipv4 = ipv4;
}

void
NodeBfsRouting::SetTopology(Ptr<StructuredTopology> topo)
{
    NS_LOG_FUNCTION(this << topo);
    NS_ASSERT(topo);
    if (!m_topo)
    {
        m_topo = topo;
    }
}

void
NodeBfsRouting::PrintRoutingTable(Ptr<OutputStreamWrapper> stream, Time::Unit unit) const
{
    NS_LOG_FUNCTION(this << stream);
}

void
NodeBfsRouting::PrintRoutingTable() const
{
    std::cout << m_node->GetId() << " Routing table: " << std::endl;
    for (auto i = m_rtTable.begin(); i != m_rtTable.end(); i++)
    {
        uint32_t dst = i->first;
        std::vector<uint32_t> nexts = i->second;
        for (uint32_t k = 0; k < (uint32_t)nexts.size(); k++)
        {
            uint32_t interface = nexts[k];
            std::cout << "To " << dst << " from port: " << interface << std::endl;
        }
    }
}

void
NodeBfsRouting::AddTableEntry(Ipv4Address& dstAddr, uint32_t interface)
{
    uint32_t dip = dstAddr.Get();
    m_rtTable[dip].push_back(interface);
    std::sort(m_rtTable[dip].begin(), m_rtTable[dip].end());
}

void
NodeBfsRouting::SetPortSelectPolicy(PortSelectPolicy policy)
{
    m_portSelectPolicy = policy;
}

PortSelectPolicy
NodeBfsRouting::GetPortSelectPolicy() const
{
    return m_portSelectPolicy;
}

void
NodeBfsRouting::SetRandomEcmp(bool enable)
{
    m_portSelectPolicy = enable ? PortSelectPolicy::kByHash : PortSelectPolicy::kFirst;
}

bool
NodeBfsRouting::GetRandomEcmp() const
{
    return m_portSelectPolicy != PortSelectPolicy::kFirst;
}

void
NodeBfsRouting::ClearRoutingTable()
{
    m_rtTable.clear();
}

} /* namespace ns3 */
