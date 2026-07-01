#include "fattree-routing.h"

#include "ns3/boolean.h"
#include "ns3/double.h"
#include "ns3/integer.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/tcp-header.h"
#include "ns3/udp-header.h"
#include "ns3/uinteger.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("FattreeRouting");

NS_OBJECT_ENSURE_REGISTERED(FattreeRoutingProtocol);

TypeId
FattreeRoutingProtocol::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::FattreeRoutingProtocol")
            .SetParent<Ipv4RoutingProtocol>()
            .SetGroupName("Internet")
            .AddConstructor<FattreeRoutingProtocol>()
            .AddAttribute("Strategy",
                          "Routing strategy",
                          EnumValue(ECMP),
                          MakeEnumAccessor(&FattreeRoutingProtocol::m_strategy),
                          MakeEnumChecker(ECMP,
                                          "ECMP",
                                          ADAPTIVE_ROUTING,
                                          "ADAPTIVE_ROUTING",
                                          ECMP_ADAPTIVE,
                                          "ECMP_ADAPTIVE",
                                          RR,
                                          "RR",
                                          RR_ECMP,
                                          "RR_ECMP"))
            .AddAttribute("StickyMode",
                          "PER_PACKET or PER_FLOWLET",
                          EnumValue(PER_PACKET),
                          MakeEnumAccessor(&FattreeRoutingProtocol::m_sticky),
                          MakeEnumChecker(PER_PACKET, "PER_PACKET", PER_FLOWLET, "PER_FLOWLET"))
            .AddAttribute("StickyDeltaUs",
                          "Flowlet gap in microseconds",
                          TimeValue(MicroSeconds(10)),
                          MakeTimeAccessor(&FattreeRoutingProtocol::m_stickyDelta),
                          MakeTimeChecker())
            .AddAttribute("EcnThresholdFraction",
                          "ECN threshold fraction (placeholder)",
                          DoubleValue(1.0),
                          MakeDoubleAccessor(&FattreeRoutingProtocol::m_ecnThresholdFraction),
                          MakeDoubleChecker<double>(0.0))
            .AddAttribute(
                "SpeculativeThresholdFraction",
                "Speculative threshold fraction (placeholder)",
                DoubleValue(0.2),
                MakeDoubleAccessor(&FattreeRoutingProtocol::m_speculativeThresholdFraction),
                MakeDoubleChecker<double>(0.0));
    return tid;
}

FattreeRoutingProtocol::FattreeRoutingProtocol()
{
    m_rng = CreateObject<UniformRandomVariable>();
    m_hashSalt = m_rng->GetInteger(0, 0x7fffffff);
}

FattreeRoutingProtocol::~FattreeRoutingProtocol() = default;

void
FattreeRoutingProtocol::SetParameters(uint32_t k, uint32_t tiers)
{
    m_k = k;
    m_tiers = tiers;
}

void
FattreeRoutingProtocol::SetEcmpComparator(const std::string& name)
{
    if (name == "queuesize")
    {
        m_cmp = &FattreeRoutingProtocol::CmpQueueSize;
    }
    else if (name == "bandwidth")
    {
        m_cmp = &FattreeRoutingProtocol::CmpBandwidth;
    }
    else if (name == "pq")
    {
        m_cmp = &FattreeRoutingProtocol::CmpPQ;
    }
    else if (name == "qb")
    {
        m_cmp = &FattreeRoutingProtocol::CmpQB;
    }
    else if (name == "pb")
    {
        m_cmp = &FattreeRoutingProtocol::CmpPB;
    }
    else
    {
        m_cmp = &FattreeRoutingProtocol::CmpQueueSize;
    }
}

void
FattreeRoutingProtocol::BuildLocalFib(const std::function<void(FattreeRoutingProtocol&)>& builder)
{
    ClearFib();
    builder(*this);
}

void
FattreeRoutingProtocol::ClearFib()
{
    m_fib.clear();
    m_flowlets.clear();
    m_rrCursor = 0;
}

void
FattreeRoutingProtocol::AddHostRoute(Ipv4Address dst, uint32_t outIf)
{
    auto& set = m_fib[dst];
    NextHop nh;
    nh.outIf = outIf;
    nh.dev = m_ipv4->GetNetDevice(outIf);
    set.hops.push_back(nh);
}

