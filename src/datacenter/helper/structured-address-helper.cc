/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#include "structured-address-helper.h"

#include <cstddef>
#include <cstdint>

namespace ns3
{

bool
StructuredAddressHelper::CompareField(const StructuredAddress& src,
                                      const StructuredAddress& dst,
                                      size_t index)
{
    if (index >= src.Size() || index >= dst.Size())
    {
        return false;
    }
    return src[index] == dst[index];
}

bool
StructuredAddressHelper::CompareRange(const StructuredAddress& src,
                                      const StructuredAddress& dst,
                                      size_t l,
                                      size_t r)
{
    // Validate input parameters
    if (l > r)
    {
        return false;
    }

    const size_t srcSize = src.Size();
    const size_t dstSize = dst.Size();

    // Check if the range is valid for both addresses
    if (l >= srcSize || l >= dstSize || r >= srcSize || r >= dstSize)
    {
        return false;
    }

    // Compare each position in the range
    for (size_t i = l; i <= r; ++i)
    {
        if (src[i] != dst[i])
        {
            return false;
        }
    }

    return true;
}

bool
StructuredAddressHelper::ComparePrefix(const StructuredAddress& src,
                                       const StructuredAddress& dst,
                                       size_t position)
{
    const size_t size = src.Size();
    // Handle edge cases
    if (position >= size)
    {
        return true;
    }
    if (dst.Size() < size)
    {
        return false;
    }

    const uint32_t* srcData = src.Data().data();
    const uint32_t* dstData = dst.Data().data();
    size_t length = size - position;

    const uint32_t* srcStart = srcData + position;
    const uint32_t* dstStart = dstData + position;

    return std::equal(srcStart, srcStart + length, dstStart, dstStart + length);
}

bool
StructuredAddressHelper::CompareSuffix(const StructuredAddress& src,
                                       const StructuredAddress& dst,
                                       size_t length)
{
    // Handle edge cases
    if (length == 0)
    {
        return true;
    }

    const size_t srcSize = src.Size();
    const size_t dstSize = dst.Size();

    // Check if length exceeds either address size
    if (length > srcSize || length > dstSize)
    {
        return false;
    }

    // Compare the last 'length' positions
    for (size_t i = 0; i < length; ++i)
    {
        if (src[srcSize - length + i] != dst[dstSize - length + i])
        {
            return false;
        }
    }

    return true;
}

} // namespace ns3