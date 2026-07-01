/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#include "structured-address-directory.h"

#include "ns3/log.h"
#include "ns3/simulator.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("StructuredAddressDirectory");
NS_OBJECT_ENSURE_REGISTERED(StructuredAddressDirectory);
Ptr<StructuredAddressDirectory> StructuredAddressDirectory::m_singleton;

TypeId
StructuredAddressDirectory::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::StructuredAddressDirectory").SetParent<Object>().SetGroupName("Datacenter");
    return tid;
}

Ptr<StructuredAddressDirectory>
StructuredAddressDirectory::Get()
{
    if (!m_singleton)
    {
        m_singleton = CreateObject<StructuredAddressDirectory>();
    }
    return m_singleton;
}

void
StructuredAddressDirectory::Register(Ipv4Address ip, const StructuredAddress& saddr)
{
    m_map[ip.Get()] = saddr;
}

bool
StructuredAddressDirectory::Has(Ipv4Address ip) const
{
    return m_map.find(ip.Get()) != m_map.end();
}

StructuredAddress
StructuredAddressDirectory::Lookup(Ipv4Address ip) const
{
    return LookupRef(ip);
}

const StructuredAddress&
StructuredAddressDirectory::LookupRef(Ipv4Address ip) const
{
    auto it = m_map.find(ip.Get());
    if (it == m_map.end())
    {
        NS_ABORT_MSG("StructuredAddressDirectory: missing key for " << ip);
    }
    return it->second;
}

void
StructuredAddressDirectory::Clear()
{
    m_map.clear();
}

} // namespace ns3