void
FattreeRoutingProtocol::AddEcmpRoute(Ipv4Address dst, uint32_t outIf)
{
    AddHostRoute(dst, outIf);
}

Ptr<Ipv4Route>
FattreeRoutingProtocol::RouteOutput(Ptr<Packet> p,
                                    const Ipv4Header& header,
                                    Ptr<NetDevice> oif,
                                    Socket::SocketErrno& sockerr)
{
    NS_LOG_FUNCTION(this << header.GetSource() << header.GetDestination());
    sockerr = Socket::ERROR_NOROUTETOHOST;

    auto it = m_fib.find(header.GetDestination());
    if (it == m_fib.end() || it->second.hops.empty())
    {
        return nullptr;
    }

    const EcmpSet& set = it->second;
    uint32_t choice = 0;

    if (set.hops.size() == 1)
    {
        choice = 0;
    }
    else
    {
        switch (m_strategy)
        {
        case NIX:
            NS_ASSERT(false);
            break;
        case ECMP:
            choice = EcmpHash(header, p) % set.hops.size();
            break;
        case ADAPTIVE_ROUTING:
            if (m_sticky == PER_PACKET)
            {
                choice = AdaptiveRoute(set);
            }
            else
            {
                // flowlet-sticky
                FiveTuple key = MakeFiveTuple(header, p);
                auto now = Simulator::Now();
                auto fi = m_flowlets.find(key);
                if (fi == m_flowlets.end())
                {
                    choice = AdaptiveRoute(set);
                    m_flowlets.emplace(key, FlowletState{choice, now});
                }
                else
                {
                    // 超过黏滞间隔才考虑换路，且 50% 概率
                    if ((now - fi->second.lastSeen) > m_stickyDelta && m_rng->GetInteger(0, 1) == 0)
                    {
                        uint32_t newChoice = AdaptiveRoute(set);
                        // 仅当新路“更好”时才切换
                        int8_t cmp = m_cmp(set.hops[fi->second.egressIndex], set.hops[newChoice]);
                        if (cmp < 0)
                        {
                            fi->second.egressIndex = newChoice;
                        }
                    }
                    fi->second.lastSeen = now;
                    choice = fi->second.egressIndex;
                }
            }
            break;
        case ECMP_ADAPTIVE:
            choice = EcmpHash(header, p) % set.hops.size();
            if (m_rng->GetInteger(0, 99) < 50)
            {
                choice = ReplaceWorstChoice(set, choice);
            }
            break;
        case RR:
            choice = (m_rrCursor++) % set.hops.size();
            break;
        case RR_ECMP:
            if (m_type == TOR)
            {
                choice = (m_rrCursor++) % set.hops.size();
            }
            else
            {
                choice = EcmpHash(header, p) % set.hops.size();
            }
            break;
        }
    }

    const NextHop& nh = set.hops[choice];

    Ptr<Ipv4Route> rt = Create<Ipv4Route>();
    rt->SetDestination(header.GetDestination());
    // 下一跳设为对端 IP（此处用点到点链路的 peer 地址获取）
    Ipv4InterfaceAddress ifAddr = m_ipv4->GetAddress(nh.outIf, 0);
    rt->SetSource(ifAddr.GetLocal());
    rt->SetOutputDevice(m_ipv4->GetNetDevice(nh.outIf));

    // 对于点到点，gateway = 对端地址（假设网段/30 或 /31）
    // 若你使用自定义编址，可在 Helper 里把 peer 地址记录进 NextHop 并在此设置。
    rt->SetGateway(ifAddr.GetBroadcast()); // 占位；更严谨：存储并设置 peer 的接口地址

    sockerr = Socket::ERROR_NOTERROR;
    return rt;
}

