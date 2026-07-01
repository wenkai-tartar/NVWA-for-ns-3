// SPDX-License-Identifier: GPL-2.0-only
#ifndef FATTREE_ROUTING_H
#define FATTREE_ROUTING_H

#include "ns3/ipv4-address.h"
#include "ns3/ipv4-route.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/ipv4.h"
#include "ns3/log.h"
#include "ns3/net-device.h"
#include "ns3/node.h"
#include "ns3/object.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/ptr.h"
#include "ns3/queue.h"
#include "ns3/random-variable-stream.h"
#include "ns3/simulator.h"

#include <functional>
#include <unordered_map>
#include <vector>

namespace ns3
{

class FattreeRoutingProtocol : public Ipv4RoutingProtocol
{
  public:
    static TypeId GetTypeId();

    enum SwitchType
    {
        NONE = 0,
        TOR = 1,
        AGG = 2,
        CORE = 3
    };

    enum RoutingStrategy
    {
        NIX = 0,
        ECMP = 1,
        ADAPTIVE_ROUTING = 2,
        ECMP_ADAPTIVE = 3,
        RR = 4,
        RR_ECMP = 5
    };

    enum StickyMode
    {
        PER_PACKET = 0,
        PER_FLOWLET = 1
    };

    FattreeRoutingProtocol();
    ~FattreeRoutingProtocol() override;

    // ----- Ipv4RoutingProtocol -----
    Ptr<Ipv4Route> RouteOutput(Ptr<Packet> p,
                               const Ipv4Header& header,
                               Ptr<NetDevice> oif,
                               Socket::SocketErrno& sockerr) override;

    bool RouteInput(Ptr<const Packet> p,
                    const Ipv4Header& header,
                    Ptr<const NetDevice> idev,
                    const UnicastForwardCallback& ucb,
                    const MulticastForwardCallback& mcb,
                    const LocalDeliverCallback& lcb,
                    const ErrorCallback ecb) override;

    void NotifyInterfaceUp(uint32_t interface) override
    {
    }

    void NotifyInterfaceDown(uint32_t interface) override
    {
    }

    void NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address) override
    {
    }

