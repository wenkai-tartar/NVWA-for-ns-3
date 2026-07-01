/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "non-minimal-policy.h"

#include "ns3/log.h"

#include <algorithm>
#include <unordered_set>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("NonMinimalPolicy");

NS_OBJECT_ENSURE_REGISTERED(NonMinimalPolicy);
NS_OBJECT_ENSURE_REGISTERED(ValiantPolicy);
NS_OBJECT_ENSURE_REGISTERED(UgalPolicy);
NS_OBJECT_ENSURE_REGISTERED(DetourPolicy);

namespace
{
constexpr uint8_t kUgalPending = 1;

bool
HasMetaFlag(const IngressInfo& ingress, const std::string& key)
{
    auto it = ingress.meta.find(key);
    return it != ingress.meta.end() && it->second != 0;
}

uint64_t
GetMetaOrDefault(const IngressInfo& ingress, const std::string& key, uint64_t fallback)
{
    auto it = ingress.meta.find(key);
    return it == ingress.meta.end() ? fallback : it->second;
}
} // namespace

TypeId
NonMinimalPolicy::GetTypeId()
{
    static TypeId tid = TypeId("ns3::NonMinimalPolicy")
                            .SetParent<Object>()
                            .SetGroupName("Datacenter");
    return tid;
}

void
NonMinimalPolicy::SetTransitFields(const std::vector<uint16_t>& fields)
{
    m_transitFields = fields;
    m_fieldValues.clear();
    m_cachedTopoId = 0;
}

const std::vector<uint16_t>&
NonMinimalPolicy::GetTransitFields() const
{
    return m_transitFields;
}

void
NonMinimalPolicy::SetSeed(uint64_t seed)
{
    m_seed = seed;
}

uint64_t
NonMinimalPolicy::GetSeed() const
{
    return m_seed;
}

void
NonMinimalPolicy::EnsureFieldValues(const StructuredTopology& topo) const
{
    uintptr_t topoId = reinterpret_cast<uintptr_t>(&topo);
    if (m_cachedTopoId == topoId && !m_fieldValues.empty())
    {
        return;
    }

    size_t maxIndex = 0;
    for (uint16_t idx : m_transitFields)
    {
        if (idx > maxIndex)
        {
            maxIndex = idx;
        }
    }
    m_fieldValues.clear();
    if (m_transitFields.empty())
    {
        m_cachedTopoId = topoId;
        return;
    }
    m_fieldValues.resize(maxIndex + 1);
    std::vector<std::unordered_set<uint32_t>> sets(maxIndex + 1);

    const auto& addrsByLevel = topo.GetStructuredAddrs();
    for (const auto& levelAddrs : addrsByLevel)
    {
        for (const auto& addr : levelAddrs)
        {
            const size_t size = addr.Size();
            for (uint16_t idx : m_transitFields)
            {
                if (idx < size)
                {
                    sets[idx].insert(addr[idx]);
                }
            }
        }
    }

    for (uint16_t idx : m_transitFields)
    {
        auto& vec = m_fieldValues[idx];
        vec.assign(sets[idx].begin(), sets[idx].end());
        std::sort(vec.begin(), vec.end());
    }

    m_cachedTopoId = topoId;
}

std::vector<NonMinimalRoutingTag::RewriteEntry>
NonMinimalPolicy::PickTransitRewrites(const RoutingContext& ctx,
                                      const StructuredAddress& finalDst,
                                      uint64_t seed) const
{
    std::vector<NonMinimalRoutingTag::RewriteEntry> rewrites;
    if (!ctx.topo || m_transitFields.empty())
    {
        return rewrites;
    }

    EnsureFieldValues(*ctx.topo);
    constexpr uint64_t kSeedMix = 0x9e3779b97f4a7c15ULL;
    uint64_t h = (seed ^ (m_seed * kSeedMix)) ^ kSeedMix;
    for (uint16_t idx : m_transitFields)
    {
        if (idx >= m_fieldValues.size())
        {
            continue;
        }
        const auto& values = m_fieldValues[idx];
        if (values.empty())
        {
            continue;
        }
        const size_t n = values.size();
        const size_t choice = static_cast<size_t>(h % n);
        uint32_t value = values[choice];
        const bool hasDst = (idx < finalDst.Size());
        const bool hasSrc = (idx < ctx.GetSrc().Size());
        const uint32_t dstValue = hasDst ? finalDst[idx] : 0;
        const uint32_t srcValue = hasSrc ? ctx.GetSrc()[idx] : 0;
        if (n > 1)
        {
            for (size_t offset = 0; offset < n; ++offset)
            {
                const size_t candIdx = (choice + offset) % n;
                const uint32_t cand = values[candIdx];
                const bool forbidden =
                    (hasDst && cand == dstValue) || (hasSrc && cand == srcValue);
                if (!forbidden)
                {
                    value = cand;
                    break;
                }
                value = cand; // fallback if all candidates are forbidden
            }
        }
        rewrites.push_back({idx, value});
        h ^= (h << 7) + static_cast<uint64_t>(idx) * 0x85ebca6b;
    }
    return rewrites;
}