bool
FattreeRoutingProtocol::RouteInput(Ptr<const Packet> p,
                                   const Ipv4Header& header,
                                   Ptr<const NetDevice> idev,
                                   const UnicastForwardCallback& ucb,
                                   const MulticastForwardCallback& mcb,
                                   const LocalDeliverCallback& lcb,
                                   const ErrorCallback& ecb)
{
    NS_LOG_FUNCTION(this << header.GetSource() << header.GetDestination());
    int32_t iif = m_ipv4->GetInterfaceForDevice(idev);
    if (iif < 0)
    {
        ecb(p, header, Socket::ERROR_NOROUTETOHOST);
        return false;
    }

    // 本地投递？
    if (m_ipv4->IsDestinationAddress(header.GetDestination(), m_ipv4->GetInterfaceForDevice(idev)))
    {
        lcb(p, header, iif);
        return true;
    }

    // 查 fib 转发
    auto it = m_fib.find(header.GetDestination());
    if (it == m_fib.end() || it->second.hops.empty())
    {
        ecb(p, header, Socket::ERROR_NOROUTETOHOST);
        return false;
    }

    // 复用 RouteOutput 的选择逻辑（需复制包以满足签名）
    Ptr<Packet> copy = p->Copy();
    Socket::SocketErrno err;
    Ptr<Ipv4Route> rt = RouteOutput(copy, header, nullptr, err);
    if (!rt)
    {
        ecb(p, header, Socket::ERROR_NOROUTETOHOST);
        return false;
    }

    ucb(rt, p, header);
    return true;
}

void
FattreeRoutingProtocol::PrintRoutingTable(Ptr<OutputStreamWrapper> stream, Time::Unit unit) const
{
    std::ostream* os = stream->GetStream();
    *os << "Node " << m_ipv4->GetObject<Node>()->GetId() << " FattreeRouting FIB entries:\n";
    for (const auto& kv : m_fib)
    {
        *os << "  " << kv.first << " -> [";
        for (size_t i = 0; i < kv.second.hops.size(); ++i)
        {
            *os << kv.second.hops[i].outIf;
            if (i + 1 < kv.second.hops.size())
            {
                *os << ",";
            }
        }
        *os << "]\n";
    }
}

/*** --- 选择逻辑 --- ***/

static inline uint32_t
FreeBsdHash(uint32_t a, uint32_t b = 0, uint32_t c = 0)
{
    auto MIX = [&](uint32_t& x, uint32_t& y, uint32_t& z) {
        x -= y;
        x -= z;
        x ^= (z >> 13);
        y -= z;
        y -= x;
        y ^= (x << 8);
        z -= x;
        z -= y;
        z ^= (y >> 13);
        x -= y;
        x -= z;
        x ^= (z >> 12);
        y -= z;
        y -= x;
        y ^= (x << 16);
        z -= x;
        z -= y;
        z ^= (y >> 5);
        x -= y;
        x -= z;
        x ^= (z >> 3);
        y -= z;
        y -= x;
        y ^= (x << 10);
        z -= x;
        z -= y;
        z ^= (y >> 15);
    };
    uint32_t x = 0x9e3779b9, y = 0x9e3779b9, z = 0;
    y += c;
    z += b;
    x += a;
    MIX(x, y, z);
    return z;
}

uint32_t
FattreeRoutingProtocol::EcmpHash(const Ipv4Header& ip, Ptr<Packet> pkt) const
{
    // 尽量用 5-tuple；拿不到端口则退化
    uint32_t a = ip.GetSource().Get() ^ (ip.GetDestination().Get() << 1);
    uint16_t sp = 0, dp = 0;
    if (ip.GetProtocol() == 6)
    {
        TcpHeader th;
        Ptr<Packet> c = pkt->Copy();
        if (c->PeekHeader(th))
        {
            sp = th.GetSourcePort();
            dp = th.GetDestinationPort();
        }
    }
    else if (ip.GetProtocol() == 17)
    {
        UdpHeader uh;
        Ptr<Packet> c = pkt->Copy();
        if (c->PeekHeader(uh))
        {
            sp = uh.GetSourcePort();
            dp = uh.GetDestinationPort();
        }
    }
    return FreeBsdHash(a ^ (ip.GetProtocol() << 16), (sp << 16) | dp, m_hashSalt);
}

uint32_t
FattreeRoutingProtocol::AdaptiveRoute(const EcmpSet& set)
{
    // 单趟扫描，找“最优”，并在相等者中随机
    uint32_t bestIdx = 0;
    std::vector<uint32_t> ties{0};
    for (uint32_t i = 1; i < set.hops.size(); ++i)
    {
        int8_t c = m_cmp(set.hops[bestIdx], set.hops[i]);
        if (c < 0)
        { // 右更好
            bestIdx = i;
            ties.clear();
            ties.push_back(i);
        }
        else if (c == 0)
        {
            ties.push_back(i);
        }
    }
    if (ties.size() == 1)
    {
        return bestIdx;
    }
    return ties[m_rng->GetInteger(0, static_cast<int>(ties.size()) - 1)];
}