    void NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress address) override
    {
    }

    void SetIpv4(Ptr<Ipv4> ipv4) override
    {
        m_ipv4 = ipv4;
    }

    void PrintRoutingTable(Ptr<OutputStreamWrapper> stream,
                           Time::Unit unit = Time::S) const override;

    // ----- Fattree config / installation -----
    void SetSwitchType(SwitchType t)
    {
        m_type = t;
    }

    void SetNodeIndex(uint32_t idx)
    {
        m_nodeIndex = idx;
    }

    void SetParameters(uint32_t k, uint32_t tiers); // k-ary fat-tree 参数

    void SetStrategy(RoutingStrategy s)
    {
        m_strategy = s;
    }

    void SetSticky(StickyMode m)
    {
        m_sticky = m;
    }

    void SetStickyDelta(Time t)
    {
        m_stickyDelta = t;
    }

    void SetEcmpComparator(const std::string& name); // "queuesize"/"bandwidth"/"pq"/"qb"/"pb"

    void SetEcnThresholdFraction(double f)
    {
        m_ecnThresholdFraction = f;
    }

    void SetSpeculativeThresholdFraction(double f)
    {
        m_speculativeThresholdFraction = f;
    }

    // 由 helper 在拓扑就位后构建本节点 FIB（对应 htsim 的 addHostPort / 各层 up/down 路）
    void BuildLocalFib(const std::function<void(FattreeRoutingProtocol&)>& builder);

    // 添加一条目的地主机直连（ToR->host）
    void AddHostRoute(Ipv4Address dst, uint32_t outIf);

    // 添加 ECMP 多下一跳
    void AddEcmpRoute(Ipv4Address dst, uint32_t outIf);

    // 对外（测试）暴露：清空并重建
    void ClearFib();

  private:
    // ---- 内部结构 ----
    struct NextHop
    {
        uint32_t outIf = 0; // 输出接口索引
        Ptr<NetDevice> dev; // 设备指针（便于取队列）
    };

    struct EcmpSet
    {
        std::vector<NextHop> hops;
    };

    struct FiveTuple
    {
        Ipv4Address src, dst;
        uint8_t proto = 0;
        uint16_t sport = 0, dport = 0;

        bool operator==(const FiveTuple& o) const
        {
            return src == o.src && dst == o.dst && proto == o.proto && sport == o.sport &&
                   dport == o.dport;
        }
    };

    struct FiveTupleHash
    {
        size_t operator()(const FiveTuple& k) const
        {
            auto h = k.src.Get();
            h ^= (k.dst.Get() + 0x9e3779b9 + (h << 6) + (h >> 2));
            h ^= (k.proto + 0x9e3779b9 + (h << 6) + (h >> 2));
            h ^= (k.sport + 0x9e3779b9 + (h << 6) + (h >> 2));
            h ^= (k.dport + 0x9e3779b9 + (h << 6) + (h >> 2));
            return h;
        }
    };

    struct FlowletState
    {
        uint32_t egressIndex = 0;
        Time lastSeen;
    };

    // ---- FIB：目的主机 -> 多下一跳 ----
    struct Ipv4AddrHash
    {
        size_t operator()(const Ipv4Address& a) const
        {
            return std::hash<uint32_t>()(a.Get());
        }
    };

    std::unordered_map<Ipv4Address, EcmpSet, Ipv4AddrHash> m_fib;

    // flowlet 黏滞表
    std::unordered_map<FiveTuple, FlowletState, FiveTupleHash> m_flowlets;

    // ---- 选择逻辑 ----
    uint32_t ChooseEcmp(const EcmpSet& set, const Ipv4Header& ipHeader, Ptr<Packet> pkt);
    uint32_t EcmpHash(const Ipv4Header& ipHeader, Ptr<Packet> pkt) const;

    uint32_t AdaptiveRoute(const EcmpSet& set);
    uint32_t ReplaceWorstChoice(const EcmpSet& set, uint32_t myChoice);

    // 比较器族（返回：左更好 -> 1；右更好 -> -1；相等 -> 0）
    using CmpFn = std::function<int8_t(const NextHop&, const NextHop&)>;
    static int8_t CmpQueueSize(const NextHop& l, const NextHop& r);
    static int8_t CmpBandwidth(const NextHop& l, const NextHop& r);
    static int8_t CmpPQ(const NextHop& l, const NextHop& r);
    static int8_t CmpQB(const NextHop& l, const NextHop& r);
    static int8_t CmpPB(const NextHop& l, const NextHop& r);

    // 队列/带宽度量提取
    static uint32_t GetQueueBytes(const NextHop& nh);
    static double GetApproxUtil(const NextHop& nh); // 简易近似（可按需替换）

    // flow key
    FiveTuple MakeFiveTuple(const Ipv4Header& ipHeader, Ptr<Packet> pkt) const;

    // misc
    Ptr<Ipv4> m_ipv4;
    Ptr<UniformRandomVariable> m_rng;
    uint32_t m_hashSalt = 0;

    SwitchType m_type = NONE;
    RoutingStrategy m_strategy = ECMP;
    StickyMode m_sticky = PER_PACKET;
    Time m_stickyDelta = MicroSeconds(10);

    uint32_t m_k = 0, m_tiers = 3;
    uint32_t m_nodeIndex = 0;

    double m_ecnThresholdFraction = 1.0;
    double m_speculativeThresholdFraction = 0.2;
    CmpFn m_cmp = &FattreeRoutingProtocol::CmpQueueSize;

    // round-robin
    uint32_t m_rrCursor = 0;
};

} // namespace ns3

#endif