StructuredAddress
NonMinimalPolicy::ApplyRewrites(const StructuredAddress& base,
                                const std::vector<NonMinimalRoutingTag::RewriteEntry>& rewrites) const
{
    StructuredAddress out = base;
    for (const auto& entry : rewrites)
    {
        if (entry.index < out.Size())
        {
            out[entry.index] = entry.value;
        }
    }
    return out;
}

bool
NonMinimalPolicy::MatchesRewrites(
    const StructuredAddress& addr,
    const std::vector<NonMinimalRoutingTag::RewriteEntry>& rewrites) const
{
    for (const auto& entry : rewrites)
    {
        if (entry.index >= addr.Size())
        {
            return false;
        }
        if (addr[entry.index] != entry.value)
        {
            return false;
        }
    }
    return true;
}

TypeId
ValiantPolicy::GetTypeId()
{
    static TypeId tid = TypeId("ns3::ValiantPolicy")
                            .SetParent<NonMinimalPolicy>()
                            .SetGroupName("Datacenter")
                            .AddConstructor<ValiantPolicy>();
    return tid;
}

TypeId
UgalPolicy::GetTypeId()
{
    static TypeId tid = TypeId("ns3::UgalPolicy")
                            .SetParent<NonMinimalPolicy>()
                            .SetGroupName("Datacenter")
                            .AddConstructor<UgalPolicy>();
    return tid;
}

NonMinimalPolicy::Decision
ValiantPolicy::Decide(const RoutingContext& ctx,
                      const StructuredAddress& finalDst,
                      const IngressInfo& ingress,
                      const NonMinimalRoutingTag& currentTag,
                      const ProbeCallback& probe) const
{
    (void)probe;
    Decision d;
    d.effectiveDst = finalDst;
    d.tag = currentTag;
    d.tagUpdated = false;

    if (currentTag.GetAlgorithm() == NonMinimalAlgorithm::kValiant &&
        currentTag.GetPhase() == NonMinimalPhase::kToTransit &&
        currentTag.HasRewrites())
    {
        if (MatchesRewrites(ctx.GetSrc(), currentTag.GetRewrites()))
        {
            NonMinimalRoutingTag nextTag = currentTag;
            nextTag.SetPhase(NonMinimalPhase::kToFinal);
            nextTag.SetFlags(0);
            nextTag.ClearRewrite();
            d.tag = nextTag;
            d.tagUpdated = true;
            return d;
        }
        d.effectiveDst = ApplyRewrites(finalDst, currentTag.GetRewrites());
        return d;
    }

    if (ingress.direction != IngressDirection::kInject)
    {
        return d;
    }

    auto rewrites = PickTransitRewrites(ctx, finalDst, ctx.GetFlowHash());
    if (rewrites.empty())
    {
        return d;
    }

    NonMinimalRoutingTag nextTag = currentTag;
    nextTag.SetAlgorithm(NonMinimalAlgorithm::kValiant);
    nextTag.SetPhase(NonMinimalPhase::kToTransit);
    nextTag.SetRewrite(rewrites);
    d.tag = nextTag;
    d.tagUpdated = true;
    d.effectiveDst = ApplyRewrites(finalDst, rewrites);
    return d;
}

void
UgalPolicy::SetAlpha(double alpha)
{
    m_alpha = alpha;
}

double
UgalPolicy::GetAlpha() const
{
    return m_alpha;
}

