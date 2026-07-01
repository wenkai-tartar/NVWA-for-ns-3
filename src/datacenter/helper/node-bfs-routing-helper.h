/*
 * Author: Wenkai Li (v-wenkaili@microsoft.com)
 */

#ifndef NODE_ROUTING_HELPER_H
#define NODE_ROUTING_HELPER_H

#include "ns3/ipv4-address.h"
#include "ns3/ipv4-routing-helper.h"
#include "ns3/node-bfs-routing.h"
#include "ns3/structured-topology.h"

#include <cstdint>

namespace ns3
{


/**
 * \ingroup datacenterHelpers
 *
 * \brief Helper class that adds ns3::NodeRouting objects
 */
class NodeBfsRoutingHelper : public Ipv4RoutingHelper
{
  public:
    /**
     * @brief Construct a helper class to make life easier while doing simple layered
     * address assignment in scripts.
     */
    NodeBfsRoutingHelper();

    NodeBfsRoutingHelper(Ptr<StructuredTopology> topo);

    /**
     * \brief Construct a NodeBfsRoutingHelper from another previously initialized
     * instance (Copy Constructor).
     * \param o object to be copied
     */
    NodeBfsRoutingHelper(const NodeBfsRoutingHelper& o);

    ~NodeBfsRoutingHelper();

    // Delete assignment operator to avoid misuse
    NodeBfsRoutingHelper& operator=(const NodeBfsRoutingHelper&) = delete;

    /**
     * \returns pointer to clone of this NodeBfsRoutingHelper
     *
     * This method is mainly for internal use by the other helpers;
     * clients are expected to free the dynamic memory allocated by this method
     */
    NodeBfsRoutingHelper* Copy() const override;

    /**
     * \param node the node on which the routing protocol will run
     * \returns a newly-created routing protocol
     *
     * This method will be called by ns3::InternetStackHelper::Install
     */
    Ptr<Ipv4RoutingProtocol> Create(Ptr<Node> node) const override;

    static void CalculateRoutes(Ptr<StructuredTopology> topo);
    static void CalculateRoute(Ptr<StructuredTopology> topo, Ptr<Node> host);
    static void CalculateRoutesWithHost(Ptr<StructuredTopology> topo);
    static void CalculateRouteWithHost(Ptr<StructuredTopology> topo, Ptr<Node> host);
    static void EnableStrictBaseline(Ptr<StructuredTopology> topo, bool enable, bool withHost = false);
    static void SetRoutingEntries(Ptr<StructuredTopology> topo);
    static void RecalculateRoutes(Ptr<StructuredTopology> topo);
    static void SuppressRecalculate(bool enable);
    static bool IsRecalculateSuppressed();
    static void SetEcmpPolicy(Ptr<StructuredTopology> topo, PortSelectPolicy policy);
    static void EnableEcmp(Ptr<StructuredTopology> topo, bool enable);
    static void PrintRoutingEntries(Ptr<StructuredTopology> topo);
    static uint64_t GetRoutingEntryNumber(Ptr<StructuredTopology> topo);

  private:
    Ptr<StructuredTopology> m_topo;

    // nextHop[node][neighbor] = {neighbor, interface index}
    static std::map<Ptr<Node>, std::map<Ptr<Node>, std::vector<std::pair<Ptr<Node>, uint32_t>>>>
        m_nextHop;
    static std::map<uint32_t, bool> m_isHost;
    static bool m_withHost;
    static bool m_strictBaseline;
    static bool m_suppressRecalc;
    static std::map<uint32_t, std::map<uint32_t, uint32_t>> m_baseDist;
    static void CalculateBaselineDistances(Ptr<StructuredTopology> topo, bool withHost);
};

} /* namespace ns3 */

#endif /* NODE_ROUTING_HELPER_H */
