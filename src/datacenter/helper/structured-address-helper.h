/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#ifndef STRUCTURED_ADDRESS_HELPER_H
#define STRUCTURED_ADDRESS_HELPER_H

#include "ns3/structured-address.h"

#include <cstddef>
#include <vector>

namespace ns3
{

/**
 * @brief Helper class for structured address operations and comparisons
 *
 * This class provides utilities for comparing structured addresses
 * within specified ranges and performing common address matching operations.
 */
class StructuredAddressHelper
{
  public:
    using Field = StructuredAddress::Field;

    /**
     * @brief Compare the field at the specified index
     *
     * @param src The source address
     * @param dst The destination address
     * @param index The index of the field to compare
     * @return true if the fields match, false otherwise
     */
    static bool CompareField(const StructuredAddress& src,
                             const StructuredAddress& dst,
                             size_t index);
    /**
     * @brief Compare two addresses from position l to position r (inclusive)
     *
     * @param src The source address
     * @param dst The destination address
     * @param l The starting position (0-based, inclusive)
     * @param r The ending position (0-based, inclusive)
     * @return true if all positions match, false otherwise
     *
     * If l > r, or if either l or r is out of range for either address,
     * returns false.
     */
    static bool CompareRange(const StructuredAddress& src,
                             const StructuredAddress& dst,
                             size_t l,
                             size_t r);

    /**
     * @brief Compare the prefix of two addresses
     *
     * @param src The source address
     * @param dst The destination address
     * @param position The position to compare from the highest bit
     * @return true if the prefix matches, false otherwise
     *
     * If position exceeds address size, returns true.
     */
    static bool ComparePrefix(const StructuredAddress& src,
                              const StructuredAddress& dst,
                              size_t position);
    /**
     * @brief Compare the suffix of two addresses
     *
     * @param src The source address
     * @param dst The destination address
     * @param length The number of positions to compare from the end
     * @return true if the last 'length' positions match, false otherwise
     *
     * If length is 0, returns true.
     * If length exceeds either address size, returns false.
     */
    static bool CompareSuffix(const StructuredAddress& src,
                              const StructuredAddress& dst,
                              size_t length);
};

} // namespace ns3

#endif // STRUCTURED_ADDRESS_HELPER_H