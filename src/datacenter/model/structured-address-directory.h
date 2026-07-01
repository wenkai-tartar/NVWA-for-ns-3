/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#ifndef STRUCTURED_ADDRESS_DIRECTORY_H
#define STRUCTURED_ADDRESS_DIRECTORY_H

#include "structured-address.h"

#include "ns3/ipv4-address.h"
#include "ns3/object.h"

#include <unordered_map>

namespace ns3
{

/**
 * Global directory mapping: Ipv4Address -> StructuredAddress.
 * Filled during topology build; read at forwarding time.
 */
class StructuredAddressDirectory : public Object
{
  public:
    static TypeId GetTypeId();

    /** Singleton accessor */
    static Ptr<StructuredAddressDirectory> Get();

    void Register(Ipv4Address ip, const StructuredAddress& saddr);
    bool Has(Ipv4Address ip) const;
    StructuredAddress Lookup(Ipv4Address ip) const;
    const StructuredAddress& LookupRef(Ipv4Address ip) const;

    /** Optional: clear all (useful for tests) */
    void Clear();

  private:
    static Ptr<StructuredAddressDirectory> m_singleton;
    std::unordered_map<uint32_t, StructuredAddress> m_map;
};

} // namespace ns3

#endif // STRUCTURED_ADDRESS_DIRECTORY_H