uint32_t
FattreeRoutingProtocol::ReplaceWorstChoice(const EcmpSet& set, uint32_t myChoice)
{
    uint32_t bestIdx = 0, worstIdx = 0;
    std::vector<uint32_t> ties{0};
    for (uint32_t i = 1; i < set.hops.size(); ++i)
    {
        int8_t cb = m_cmp(set.hops[bestIdx], set.hops[i]);
        if (cb < 0)
        {
            bestIdx = i;
            ties.clear();
            ties.push_back(i);
        }
        else if (cb == 0)
        {
            ties.push_back(i);
        }

        int8_t cw = m_cmp(set.hops[worstIdx], set.hops[i]);
        if (cw > 0)
        {
            worstIdx = i;
        }
    }
    int8_t r = m_cmp(set.hops[myChoice], set.hops[worstIdx]);
    if (r == 0)
    {
        return ties[m_rng->GetInteger(0, static_cast<int>(ties.size()) - 1)];
    }
    return myChoice;
}

/*** --- 比较器 & 度量 --- ***/

uint32_t
FattreeRoutingProtocol::GetQueueBytes(const NextHop& nh)
{
    if (!nh.dev)
    {
        return 0;
    }
    // PointToPointNetDevice 的队列
    Ptr<PointToPointNetDevice> p2p = DynamicCast<PointToPointNetDevice>(nh.dev);
    if (p2p && p2p->GetQueue())
    {
        return p2p->GetQueue()->GetNBytes();
    }
    // 若使用 TrafficControl 层，可在此接入 QueueDisc 统计
    return 0;
}

double
FattreeRoutingProtocol::GetApproxUtil(const NextHop& nh)
{
    // 简易占位：用队列字节做近似（0~1 归一化可结合链路速率）
    // 你也可以改为读取 NetDevice 统计窗口内的字节发送速率 / 链路速率
    const double denom = 1e6; // 经验尺度，按需修改
    return std::min(1.0, GetQueueBytes(nh) / denom);
}

int8_t
FattreeRoutingProtocol::CmpQueueSize(const NextHop& l, const NextHop& r)
{
    auto ql = GetQueueBytes(l), qr = GetQueueBytes(r);
    if (ql < qr)
    {
        return 1;
    }
    if (ql > qr)
    {
        return -1;
    }
    return 0;
}

int8_t
FattreeRoutingProtocol::CmpBandwidth(const NextHop& l, const NextHop& r)
{
    double ul = GetApproxUtil(l), ur = GetApproxUtil(r);
    if (ul < ur)
    {
        return 1;
    }
    if (ul > ur)
    {
        return -1;
    }
    return 0;
}

int8_t
FattreeRoutingProtocol::CmpPQ(const NextHop& l, const NextHop& r)
{
    int8_t p = CmpQueueSize(l, r);
    if (p != 0)
    {
        return p;
    }
    return 0; // 暂无 pause，等价于 PQ
}

int8_t
FattreeRoutingProtocol::CmpQB(const NextHop& l, const NextHop& r)
{
    int8_t p = CmpQueueSize(l, r);
    if (p != 0)
    {
        return p;
    }
    return CmpBandwidth(l, r);
}

int8_t
FattreeRoutingProtocol::CmpPB(const NextHop& l, const NextHop& r)
{
    // 暂无 pause，退化为带宽
    return CmpBandwidth(l, r);
}

FattreeRoutingProtocol::FiveTuple
FattreeRoutingProtocol::MakeFiveTuple(const Ipv4Header& ip, Ptr<Packet> pkt) const
{
    FiveTuple k;
    k.src = ip.GetSource();
    k.dst = ip.GetDestination();
    k.proto = ip.GetProtocol();
    if (k.proto == 6)
    {
        TcpHeader th;
        Ptr<Packet> c = pkt->Copy();
        if (c->PeekHeader(th))
        {
            k.sport = th.GetSourcePort();
            k.dport = th.GetDestinationPort();
        }
    }
    else if (k.proto == 17)
    {
        UdpHeader uh;
        Ptr<Packet> c = pkt->Copy();
        if (c->PeekHeader(uh))
        {
            k.sport = uh.GetSourcePort();
            k.dport = uh.GetDestinationPort();
        }
    }
    return k;
}

} // namespace ns3