void
UgalPolicy::SetDetourPenalty(double penalty)
{
    m_detourPenalty = penalty;
}

double
UgalPolicy::GetDetourPenalty() const
{
    return m_detourPenalty;
}

NonMinimalPolicy::Decision
UgalPolicy::Decide(const RoutingContext& ctx,
                   const StructuredAddress& finalDst,
                   const IngressInfo& ingress,
                   const NonMinimalRoutingTag& currentTag,
                   const ProbeCallback& probe) const
{
    Decision d;
    d.effectiveDst = finalDst;
    d.tag = currentTag;
    d.tagUpdated = false;

    if (currentTag.GetAlgorithm() == NonMinimalAlgorithm::kUgal &&
        currentTag.GetPhase() == NonMinimalPhase::kToTransit &&
        currentTag.HasRewrites())
    {
        if (MatchesRewrites(ctx.GetSrc(), currentTag.GetRewrites()))
        {
            NonMinimalRoutingTag nextTag = currentTag;
            nextTag.SetPhase(NonMinimalPhase::kToFinal);
            nextTag.SetFlags(0);
            nextTag.ClearRewrite();
            d.tag = nextTag;
            d.tagUpdated = true;
            return d;
        }
        d.effectiveDst = ApplyRewrites(finalDst, currentTag.GetRewrites());
        return d;
    }

    const bool isSourceRouter = HasMetaFlag(ingress, "sourceRouter");
    if (!isSourceRouter)
    {
        if (ingress.direction == IngressDirection::kInject)
        {
            auto rewrites = PickTransitRewrites(ctx, finalDst, ctx.GetFlowHash());
            if (!rewrites.empty())
            {
                NonMinimalRoutingTag nextTag = currentTag;
                nextTag.SetAlgorithm(NonMinimalAlgorithm::kUgal);
                nextTag.SetPhase(NonMinimalPhase::kMin);
                nextTag.SetFlags(kUgalPending);
                nextTag.SetRewrite(rewrites);
                d.tag = nextTag;
                d.tagUpdated = true;
            }
            else if (currentTag.GetAlgorithm() != NonMinimalAlgorithm::kUgal ||
                     currentTag.GetPhase() != NonMinimalPhase::kMin ||
                     currentTag.GetFlags() != 0 ||
                     currentTag.HasRewrites())
            {
                NonMinimalRoutingTag nextTag = currentTag;
                nextTag.SetAlgorithm(NonMinimalAlgorithm::kUgal);
                nextTag.SetPhase(NonMinimalPhase::kMin);
                nextTag.SetFlags(0);
                nextTag.ClearRewrite();
                d.tag = nextTag;
                d.tagUpdated = true;
            }
        }
        return d;
    }

    std::vector<NonMinimalRoutingTag::RewriteEntry> rewrites;
    const bool hasPendingTransit =
        currentTag.GetAlgorithm() == NonMinimalAlgorithm::kUgal &&
        currentTag.GetPhase() == NonMinimalPhase::kMin &&
        currentTag.GetFlags() == kUgalPending &&
        currentTag.HasRewrites();
    if (hasPendingTransit)
    {
        rewrites = currentTag.GetRewrites();
    }
    else
    {
        uint64_t transitSeed = GetMetaOrDefault(ingress, "packetSourceFlowHash", ctx.GetFlowHash());
        rewrites = PickTransitRewrites(ctx, finalDst, transitSeed);
    }
    if (rewrites.empty())
    {
        if (currentTag.GetAlgorithm() != NonMinimalAlgorithm::kUgal ||
            currentTag.GetPhase() != NonMinimalPhase::kMin ||
            currentTag.GetFlags() != 0 ||
            currentTag.HasRewrites())
        {
            NonMinimalRoutingTag nextTag = currentTag;
            nextTag.SetAlgorithm(NonMinimalAlgorithm::kUgal);
            nextTag.SetPhase(NonMinimalPhase::kMin);
            nextTag.SetFlags(0);
            nextTag.ClearRewrite();
            d.tag = nextTag;
            d.tagUpdated = true;
        }
        return d;
    }

    StructuredAddress valDst = ApplyRewrites(finalDst, rewrites);
    auto minProbe = probe(finalDst, PortSelectPolicy::kByHash);
    auto valProbe = probe(valDst, PortSelectPolicy::kByHash);

    double minCost = static_cast<double>(minProbe.metric);
    double valCost = static_cast<double>(valProbe.metric) + m_alpha * m_detourPenalty;

    bool pickVal = false;
    if (valProbe.outIf.has_value() && !minProbe.outIf.has_value())
    {
        pickVal = true;
    }
    else if (!valProbe.outIf.has_value())
    {
        pickVal = false;
    }
    else
    {
        pickVal = (valCost < minCost);
    }

    if (pickVal)
    {
        NonMinimalRoutingTag nextTag = currentTag;
        nextTag.SetAlgorithm(NonMinimalAlgorithm::kUgal);
        nextTag.SetPhase(NonMinimalPhase::kToTransit);
        nextTag.SetFlags(0);
        nextTag.SetRewrite(rewrites);
        d.tag = nextTag;
        d.tagUpdated = true;
        d.effectiveDst = valDst;
        return d;
    }

    if (currentTag.GetAlgorithm() != NonMinimalAlgorithm::kUgal ||
        currentTag.GetPhase() != NonMinimalPhase::kMin ||
        currentTag.GetFlags() != 0 ||
        currentTag.HasRewrites())
    {
        NonMinimalRoutingTag nextTag = currentTag;
        nextTag.SetAlgorithm(NonMinimalAlgorithm::kUgal);
        nextTag.SetPhase(NonMinimalPhase::kMin);
        nextTag.SetFlags(0);
        nextTag.ClearRewrite();
        d.tag = nextTag;
        d.tagUpdated = true;
    }
    return d;
}

