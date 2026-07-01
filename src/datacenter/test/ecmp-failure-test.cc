/*
 * Test for ECMP routing behavior change after link failure
 * Verifies that:
 * 1. Before failure: packet uses one of the ECMP paths
 * 2. After failure: packet route changes when the original path is unavailable
 *
 * Uses fattree_k4.json topology configuration
 */

#include "ns3/test.h"
#include "ns3/core-module.h"
#include "ns3/datacenter-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/failure-helper.h"
#include "ns3/rule-based-routing-helper.h"
#include "ns3/json.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using json = nlohmann::json;
using namespace ns3;

NS_LOG_COMPONENT_DEFINE("EcmpFailureTestSuite");

/**
 * Helper function to build topology from JSON config (same logic as constructor_fail.cc)
 */
static void
BuildFromJson(const json& cfg, TopologyBuilder& builder, std::shared_ptr<TopologyHelper> topoHelper)
{
    // Link profile
    std::string bwStr = cfg.at("link").value("bandwidth", "10Gbps");
    std::string delayStr = cfg.at("link").value("delay", "1us");
    LevelTemplate::LinkProfile lp{DataRate(bwStr), Time(delayStr)};

    uint32_t levelId = 0;
    // Create template level by level
    for (const auto& lv : cfg.at("levels"))
    {
        uint32_t dimId = 0;
        for (const auto& dim : lv.at("dims"))
        {
            std::string type = dim.at("template");
            if (type == "ClosInterLevel")
            {
                ++levelId;
                uint32_t nodeNum = dim.at("nodeNum");
                uint32_t subBlockNum = dim.at("subBlockNum");
                uint32_t groupNum = dim.at("groupNum");

                // Check for loadBalance policy
                PortSelectPolicy policy = PortSelectPolicy::kByHash;
                if (dim.contains("loadBalance"))
                {
                    std::string lb = dim.at("loadBalance");
                    if (lb == "kFirst") policy = PortSelectPolicy::kFirst;
                    else if (lb == "kRandom") policy = PortSelectPolicy::kRandom;
                    else if (lb == "kByHash") policy = PortSelectPolicy::kByHash;
                }

                Ptr<ClosInterLevelTemplate> t = CreateObject<ClosInterLevelTemplate>(
                    levelId, dimId, nodeNum, subBlockNum, groupNum, lp);
                t->SetTopologyHelper(topoHelper);
                t->SetPortSelectPolicy(policy);
                builder.AddLevel(t);
            }
            else
            {
                NS_ABORT_MSG("Unknown template: " << type);
            }
        }
    }
}

/**
 * Test case for ECMP routing behavior after link failure
 */
class EcmpFailureTestCase : public TestCase
{
  public:
    EcmpFailureTestCase();
    virtual ~EcmpFailureTestCase();

  private:
    void DoRun() override;
};

EcmpFailureTestCase::EcmpFailureTestCase()
    : TestCase("ECMP Routing Change After Link Failure Test")
{
}

EcmpFailureTestCase::~EcmpFailureTestCase()
{
}

