/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef NON_MINIMAL_POLICY_H
#define NON_MINIMAL_POLICY_H

#include "congestion-signal-provider.h"
#include "ingress-classifier.h"
#include "non-minimal-routing-tag.h"
#include "routing-common.h"
#include "structured-address.h"

#include "ns3/object.h"
#include "ns3/ptr.h"

#include <functional>
#include <optional>
#include <vector>

namespace ns3
{

class NonMinimalPolicy : public Object
{
  public:
    static TypeId GetTypeId();

    struct ProbeResult
    {
        std::optional<uint32_t> outIf;
        uint64_t metric{0};
    };

    using ProbeCallback = std::function<ProbeResult(const StructuredAddress& dst,
                                                    PortSelectPolicy policyOverride)>;

    struct Decision
    {
        StructuredAddress effectiveDst;
        bool tagUpdated{false};
        NonMinimalRoutingTag tag;
    };

    void SetTransitFields(const std::vector<uint16_t>& fields);
    const std::vector<uint16_t>& GetTransitFields() const;
    void SetSeed(uint64_t seed);
    uint64_t GetSeed() const;

    virtual Decision Decide(const RoutingContext& ctx,
                            const StructuredAddress& finalDst,
                            const IngressInfo& ingress,
                            const NonMinimalRoutingTag& currentTag,
                            const ProbeCallback& probe) const = 0;

  protected:
    std::vector<NonMinimalRoutingTag::RewriteEntry> PickTransitRewrites(
        const RoutingContext& ctx,
        const StructuredAddress& finalDst,
        uint64_t seed) const;
    StructuredAddress ApplyRewrites(const StructuredAddress& base,
                                    const std::vector<NonMinimalRoutingTag::RewriteEntry>& rewrites)
        const;
    bool MatchesRewrites(const StructuredAddress& addr,
                         const std::vector<NonMinimalRoutingTag::RewriteEntry>& rewrites) const;

    void EnsureFieldValues(const StructuredTopology& topo) const;

    std::vector<uint16_t> m_transitFields;
    uint64_t m_seed{1};
    mutable uintptr_t m_cachedTopoId{0};
    mutable std::vector<std::vector<uint32_t>> m_fieldValues;
};

class ValiantPolicy : public NonMinimalPolicy
{
  public:
    static TypeId GetTypeId();

    Decision Decide(const RoutingContext& ctx,
                    const StructuredAddress& finalDst,
                    const IngressInfo& ingress,
                    const NonMinimalRoutingTag& currentTag,
                    const ProbeCallback& probe) const override;
};

class UgalPolicy : public NonMinimalPolicy
{
  public:
    static TypeId GetTypeId();

    void SetAlpha(double alpha);
    double GetAlpha() const;
    void SetDetourPenalty(double penalty);
    double GetDetourPenalty() const;

    Decision Decide(const RoutingContext& ctx,
                    const StructuredAddress& finalDst,
                    const IngressInfo& ingress,
                    const NonMinimalRoutingTag& currentTag,
                    const ProbeCallback& probe) const override;

  private:
    double m_alpha{1.0};
    double m_detourPenalty{1.0};
};

class DetourPolicy : public NonMinimalPolicy
{
  public:
    static TypeId GetTypeId();

    void SetStages(uint8_t stages);
    uint8_t GetStages() const;

    Decision Decide(const RoutingContext& ctx,
                    const StructuredAddress& finalDst,
                    const IngressInfo& ingress,
                    const NonMinimalRoutingTag& currentTag,
                    const ProbeCallback& probe) const override;

  private:
    uint8_t m_stages{1};
};

} // namespace ns3

#endif // NON_MINIMAL_POLICY_H