TypeId
DetourPolicy::GetTypeId()
{
    static TypeId tid = TypeId("ns3::DetourPolicy")
                            .SetParent<NonMinimalPolicy>()
                            .SetGroupName("Datacenter")
                            .AddConstructor<DetourPolicy>();
    return tid;
}

void
DetourPolicy::SetStages(uint8_t stages)
{
    m_stages = stages == 0 ? 1 : stages;
}

uint8_t
DetourPolicy::GetStages() const
{
    return m_stages;
}

namespace
{
bool
AddressEquals(const StructuredAddress& a, const StructuredAddress& b)
{
    if (a.Size() != b.Size())
    {
        return false;
    }
    for (size_t i = 0; i < a.Size(); ++i)
    {
        if (a[i] != b[i])
        {
            return false;
        }
    }
    return true;
}

uint64_t
MixDetourHash(uint64_t h, uint8_t stage, uint32_t attempt)
{
    uint64_t x = h ^ (0x9e3779b97f4a7c15ULL + static_cast<uint64_t>(stage) * 0xbf58476d1ce4e5b9ULL);
    x ^= static_cast<uint64_t>(attempt) * 0x94d049bb133111ebULL;
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}
} // namespace

NonMinimalPolicy::Decision
DetourPolicy::Decide(const RoutingContext& ctx,
                     const StructuredAddress& finalDst,
                     const IngressInfo& ingress,
                     const NonMinimalRoutingTag& currentTag,
                     const ProbeCallback& probe) const
{
    (void)probe;
    Decision d;
    d.effectiveDst = finalDst;
    d.tag = currentTag;
    d.tagUpdated = false;

    auto applySingleRewrite = [](const StructuredAddress& base,
                                     const NonMinimalRoutingTag::RewriteEntry& entry) {
        StructuredAddress out = base;
        if (entry.index < out.Size())
        {
            out[entry.index] = entry.value;
        }
        return out;
    };

    auto pickStageRewrite =
        [this, &ctx, &finalDst, &applySingleRewrite](uint64_t flowHash,
                                uint8_t stage) -> std::optional<NonMinimalRoutingTag::RewriteEntry> {
        if (!ctx.topo || m_transitFields.empty())
        {
            return std::nullopt;
        }
        EnsureFieldValues(*ctx.topo);
        const size_t dimCount = m_transitFields.size();
        const size_t maxAttempts = std::max<size_t>(dimCount * 4, 1);
        for (size_t attempt = 0; attempt < maxAttempts; ++attempt)
        {
            uint64_t h = MixDetourHash(flowHash, stage, static_cast<uint32_t>(attempt));
            size_t dimPos = static_cast<size_t>(h % dimCount);
            uint16_t field = m_transitFields[dimPos];
            if (field >= m_fieldValues.size())
            {
                continue;
            }
            const auto& values = m_fieldValues[field];
            if (values.size() <= 1)
            {
                continue;
            }
            if (field >= finalDst.Size())
            {
                continue;
            }
            uint32_t dstVal = finalDst[field];
            auto it = std::find(values.begin(), values.end(), dstVal);
            if (it == values.end())
            {
                continue;
            }
            size_t dstIdx = static_cast<size_t>(std::distance(values.begin(), it));
            size_t delta = (static_cast<size_t>(h / dimCount) % (values.size() - 1)) + 1;
            size_t newIdx = (dstIdx + delta) % values.size();
            uint32_t newVal = values[newIdx];
            NonMinimalRoutingTag::RewriteEntry entry{field, newVal};

            StructuredAddress effective = applySingleRewrite(finalDst, entry);
            if (AddressEquals(effective, finalDst))
            {
                continue;
            }
            if (!ctx.GetSrc().Empty() && AddressEquals(effective, ctx.GetSrc()))
            {
                continue;
            }
            return entry;
        }
        return std::nullopt;
    };

    auto getStage = [](const NonMinimalRoutingTag& tag) -> uint8_t { return tag.GetFlags(); };
    auto setStage = [](NonMinimalRoutingTag& tag, uint8_t stage) { tag.SetFlags(stage); };

    if (currentTag.GetAlgorithm() == NonMinimalAlgorithm::kDetour &&
        currentTag.GetPhase() == NonMinimalPhase::kToTransit && currentTag.HasRewrites())
    {
        NonMinimalRoutingTag tag = currentTag;
        uint8_t stage = getStage(tag);
        const auto& rewrites = tag.GetRewrites();
        if (stage >= rewrites.size())
        {
            tag.SetPhase(NonMinimalPhase::kToFinal);
            tag.ClearRewrite();
            setStage(tag, 0);
            d.tag = tag;
            d.tagUpdated = true;
            d.effectiveDst = finalDst;
            return d;
        }

        bool updated = false;
        while (stage < rewrites.size())
        {
            std::vector<NonMinimalRoutingTag::RewriteEntry> stageVec{rewrites[stage]};
            if (!MatchesRewrites(ctx.GetSrc(), stageVec))
            {
                break;
            }
            stage++;
            updated = true;
            if (stage >= rewrites.size())
            {
                tag.SetPhase(NonMinimalPhase::kToFinal);
                tag.ClearRewrite();
                setStage(tag, 0);
                d.effectiveDst = finalDst;
                d.tag = tag;
                d.tagUpdated = true;
                return d;
            }
            setStage(tag, stage);
        }

        if (updated)
        {
            d.tag = tag;
            d.tagUpdated = true;
        }
        if (tag.GetPhase() == NonMinimalPhase::kToTransit && tag.HasRewrites())
        {
            d.effectiveDst = applySingleRewrite(finalDst, tag.GetRewrites()[stage]);
        }
        return d;
    }

    if (ingress.direction != IngressDirection::kInject)
    {
        return d;
    }

    std::vector<NonMinimalRoutingTag::RewriteEntry> stageRewrites;
    const uint8_t stages = m_stages == 0 ? 1 : m_stages;
    stageRewrites.reserve(stages);
    for (uint8_t stage = 0; stage < stages; ++stage)
    {
        auto entry = pickStageRewrite(ctx.GetFlowHash(), stage);
        if (!entry.has_value())
        {
            break;
        }
        stageRewrites.push_back(*entry);
    }

    if (stageRewrites.empty())
    {
        return d;
    }

    NonMinimalRoutingTag nextTag = currentTag;
    nextTag.SetAlgorithm(NonMinimalAlgorithm::kDetour);
    nextTag.SetPhase(NonMinimalPhase::kToTransit);
    nextTag.SetRewrite(stageRewrites);
    setStage(nextTag, 0);
    d.tag = nextTag;
    d.tagUpdated = true;
    d.effectiveDst = applySingleRewrite(finalDst, stageRewrites[0]);
    return d;
}

} // namespace ns3
