/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#ifndef TOPOLOGY_HELPER_H
#define TOPOLOGY_HELPER_H

#include "ns3/data-rate.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/ipv4-interface-container.h"
#include "ns3/net-device-container.h"
#include "ns3/node-container.h"
#include "ns3/node.h"
#include "ns3/nstime.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/ptr.h"

#include <string>
#include <utility>
#include <vector>

namespace ns3
{
class StructuredTopology;

/**
 * TopologyHelper
 * A lightweight wrapper over PointToPointHelper + Ipv4AddressHelper.
 * - Configure link attributes once, reuse for many links.
 * - Install P2P devices/channels between nodes.
 * - Assign IPv4 addresses per-link and auto-advance to next subnet.
 */
class TopologyHelper
{
  public:
    TopologyHelper();

    // Link attributes
    void SetLinkAttributes(const DataRate& rate, const Time& delay, uint16_t mtu = 1500);
    void SetLinkAttributes(const std::string& rate, const std::string& delay, uint16_t mtu = 1500);

    // IPv4 addressing plan
    // Set the base network/mask and the initial address (host part).
    // Example: SetBase("10.0.0.0", "255.255.255.252"); // /30 per-link
    void SetBase(const std::string& network,
                 const std::string& mask,
                 const std::string& address = "0.0.0.1");

    // Move to the next network block (use after Assign if you want manual control).
    void NewNetwork();

    // Internet stack install
    void InstallInternetStack(const NodeContainer& nodes);

    // Connect helpers
    // Only install devices & channel, without IP assignment
    NetDeviceContainer Connect(Ptr<Node> a, Ptr<Node> b);

    // Assign IPv4 on the "current" subnet to an existing device container, then advance
    Ipv4InterfaceContainer Assign(const NetDeviceContainer& ndc);

    // Connect nodes in StructuredTopology with adjacency tracking
    std::pair<uint32_t, uint32_t> ConnectNodes(StructuredTopology& topo,
                                               uint32_t levelA,
                                               uint32_t dimA,
                                               uint32_t localA,
                                               uint32_t levelB,
                                               uint32_t localB);

    PointToPointHelper& P2p()
    {
        return m_p2p;
    }

    const PointToPointHelper& P2p() const
    {
        return m_p2p;
    }

    Ipv4AddressHelper& Ipv4()
    {
        return m_ipv4;
    }

    const Ipv4AddressHelper& Ipv4() const
    {
        return m_ipv4;
    }

    InternetStackHelper& GetInternetStack()
    {
        return m_stack;
    }

  private:
    // Internal: set p2p attributes for rate/delay/mtu
    void ApplyLinkAttributes();

    // Link config
    DataRate m_rate;
    Time m_delay;
    uint16_t m_mtu;

    // Helpers
    PointToPointHelper m_p2p;
    InternetStackHelper m_stack;
    Ipv4AddressHelper m_ipv4;

    std::string m_baseNetwork;
    std::string m_baseMask;
    std::string m_baseAddress;
};

} // namespace ns3

#endif // TOPOLOGY_HELPER_H
