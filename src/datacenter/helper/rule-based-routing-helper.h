/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#ifndef RULE_BASED_ROUTING_HELPER_H
#define RULE_BASED_ROUTING_HELPER_H

#include "ns3/ipv4-address.h"
#include "ns3/ipv4-routing-helper.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/net-device-container.h"
#include "ns3/routing-rule.h"
#include "ns3/rule-based-routing.h"
#include "ns3/structured-topology.h"

namespace ns3
{

/**
 * \ingroup datacenterHelpers
 *
 * \brief Helper class that adds ns3::RuleBasedRouting objects
 */
class RuleBasedRoutingHelper : public Ipv4RoutingHelper
{
  public:
    /**
     * @brief Construct a helper class to make life easier while doing simple layered
     * address assignment in scripts.
     */
    RuleBasedRoutingHelper();

    /**
     * \brief Construct a RuleBasedRoutingHelper from another previously initialized
     * instance (Copy Constructor).
     * \param o object to be copied
     */
    RuleBasedRoutingHelper(const RuleBasedRoutingHelper& o);

    // Delete assignment operator to avoid misuse
    RuleBasedRoutingHelper& operator=(const RuleBasedRoutingHelper&) = delete;

    /**
     * \returns pointer to clone of this RuleBasedRoutingHelper
     *
     * This method is mainly for internal use by the other helpers;
     * clients are expected to free the dynamic memory allocated by this method
     */
    RuleBasedRoutingHelper* Copy() const override;

    /**
     * \param node the node on which the routing protocol will run
     * \returns a newly-created routing protocol
     *
     * This method will be called by ns3::InternetStackHelper::Install
     */
    Ptr<Ipv4RoutingProtocol> Create(Ptr<Node> node) const override;

    void Initialize(StructuredTopology& topo);

  private:
};

} /* namespace ns3 */

#endif /* RULE_BASED_ROUTING_HELPER_H */