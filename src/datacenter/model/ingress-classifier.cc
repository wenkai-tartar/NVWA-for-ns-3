/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "ingress-classifier.h"

#include "ns3/log.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("IngressClassifier");

void
PortSetIngressClassifier::Build(const PortSet& ports, uint32_t numIfs)
{
    m_byIf.assign(numIfs, IngressInfo::Unknown());

    // Downward buckets
    for (const auto& kv : ports.GetDownwardBuckets())
    {
        const std::string& key = kv.first;
        const auto& bucket = kv.second;
        for (uint32_t port : bucket)
        {
            IngressInfo info;
            info.direction = IngressDirection::kDownward;
            info.bucketKey = key;
            SetIf(port, info, "downward");
        }
    }

    // Upward buckets
    for (const auto& kv : ports.GetUpwardBuckets())
    {
        const std::string& key = kv.first;
        const auto& bucket = kv.second;
        for (uint32_t port : bucket)
        {
            IngressInfo info;
            info.direction = IngressDirection::kUpward;
            info.bucketKey = key;
            SetIf(port, info, "upward");
        }
    }

    // Same-level buckets
    const auto& sameLevel = ports.GetSameLevelBuckets();
    for (uint32_t dim = 0; dim < sameLevel.size(); ++dim)
    {
        for (const auto& kv : sameLevel[dim])
        {
            const std::string& key = kv.first;
            const auto& bucket = kv.second;
            for (uint32_t port : bucket)
            {
                IngressInfo info;
                info.direction = IngressDirection::kSameLevel;
                info.dimId = static_cast<int32_t>(dim);
                info.bucketKey = key;
                SetIf(port, info, "same-level");
            }
        }
    }
}

const IngressInfo&
PortSetIngressClassifier::Get(uint32_t ifIndex) const
{
    if (ifIndex < m_byIf.size())
    {
        return m_byIf[ifIndex];
    }
    return m_unknown;
}

void
PortSetIngressClassifier::SetIf(uint32_t ifIndex, const IngressInfo& info, const char* reason)
{
    if (ifIndex >= m_byIf.size())
    {
        return;
    }

    IngressInfo& existing = m_byIf[ifIndex];
    if (existing.direction == IngressDirection::kUnknown)
    {
        existing = info;
        return;
    }

    bool conflict = (existing.direction != info.direction) ||
                    (existing.direction == IngressDirection::kSameLevel &&
                     existing.dimId != info.dimId) ||
                    (!info.bucketKey.empty() && existing.bucketKey != info.bucketKey);

    if (conflict)
    {
        NS_LOG_WARN("Ingress classifier conflict on ifIndex " << ifIndex << " reason=" << reason);
        IngressInfo unknown = IngressInfo::Unknown();
        unknown.meta["conflict"] = 1;
        existing = unknown;
    }
}

} // namespace ns3
