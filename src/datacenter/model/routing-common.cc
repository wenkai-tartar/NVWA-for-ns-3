/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#include "routing-common.h"

#include "ns3/log.h"

#include <algorithm>
#include <limits>
#include <random>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("RoutingCommon");

const std::vector<uint32_t> PortSet::kEmpty = {};
const std::vector<std::string> PortSet::kEmptyKeys = {};

bool
PortSet::TryParseSingleFieldKey(const std::string& key, uint32_t* value)
{
    if (key.empty())
    {
        return false;
    }

    uint64_t parsed = 0;
    for (char c : key)
    {
        if (c < '0' || c > '9')
        {
            return false;
        }
        parsed = parsed * 10 + static_cast<uint64_t>(c - '0');
        if (parsed > std::numeric_limits<uint32_t>::max())
        {
            return false;
        }
    }

    if (value)
    {
        *value = static_cast<uint32_t>(parsed);
    }
    return true;
}

PortSet::PortSet(size_t sameLevelDimensions)
    : m_sameLevelBuckets(sameLevelDimensions)
{
}

void
PortSet::SetUpward(const std::string& addressBit, std::vector<uint32_t> ports)
{
    m_upwardBuckets[addressBit] = ports;
}

void
PortSet::AddUpward(const std::string& addressBit, uint32_t port)
{
    m_upwardBuckets[addressBit].push_back(port);
}

const std::vector<uint32_t>&
PortSet::GetUpward(const std::string& addressBit) const
{
    return m_upwardBuckets.find(addressBit) != m_upwardBuckets.end()
               ? m_upwardBuckets.find(addressBit)->second
               : kEmpty;
}

void
PortSet::SetDownward(const std::string& addressBit, std::vector<uint32_t> ports)
{
    uint32_t fieldKey = 0;
    const bool hasFieldKey = TryParseSingleFieldKey(addressBit, &fieldKey);

    // If overwriting an existing bucket, remove old reverse-index entries first.
    auto itOld = m_downwardBuckets.find(addressBit);
    if (itOld != m_downwardBuckets.end())
    {
        for (uint32_t oldPort : itOld->second)
        {
            auto itMap = m_downwardKeysByPort.find(oldPort);
            if (itMap != m_downwardKeysByPort.end())
            {
                auto& keys = itMap->second;
                keys.erase(std::remove(keys.begin(), keys.end(), addressBit), keys.end());
                if (keys.empty())
                {
                    m_downwardKeysByPort.erase(itMap);
                }
            }
        }
    }

    m_downwardBuckets[addressBit] = ports;
    if (hasFieldKey)
    {
        m_downwardFieldBuckets[fieldKey] = ports;
        m_availableDownwardFieldBuckets.erase(fieldKey);
    }

    // Build reverse index.
    for (uint32_t port : ports)
    {
        auto& keys = m_downwardKeysByPort[port];
        if (std::find(keys.begin(), keys.end(), addressBit) == keys.end())
        {
            keys.push_back(addressBit);
        }
    }
}

void
PortSet::SetDownward(uint32_t addressBit, std::vector<uint32_t> ports)
{
    SetDownward(std::to_string(addressBit), std::move(ports));
}

void
PortSet::AddDownward(const std::string& addressBit, uint32_t port)
{
    m_downwardBuckets[addressBit].push_back(port);

    uint32_t fieldKey = 0;
    if (TryParseSingleFieldKey(addressBit, &fieldKey))
    {
        m_downwardFieldBuckets[fieldKey].push_back(port);
        m_availableDownwardFieldBuckets.erase(fieldKey);
    }

    // Maintain reverse index (port -> bucket keys).
    auto& keys = m_downwardKeysByPort[port];
    if (std::find(keys.begin(), keys.end(), addressBit) == keys.end())
    {
        keys.push_back(addressBit);
    }
}

