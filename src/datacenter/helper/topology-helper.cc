

#include "topology-helper.h"

#include "ns3/config.h"
#include "ns3/string.h"
#include "ns3/structured-topology.h"
#include "ns3/uinteger.h"

namespace ns3
{

TopologyHelper::TopologyHelper()
    : m_rate(DataRate("10Gbps")),
      m_delay(MicroSeconds(1)),
      m_mtu(1500)
{
    ApplyLinkAttributes();

    // Default IPv4 plan: /30 per link starting at 10.0.0.0/30
    // SetBase("10.0.0.0", "255.255.255.252", "0.0.0.1");
    SetBase("10.0.0.0", "255.0.0.0", "0.0.0.1");
}

void
TopologyHelper::ApplyLinkAttributes()
{
    m_p2p.SetDeviceAttribute("DataRate", DataRateValue(m_rate));
    m_p2p.SetChannelAttribute("Delay", TimeValue(m_delay));
    m_p2p.SetDeviceAttribute("Mtu", UintegerValue(m_mtu));
}

void
TopologyHelper::SetLinkAttributes(const DataRate& rate, const Time& delay, uint16_t mtu)
{
    m_rate = rate;
    m_delay = delay;
    m_mtu = mtu;
    ApplyLinkAttributes();
}

void
TopologyHelper::SetLinkAttributes(const std::string& rate, const std::string& delay, uint16_t mtu)
{
    m_rate = DataRate(rate);
    m_delay = Time(delay);
    m_mtu = mtu;
    ApplyLinkAttributes();
}

void
TopologyHelper::SetBase(const std::string& network,
                        const std::string& mask,
                        const std::string& address)
{
    m_baseNetwork = network;
    m_baseMask = mask;
    m_baseAddress = address;
    m_ipv4.SetBase(Ipv4Address(network.c_str()),
                   Ipv4Mask(mask.c_str()),
                   Ipv4Address(address.c_str()));
}

void
TopologyHelper::NewNetwork()
{
    m_ipv4.NewNetwork();
}

void
TopologyHelper::InstallInternetStack(const NodeContainer& nodes)
{
    // Install IPv4/ARP/ICMP, etc., to the given nodes.
    m_stack.Install(nodes);
}

std::pair<uint32_t, uint32_t>
TopologyHelper::ConnectNodes(StructuredTopology& topo,
                             uint32_t levelA,
                             uint32_t dimA,
                             uint32_t localA,
                             uint32_t levelB,
                             uint32_t localB)
{
    // boundary checks
    if (levelA >= topo.GetNumLevels() || levelB >= topo.GetNumLevels())
    {
        return std::make_pair(UINT32_MAX, UINT32_MAX);
    }
    if (localA >= topo.GetLevel(levelA).GetN() || localB >= topo.GetLevel(levelB).GetN())
    {
        return std::make_pair(UINT32_MAX, UINT32_MAX);
    }

    Ptr<Node> a = topo.GetNodeByLocal(levelA, localA);
    Ptr<Node> b = topo.GetNodeByLocal(levelB, localB);

    // always connect (Connect handles duplication internally via device count)
    NetDeviceContainer devs = m_p2p.Install(a, b);
    Ipv4InterfaceContainer ifc = m_ipv4.Assign(devs);
    // m_ipv4.NewNetwork();

    // Update adjacency table in StructuredTopology directly (using friend access)
    auto pairAB = std::make_pair(levelB, localB);
    auto& adjA = topo.m_adj[levelA][dimA][localA];
    // if (std::find(adjA.begin(), adjA.end(), pairAB) == adjA.end())
    // {
    adjA.emplace_back(pairAB);
    // }

    // NS_ASSERT_MSG(levelB <= levelA, "levelB > levelA");
    // auto pairBA = std::make_pair(levelA, localA);
    // auto& adjB = topo.m_adj[levelB][dimA][localB];
    // // if (std::find(adjB.begin(), adjB.end(), pairBA) == adjB.end())
    // // {
    // adjB.emplace_back(pairBA);
    // // }
    return std::make_pair(devs.Get(0)->GetIfIndex(), devs.Get(1)->GetIfIndex());
}

} // namespace ns3
