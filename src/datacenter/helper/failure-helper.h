/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#ifndef FAILURE_HELPER_H
#define FAILURE_HELPER_H

#include "ns3/nstime.h"
#include "ns3/ptr.h"

#include <cstdint>
#include <string>

namespace ns3
{

class StructuredTopology;

/**
 * \brief Helper class for managing link failures in a StructuredTopology
 *
 * This class provides functionality to:
 * - Parse failure configuration from JSON files
 * - Schedule link failure and recovery events
 * - Set link state (up/down) for specific links
 */
class FailureHelper
{
  public:
    FailureHelper();
    ~FailureHelper();

    /**
     * \brief Load and schedule failures from a JSON configuration file
     * \param filename Path to the JSON failure configuration file
     * \param topo Pointer to the StructuredTopology
     *
     * This function parses the JSON file and schedules all failure and recovery
     * events using ns3::Simulator::Schedule
     */
    void LoadFailuresFromJson(const std::string& filename, Ptr<StructuredTopology> topo);

    /**
     * \brief Apply failures immediately from a JSON configuration file (no scheduling).
     * \param filename Path to the JSON failure configuration file
     * \param topo Pointer to the StructuredTopology
     */
    static void ApplyFailuresFromJsonNow(const std::string& filename, Ptr<StructuredTopology> topo);

    /**
     * \brief Set a link down (failed state)
     * \param topo Pointer to the StructuredTopology
     * \param srcLevel Source node level ID
     * \param srcLocalIndex Source node local index within the level
     * \param dstLevel Destination node level ID
     * \param dstLocalIndex Destination node local index within the level
     *
     * Sets both directions of the link to down state
     */
    static void SetLinkDown(Ptr<StructuredTopology> topo,
                            uint32_t srcLevel,
                            uint32_t srcLocalIndex,
                            uint32_t dstLevel,
                            uint32_t dstLocalIndex);

    /**
     * \brief Set a link up (recovered state)
     * \param topo Pointer to the StructuredTopology
     * \param srcLevel Source node level ID
     * \param srcLocalIndex Source node local index within the level
     * \param dstLevel Destination node level ID
     * \param dstLocalIndex Destination node local index within the level
     *
     * Sets both directions of the link to up state
     */
    static void SetLinkUp(Ptr<StructuredTopology> topo,
                          uint32_t srcLevel,
                          uint32_t srcLocalIndex,
                          uint32_t dstLevel,
                          uint32_t dstLocalIndex);


  private:
    /**
     * \brief Parse time string with unit (e.g., "5.0s", "100ms")
     * \param value Time value as double
     * \param unit Time unit string ("s", "ms", "us", "ns")
     * \return Time object
     */
    static Time ParseTime(double value, const std::string& unit);
};

} // namespace ns3

#endif /* FAILURE_HELPER_H */