void
PortSet::AddDownward(uint32_t addressBit, uint32_t port)
{
    AddDownward(std::to_string(addressBit), port);
}

const std::vector<uint32_t>&
PortSet::GetDownward(const std::string& addressBit) const
{
    auto it = m_downwardBuckets.find(addressBit);
    return it != m_downwardBuckets.end() ? it->second : kEmpty;
}

const std::vector<uint32_t>&
PortSet::GetDownward(uint32_t addressBit) const
{
    auto it = m_downwardFieldBuckets.find(addressBit);
    return it != m_downwardFieldBuckets.end() ? it->second : kEmpty;
}

const std::vector<std::string>&
PortSet::GetDownwardKeysByPort(uint32_t port) const
{
    auto it = m_downwardKeysByPort.find(port);
    return it != m_downwardKeysByPort.end() ? it->second : kEmptyKeys;
}

void
PortSet::SetSameLevel(uint32_t dimension,
                      const std::string& addressBit,
                      std::vector<uint32_t> ports)
{
    if (m_sameLevelBuckets.size() <= dimension)
    {
        m_sameLevelBuckets.resize(dimension + 1);
    }
    m_sameLevelBuckets[dimension][addressBit] = ports;
}

void
PortSet::AddSameLevel(uint32_t dimension, const std::string& addressBit, uint32_t port)
{
    if (m_sameLevelBuckets.size() <= dimension)
    {
        m_sameLevelBuckets.resize(dimension + 1);
    }
    m_sameLevelBuckets[dimension][addressBit].push_back(port);
}

const std::vector<uint32_t>&
PortSet::GetSameLevel(uint32_t dimension, const std::string& addressBit) const
{
    return m_sameLevelBuckets[dimension].find(addressBit) != m_sameLevelBuckets[dimension].end()
               ? m_sameLevelBuckets[dimension].find(addressBit)->second
               : kEmpty;
}

const std::unordered_map<std::string, std::vector<uint32_t>>&
PortSet::GetUpwardBuckets() const
{
    return m_upwardBuckets;
}

const std::unordered_map<std::string, std::vector<uint32_t>>&
PortSet::GetDownwardBuckets() const
{
    return m_downwardBuckets;
}

const std::vector<std::unordered_map<std::string, std::vector<uint32_t>>>&
PortSet::GetSameLevelBuckets() const
{
    return m_sameLevelBuckets;
}

// ============ Failure-aware port management ============

void
PortSet::MarkPortDown(uint32_t port)
{
    if (m_failedPorts.find(port) == m_failedPorts.end())
    {
        m_failedPorts.insert(port);
        RebuildAvailableLists();
        NS_LOG_INFO("Port " << port << " marked as DOWN");
    }
}

void
PortSet::MarkPortUp(uint32_t port)
{
    if (m_failedPorts.find(port) != m_failedPorts.end())
    {
        m_failedPorts.erase(port);
        RebuildAvailableLists();
        NS_LOG_INFO("Port " << port << " marked as UP");
    }
}

bool
PortSet::IsPortDown(uint32_t port) const
{
    return m_failedPorts.find(port) != m_failedPorts.end();
}

