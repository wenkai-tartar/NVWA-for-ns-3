/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "non-minimal-routing-tag.h"

#include "ns3/log.h"

#include <algorithm>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("NonMinimalRoutingTag");
NS_OBJECT_ENSURE_REGISTERED(NonMinimalRoutingTag);

TypeId
NonMinimalRoutingTag::GetTypeId()
{
    static TypeId tid = TypeId("ns3::NonMinimalRoutingTag")
                            .SetParent<Tag>()
                            .SetGroupName("Datacenter")
                            .AddConstructor<NonMinimalRoutingTag>();
    return tid;
}

TypeId
NonMinimalRoutingTag::GetInstanceTypeId() const
{
    return GetTypeId();
}

uint32_t
NonMinimalRoutingTag::GetSerializedSize() const
{
    return 4 + static_cast<uint32_t>(m_rewrites.size()) * (sizeof(uint16_t) + sizeof(uint32_t));
}

void
NonMinimalRoutingTag::Serialize(TagBuffer i) const
{
    uint8_t count = static_cast<uint8_t>(std::min<size_t>(m_rewrites.size(), 255));
    i.WriteU8(m_algo);
    i.WriteU8(m_phase);
    i.WriteU8(m_flags);
    i.WriteU8(count);
    for (uint8_t idx = 0; idx < count; ++idx)
    {
        const auto& entry = m_rewrites[idx];
        i.WriteU16(entry.index);
        i.WriteU32(entry.value);
    }
}

void
NonMinimalRoutingTag::Deserialize(TagBuffer i)
{
    m_algo = i.ReadU8();
    m_phase = i.ReadU8();
    m_flags = i.ReadU8();
    m_count = i.ReadU8();

    m_rewrites.clear();
    m_rewrites.reserve(m_count);
    for (uint8_t idx = 0; idx < m_count; ++idx)
    {
        RewriteEntry entry;
        entry.index = i.ReadU16();
        entry.value = i.ReadU32();
        m_rewrites.push_back(entry);
    }
}

void
NonMinimalRoutingTag::Print(std::ostream& os) const
{
    os << "algo=" << static_cast<uint32_t>(m_algo)
       << " phase=" << static_cast<uint32_t>(m_phase)
       << " flags=" << static_cast<uint32_t>(m_flags)
       << " rewrites=" << static_cast<uint32_t>(m_rewrites.size());
    for (const auto& entry : m_rewrites)
    {
        os << " [" << entry.index << "->" << entry.value << "]";
    }
}

void
NonMinimalRoutingTag::Reset()
{
    m_algo = static_cast<uint8_t>(NonMinimalAlgorithm::kNone);
    m_phase = static_cast<uint8_t>(NonMinimalPhase::kMin);
    m_flags = 0;
    m_count = 0;
    m_rewrites.clear();
}

void
NonMinimalRoutingTag::SetAlgorithm(NonMinimalAlgorithm algo)
{
    m_algo = static_cast<uint8_t>(algo);
}

NonMinimalAlgorithm
NonMinimalRoutingTag::GetAlgorithm() const
{
    return static_cast<NonMinimalAlgorithm>(m_algo);
}

void
NonMinimalRoutingTag::SetPhase(NonMinimalPhase phase)
{
    m_phase = static_cast<uint8_t>(phase);
}

NonMinimalPhase
NonMinimalRoutingTag::GetPhase() const
{
    return static_cast<NonMinimalPhase>(m_phase);
}

void
NonMinimalRoutingTag::SetFlags(uint8_t flags)
{
    m_flags = flags;
}

uint8_t
NonMinimalRoutingTag::GetFlags() const
{
    return m_flags;
}

void
NonMinimalRoutingTag::ClearRewrite()
{
    m_rewrites.clear();
    m_count = 0;
}

void
NonMinimalRoutingTag::AddRewrite(uint16_t index, uint32_t value)
{
    if (m_rewrites.size() >= 255)
    {
        return;
    }
    m_rewrites.push_back({index, value});
    m_count = static_cast<uint8_t>(m_rewrites.size());
}

void
NonMinimalRoutingTag::SetRewrite(const std::vector<RewriteEntry>& rewrites)
{
    m_rewrites = rewrites;
    if (m_rewrites.size() > 255)
    {
        m_rewrites.resize(255);
    }
    m_count = static_cast<uint8_t>(m_rewrites.size());
}

const std::vector<NonMinimalRoutingTag::RewriteEntry>&
NonMinimalRoutingTag::GetRewrites() const
{
    return m_rewrites;
}

bool
NonMinimalRoutingTag::HasRewrites() const
{
    return !m_rewrites.empty();
}

} // namespace ns3
