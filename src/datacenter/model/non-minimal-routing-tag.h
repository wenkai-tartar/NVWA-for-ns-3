/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef NON_MINIMAL_ROUTING_TAG_H
#define NON_MINIMAL_ROUTING_TAG_H

#include "ns3/tag.h"

#include <cstdint>
#include <ostream>
#include <vector>

namespace ns3
{

enum class NonMinimalAlgorithm : uint8_t
{
    kNone = 0,
    kValiant = 1,
    kUgal = 2,
    kDetour = 3,
};

enum class NonMinimalPhase : uint8_t
{
    kMin = 0,
    kToTransit = 1,
    kToFinal = 2,
};

class NonMinimalRoutingTag : public Tag
{
  public:
    struct RewriteEntry
    {
        uint16_t index{0};
        uint32_t value{0};
    };

    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;

    uint32_t GetSerializedSize() const override;
    void Serialize(TagBuffer i) const override;
    void Deserialize(TagBuffer i) override;
    void Print(std::ostream& os) const override;

    void Reset();

    void SetAlgorithm(NonMinimalAlgorithm algo);
    NonMinimalAlgorithm GetAlgorithm() const;

    void SetPhase(NonMinimalPhase phase);
    NonMinimalPhase GetPhase() const;

    void SetFlags(uint8_t flags);
    uint8_t GetFlags() const;

    void ClearRewrite();
    void AddRewrite(uint16_t index, uint32_t value);
    void SetRewrite(const std::vector<RewriteEntry>& rewrites);
    const std::vector<RewriteEntry>& GetRewrites() const;
    bool HasRewrites() const;

  private:
    uint8_t m_algo{static_cast<uint8_t>(NonMinimalAlgorithm::kNone)};
    uint8_t m_phase{static_cast<uint8_t>(NonMinimalPhase::kMin)};
    uint8_t m_flags{0};
    uint8_t m_count{0};
    std::vector<RewriteEntry> m_rewrites;
};

} // namespace ns3

#endif // NON_MINIMAL_ROUTING_TAG_H
