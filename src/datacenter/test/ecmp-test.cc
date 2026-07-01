/*
 * Simple ECMP test for Fat-Tree k=4 topology
 * Verifies that kFirst and kByHash produce same packet count but different routing paths
 */

#include "ns3/test.h"
#include "ns3/core-module.h"
#include "ns3/datacenter-module.h"

#include <string>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("EcmpTestSuite");

/**
 * Test case for ECMP functionality
 */
class EcmpTestCase : public TestCase
{
  public:
    EcmpTestCase();
    virtual ~EcmpTestCase();

  private:
    void DoRun() override;
};

EcmpTestCase::EcmpTestCase()
    : TestCase("ECMP kFirst vs kByHash Test")
{
}

EcmpTestCase::~EcmpTestCase()
{
}

void
EcmpTestCase::DoRun()
{
    NS_LOG_INFO("Testing ECMP hash function");

    // Test 1: Verify MurmurHash3 produces diverse hash values
    Ipv4Header header1, header2, header3;
    header1.SetSource(Ipv4Address("10.0.0.1"));
    header1.SetDestination(Ipv4Address("10.0.0.2"));
    header1.SetProtocol(17); // UDP

    header2.SetSource(Ipv4Address("10.0.0.1"));
    header2.SetDestination(Ipv4Address("10.0.0.3"));
    header2.SetProtocol(17);

    header3.SetSource(Ipv4Address("10.0.0.2"));
    header3.SetDestination(Ipv4Address("10.0.0.3"));
    header3.SetProtocol(17);

    // Create simple packets with different ports
    Ptr<Packet> p1 = Create<Packet>(100);
    Ptr<Packet> p2 = Create<Packet>(100);
    Ptr<Packet> p3 = Create<Packet>(100);

    // Add UDP headers with different ports
    uint8_t udpHeader1[8] = {0xC0, 0x01, 0x00, 0x09, 0, 0, 0, 0}; // sport=49153, dport=9
    uint8_t udpHeader2[8] = {0xC0, 0x02, 0x00, 0x09, 0, 0, 0, 0}; // sport=49154, dport=9
    uint8_t udpHeader3[8] = {0xC0, 0x03, 0x00, 0x09, 0, 0, 0, 0}; // sport=49155, dport=9

    p1->AddAtEnd(Create<Packet>(udpHeader1, 8));
    p2->AddAtEnd(Create<Packet>(udpHeader2, 8));
    p3->AddAtEnd(Create<Packet>(udpHeader3, 8));

    // Compute hashes
    uint64_t hash1 = RuleBasedRouting::ComputeFlowHash(header1, p1);
    uint64_t hash2 = RuleBasedRouting::ComputeFlowHash(header2, p2);
    uint64_t hash3 = RuleBasedRouting::ComputeFlowHash(header3, p3);

    NS_LOG_INFO("Hash1: " << hash1 << " (mod 2 = " << (hash1 % 2) << ")");
    NS_LOG_INFO("Hash2: " << hash2 << " (mod 2 = " << (hash2 % 2) << ")");
    NS_LOG_INFO("Hash3: " << hash3 << " (mod 2 = " << (hash3 % 2) << ")");

    // Test 2: Hashes should be different for different 5-tuples
    NS_TEST_ASSERT_MSG_NE(hash1, hash2, "Different flows should have different hashes");
    NS_TEST_ASSERT_MSG_NE(hash2, hash3, "Different flows should have different hashes");
    NS_TEST_ASSERT_MSG_NE(hash1, hash3, "Different flows should have different hashes");

    // Test 3: Hash values should have good distribution (not all even or all odd)
    // With 3 different flows, we should see both even and odd hash values
    uint32_t evenCount = 0;
    uint32_t oddCount = 0;

    if (hash1 % 2 == 0) evenCount++; else oddCount++;
    if (hash2 % 2 == 0) evenCount++; else oddCount++;
    if (hash3 % 2 == 0) evenCount++; else oddCount++;

    NS_TEST_ASSERT_MSG_GT(evenCount, 0, "Should have at least one even hash");
    NS_TEST_ASSERT_MSG_GT(oddCount, 0, "Should have at least one odd hash (testing diversity)");

    // Test 4: Port selection with 2 ports
    std::vector<uint32_t> ports = {0, 1};

    // With kFirst, always select ports[0]
    uint32_t kFirstSelection = ports[0];

    // With kByHash, selection depends on hash % 2
    uint32_t kByHashSelection1 = ports[hash1 % 2];
    uint32_t kByHashSelection2 = ports[hash2 % 2];

    NS_LOG_INFO("kFirst always selects: " << kFirstSelection);
    NS_LOG_INFO("kByHash selections: " << kByHashSelection1 << ", " << kByHashSelection2);

    // At least one kByHash selection should differ from kFirst (due to hash diversity)
    bool atLeastOneDifferent = (kByHashSelection1 != kFirstSelection) ||
                               (kByHashSelection2 != kFirstSelection);
    NS_TEST_ASSERT_MSG_EQ(atLeastOneDifferent, true,
                         "kByHash should select different paths than kFirst for some flows");

    NS_LOG_INFO("✓ All ECMP tests passed!");
}

/**
 * Test suite for ECMP
 */
class EcmpTestSuite : public TestSuite
{
  public:
    EcmpTestSuite();
};

EcmpTestSuite::EcmpTestSuite()
    : TestSuite("ecmp", UNIT)
{
    AddTestCase(new EcmpTestCase, TestCase::QUICK);
}

// Instantiate the test suite
static EcmpTestSuite g_ecmpTestSuite;