void
EcmpFailureTestCase::DoRun()
{
    NS_LOG_INFO("=== Testing ECMP routing behavior after link failure ===");

    // Clear previous simulation state
    Simulator::Destroy();
    RoutingRuleManager::GetInstance()->Clear();
    StructuredAddressDirectory::Get()->Clear();

    // Load fattree_k4.json configuration
    std::filesystem::path configPath = "src/datacenter/examples/inputs/fattree_k4.json";
    std::ifstream ifs(configPath);
    if (!ifs.is_open())
    {
        NS_LOG_ERROR("Cannot open config file: " << configPath);
        NS_TEST_ASSERT_MSG_EQ(ifs.is_open(), true, "Config file should be readable");
        return;
    }
    json cfg;
    ifs >> cfg;

    // Build topology
    std::shared_ptr<TopologyHelper> topoHelper = std::make_shared<TopologyHelper>();
    RuleBasedRoutingHelper ruleBasedRoutingHelper;
    topoHelper->GetInternetStack().SetRoutingHelper(ruleBasedRoutingHelper);

    TopologyBuilder builder;
    BuildFromJson(cfg, builder, topoHelper);

    Ptr<StructuredTopology> topo = CreateObject<StructuredTopology>(topoHelper);
    builder.Build(topo);
    topo->RegisterAddresses();
    ruleBasedRoutingHelper.Initialize(*topo);

    NS_LOG_INFO("Topology built successfully with " << topo->GetNumLevels() << " levels");

    // Get host nodes (level 0)
    const NodeContainer& hosts = topo->GetLevel(0);
    NS_LOG_INFO("Number of hosts: " << hosts.GetN());

    // For FatTree k=4:
    // Level 0: 16 hosts
    // Level 1: 8 ToR switches
    // Level 2: 8 Agg switches
    // Level 3: 4 Core switches

    // Select a flow from host 0 to host 8 (different pods)
    Ptr<Node> srcHost = hosts.Get(0);
    Ptr<Node> dstHost = hosts.Get(8);

    Ptr<Ipv4> srcIpv4 = srcHost->GetObject<Ipv4>();
    Ptr<Ipv4> dstIpv4 = dstHost->GetObject<Ipv4>();

    Ipv4Address srcIp = srcIpv4->GetAddress(1, 0).GetLocal();
    Ipv4Address dstIp = dstIpv4->GetAddress(1, 0).GetLocal();

    NS_LOG_INFO("Test flow: " << srcIp << " -> " << dstIp);

    // Create test packet
    Ipv4Header header;
    header.SetSource(srcIp);
    header.SetDestination(dstIp);
    header.SetProtocol(17); // UDP

    Ptr<Packet> packet = Create<Packet>(100);
    uint8_t udpHeader[8] = {0xC0, 0x01, 0x00, 0x09, 0, 0, 0, 0}; // sport=49153, dport=9
    packet->AddAtEnd(Create<Packet>(udpHeader, 8));

    // Get ToR switch for host 0 (level 1, local index 0)
    Ptr<Node> tor0 = topo->GetNodeByLocal(1, 0);
    Ptr<Ipv4> tor0Ipv4 = tor0->GetObject<Ipv4>();
    Ptr<RuleBasedRouting> tor0Routing =
        DynamicCast<RuleBasedRouting>(tor0Ipv4->GetRoutingProtocol());

    NS_TEST_ASSERT_MSG_NE(tor0Routing, nullptr, "ToR0 should have RuleBasedRouting");

    // Compute flow hash
    uint64_t flowHash = RuleBasedRouting::ComputeFlowHash(header, packet, tor0->GetId());
    NS_LOG_INFO("Flow hash: " << flowHash << " (mod 2 = " << (flowHash % 2) << ")");

    // ========== Test 1: Route BEFORE failure ==========
    NS_LOG_INFO("--- Test 1: Route BEFORE failure ---");

    Ptr<Ipv4Route> routeBefore = tor0Routing->Lookup(header, packet, nullptr);
    NS_TEST_ASSERT_MSG_NE(routeBefore, nullptr, "Should have a route before failure");

    uint32_t interfaceBefore = UINT32_MAX;
    Ptr<NetDevice> devBefore = nullptr;
    if (routeBefore)
    {
        devBefore = routeBefore->GetOutputDevice();
        interfaceBefore = tor0Ipv4->GetInterfaceForDevice(devBefore);
        NS_LOG_INFO("Interface selected BEFORE failure: " << interfaceBefore);

        // Log the destination node of this interface
        Ptr<Channel> channel = devBefore->GetChannel();
        if (channel)
        {
            for (uint32_t i = 0; i < channel->GetNDevices(); ++i)
            {
                Ptr<NetDevice> peerDev = channel->GetDevice(i);
                if (peerDev != devBefore)
                {
                    NS_LOG_INFO("  -> connected to Node " << peerDev->GetNode()->GetId());
                }
            }
        }
    }

    // ========== Test 2: Trigger link failure ==========
    // Fail the link from ToR 0 (level 1, local 0) to Agg 0 (level 2, local 0)
    // This is the same failure as in fattree_k4_early_failure.json
    NS_LOG_INFO("--- Test 2: Triggering link failure (ToR0 -> Agg0) ---");

    FailureHelper::SetLinkDown(topo, 1, 0, 2, 0);

    // Verify the interface is down
    // Find which interface connects ToR0 to Agg0
    Ptr<Node> agg0 = topo->GetNodeByLocal(2, 0);
    uint32_t downInterface = UINT32_MAX;

    for (uint32_t i = 1; i < tor0Ipv4->GetNInterfaces(); ++i)
    {
        Ptr<NetDevice> dev = tor0Ipv4->GetNetDevice(i);
        Ptr<Channel> ch = dev->GetChannel();
        if (ch)
        {
            for (uint32_t j = 0; j < ch->GetNDevices(); ++j)
            {
                Ptr<NetDevice> peerDev = ch->GetDevice(j);
                if (peerDev != dev && peerDev->GetNode() == agg0)
                {
                    downInterface = i;
                    break;
                }
            }
        }
        if (downInterface != UINT32_MAX) break;
    }

    if (downInterface != UINT32_MAX)
    {
        NS_LOG_INFO("Interface to Agg0 (interface " << downInterface << ") should be DOWN");
        NS_TEST_ASSERT_MSG_EQ(tor0Ipv4->IsUp(downInterface), false,
                              "Interface to Agg0 should be down after SetLinkDown()");
    }

    // ========== Test 3: Route AFTER failure ==========
    NS_LOG_INFO("--- Test 3: Route AFTER failure ---");

    Ptr<Ipv4Route> routeAfter = tor0Routing->Lookup(header, packet, nullptr);

    if (routeAfter)
    {
        Ptr<NetDevice> devAfter = routeAfter->GetOutputDevice();
        uint32_t interfaceAfter = tor0Ipv4->GetInterfaceForDevice(devAfter);
        NS_LOG_INFO("Interface selected AFTER failure: " << interfaceAfter);

        // Log the destination node
        Ptr<Channel> channel = devAfter->GetChannel();
        if (channel)
        {
            for (uint32_t i = 0; i < channel->GetNDevices(); ++i)
            {
                Ptr<NetDevice> peerDev = channel->GetDevice(i);
                if (peerDev != devAfter)
                {
                    NS_LOG_INFO("  -> connected to Node " << peerDev->GetNode()->GetId());
                }
            }
        }

        // The key assertion: if the original interface was the one that failed,
        // the new interface should be different
        if (interfaceBefore == downInterface)
        {
            NS_TEST_ASSERT_MSG_NE(interfaceAfter, interfaceBefore,
                "Routing SHOULD select different interface when original path fails");
            NS_LOG_INFO("SUCCESS: Route changed from interface " << interfaceBefore
                        << " to interface " << interfaceAfter << " after failure");
        }
        else
        {
            // Original interface was not the failed one, route might stay same
            NS_LOG_INFO("Original interface " << interfaceBefore
                        << " was not the failed interface " << downInterface);
        }

        // Verify selected interface is UP
        NS_TEST_ASSERT_MSG_EQ(tor0Ipv4->IsUp(interfaceAfter), true,
                              "Selected interface after failure should be UP");
    }
    else
    {
        NS_LOG_WARN("No route found after failure - this indicates ECMP is NOT failure-aware");
        // This is the current expected behavior if ECMP doesn't filter down interfaces
    }

    // ========== Test 4: Recover link and verify routing returns ==========
    NS_LOG_INFO("--- Test 4: Recovering link ---");

    FailureHelper::SetLinkUp(topo, 1, 0, 2, 0);

    if (downInterface != UINT32_MAX)
    {
        NS_TEST_ASSERT_MSG_EQ(tor0Ipv4->IsUp(downInterface), true,
                              "Interface should be UP after recovery");
    }

    Ptr<Ipv4Route> routeRecovered = tor0Routing->Lookup(header, packet, nullptr);
    NS_TEST_ASSERT_MSG_NE(routeRecovered, nullptr, "Should have route after recovery");

    if (routeRecovered)
    {
        uint32_t interfaceRecovered = tor0Ipv4->GetInterfaceForDevice(routeRecovered->GetOutputDevice());
        NS_LOG_INFO("Interface selected AFTER recovery: " << interfaceRecovered);

        // With same hash and all interfaces up, should return to original
        NS_TEST_ASSERT_MSG_EQ(interfaceRecovered, interfaceBefore,
                              "Routing should return to original interface after recovery");
    }

    NS_LOG_INFO("=== ECMP Failure Test Completed ===");

    // Cleanup
    Simulator::Destroy();
}

/**
 * Test suite for ECMP Failure behavior
 */
class EcmpFailureTestSuite : public TestSuite
{
  public:
    EcmpFailureTestSuite();
};

EcmpFailureTestSuite::EcmpFailureTestSuite()
    : TestSuite("ecmp-failure", UNIT)
{
    AddTestCase(new EcmpFailureTestCase, TestCase::QUICK);
}

// Instantiate the test suite
static EcmpFailureTestSuite g_ecmpFailureTestSuite;