void
PortSet::RebuildAvailableLists()
{
    // Rebuild upward available buckets
    m_availableUpwardBuckets.clear();
    for (const auto& bucket : m_upwardBuckets)
    {
        std::vector<uint32_t> available;
        for (uint32_t port : bucket.second)
        {
            if (m_failedPorts.find(port) == m_failedPorts.end())
            {
                available.push_back(port);
            }
        }
        m_availableUpwardBuckets[bucket.first] = std::move(available);
    }

    // Rebuild downward available buckets
    m_availableDownwardBuckets.clear();
    for (const auto& bucket : m_downwardBuckets)
    {
        std::vector<uint32_t> available;
        for (uint32_t port : bucket.second)
        {
            if (m_failedPorts.find(port) == m_failedPorts.end())
            {
                available.push_back(port);
            }
        }
        m_availableDownwardBuckets[bucket.first] = std::move(available);
    }

    m_availableDownwardFieldBuckets.clear();
    for (const auto& bucket : m_downwardFieldBuckets)
    {
        std::vector<uint32_t> available;
        for (uint32_t port : bucket.second)
        {
            if (m_failedPorts.find(port) == m_failedPorts.end())
            {
                available.push_back(port);
            }
        }
        m_availableDownwardFieldBuckets[bucket.first] = std::move(available);
    }

    // Rebuild same-level available buckets
    m_availableSameLevelBuckets.clear();
    m_availableSameLevelBuckets.resize(m_sameLevelBuckets.size());
    for (size_t dim = 0; dim < m_sameLevelBuckets.size(); ++dim)
    {
        for (const auto& bucket : m_sameLevelBuckets[dim])
        {
            std::vector<uint32_t> available;
            for (uint32_t port : bucket.second)
            {
                if (m_failedPorts.find(port) == m_failedPorts.end())
                {
                    available.push_back(port);
                }
            }
            m_availableSameLevelBuckets[dim][bucket.first] = std::move(available);
        }
    }
}

const std::vector<uint32_t>&
PortSet::GetAvailableUpward(const std::string& addressBit) const
{
    // If no failed ports, return original list directly
    if (m_failedPorts.empty())
    {
        return GetUpward(addressBit);
    }

    auto it = m_availableUpwardBuckets.find(addressBit);
    return it != m_availableUpwardBuckets.end() ? it->second : kEmpty;
}

const std::vector<uint32_t>&
PortSet::GetAvailableDownward(const std::string& addressBit) const
{
    // If no failed ports, return original list directly
    if (m_failedPorts.empty())
    {
        return GetDownward(addressBit);
    }

    auto it = m_availableDownwardBuckets.find(addressBit);
    return it != m_availableDownwardBuckets.end() ? it->second : kEmpty;
}

const std::vector<uint32_t>&
PortSet::GetAvailableDownward(uint32_t addressBit) const
{
    // If no failed ports, return original list directly
    if (m_failedPorts.empty())
    {
        return GetDownward(addressBit);
    }

    auto it = m_availableDownwardFieldBuckets.find(addressBit);
    return it != m_availableDownwardFieldBuckets.end() ? it->second : kEmpty;
}

const std::vector<uint32_t>&
PortSet::GetAvailableSameLevel(uint32_t dimension, const std::string& addressBit) const
{
    // If no failed ports, return original list directly
    if (m_failedPorts.empty())
    {
        return GetSameLevel(dimension, addressBit);
    }

    if (dimension >= m_availableSameLevelBuckets.size())
    {
        return kEmpty;
    }

    auto it = m_availableSameLevelBuckets[dimension].find(addressBit);
    return it != m_availableSameLevelBuckets[dimension].end() ? it->second : kEmpty;
}

// ---------------- PortSelector ----------------

std::optional<uint32_t>
PortSelector::Pick(const std::vector<uint32_t>& ports, const RoutingContext& ctx) const
{
    if (ports.empty())
    {
        return std::nullopt;
    }

    switch (m_policy)
    {
    case PortSelectPolicy::kFirst:
        return ports[0];

    case PortSelectPolicy::kRandom: {
        // Use flowHash as the seed to maintain "pseudo-random" behavior consistent for the same
        // flow
        std::mt19937_64 rng(ctx.GetFlowHash() ^ 0x9e3779b97f4a7c15ULL);
        std::uniform_int_distribution<size_t> dist(0, ports.size() - 1);
        return ports[dist(rng)];
    }

    case PortSelectPolicy::kByHash: {
        size_t idx = static_cast<size_t>(ctx.GetFlowHash() % ports.size());
        return ports[idx];
    }
    }

    return ports[0];
}

} // namespace ns3
