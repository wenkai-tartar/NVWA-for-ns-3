/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#include "ns3/structured-address.h"
#include "ns3/structured-address-helper.h"
#include "ns3/test.h"

using namespace ns3;

namespace
{

// ---------- TestCase: construction & basic access ----------
class StructuredAddressBasicTest : public TestCase
{
  public:
    StructuredAddressBasicTest()
        : TestCase("StructuredAddress: basic construction and access")
    {
    }

    void DoRun() override
    {
        // default ctor
        StructuredAddress a;
        NS_TEST_ASSERT_MSG_EQ(a.Size(), 0u, "Default constructed address should be empty");
        NS_TEST_ASSERT_MSG_EQ(a.Empty(), true, "Default constructed address should be empty");

        // init-list ctor
        StructuredAddress b({10u, 3u, 7u});
        NS_TEST_ASSERT_MSG_EQ(b.Size(), 3u, "Init-list constructor size must be 3");
        NS_TEST_ASSERT_MSG_EQ(b[0], 10u, "Field 0 mismatch");
        NS_TEST_ASSERT_MSG_EQ(b[1], 3u, "Field 1 mismatch");
        NS_TEST_ASSERT_MSG_EQ(b[2], 7u, "Field 2 mismatch");

        // vector ctor
        std::vector<uint32_t> v{1u, 2u};
        StructuredAddress c(v);
        NS_TEST_ASSERT_MSG_EQ(c.Size(), 2u, "Vector constructor size must be 2");
        NS_TEST_ASSERT_MSG_EQ(c[0], 1u, "Field 0 mismatch");
        NS_TEST_ASSERT_MSG_EQ(c[1], 2u, "Field 1 mismatch");

        // mutation
        b.Append(5u);
        NS_TEST_ASSERT_MSG_EQ(b.Size(), 4u, "Append should increase size");
        NS_TEST_ASSERT_MSG_EQ(b[3], 5u, "Last field after append mismatch");

        b.Prepend(42u);
        NS_TEST_ASSERT_MSG_EQ(b.Size(), 5u, "Prepend should increase size");
        NS_TEST_ASSERT_MSG_EQ(b[0], 42u, "First field after prepend mismatch");

        b.Insert(1, 99u);
        NS_TEST_ASSERT_MSG_EQ(b.Size(), 6u, "Insert should increase size");
        NS_TEST_ASSERT_MSG_EQ(b[1], 99u, "Inserted value mismatch");

        // Append multiple
        b.Append(std::vector<uint32_t>{8u, 9u});
        NS_TEST_ASSERT_MSG_EQ(b.Size(), 8u, "Append multiple should increase size by 2");
        NS_TEST_ASSERT_MSG_EQ(b[6], 8u, "Appended[0] mismatch");
        NS_TEST_ASSERT_MSG_EQ(b[7], 9u, "Appended[1] mismatch");
    }
};

// ---------- TestCase: slice & concat ----------
class StructuredAddressSliceConcatTest : public TestCase
{
  public:
    StructuredAddressSliceConcatTest()
        : TestCase("StructuredAddress: slice and concat")
    {
    }

    void DoRun() override
    {
        StructuredAddress a({10u, 3u, 7u, 5u, 11u});

        // Slice head
        auto h2 = a.Slice(0, 2);
        NS_TEST_ASSERT_MSG_EQ(h2.Size(), 2u, "Head slice length");
        NS_TEST_ASSERT_MSG_EQ(h2[0], 10u, "Head[0]");
        NS_TEST_ASSERT_MSG_EQ(h2[1], 3u, "Head[1]");

        // Slice tail
        auto t2 = a.Slice(a.Size() - 2, 2);
        NS_TEST_ASSERT_MSG_EQ(t2.Size(), 2u, "Tail slice length");
        NS_TEST_ASSERT_MSG_EQ(t2[0], 5u, "Tail[0]");
        NS_TEST_ASSERT_MSG_EQ(t2[1], 11u, "Tail[1]");

        // Concat
        auto b = h2.Concat(t2); // [10,3,5,11]
        NS_TEST_ASSERT_MSG_EQ(b.Size(), 4u, "Concat size");
        NS_TEST_ASSERT_MSG_EQ(b[2], 5u, "Concat[2]");
        NS_TEST_ASSERT_MSG_EQ(b[3], 11u, "Concat[3]");

        // In-place concat
        h2.ConcatInPlace(t2);
        NS_TEST_ASSERT_MSG_EQ(h2.Size(), 4u, "In-place concat size");
        NS_TEST_ASSERT_MSG_EQ(h2[0], 10u, "In-place[0]");
        NS_TEST_ASSERT_MSG_EQ(h2[3], 11u, "In-place[3]");
    }
};

// ---------- TestCase: prefix/suffix/position matching ----------
class StructuredAddressMatchTest : public TestCase
{
  public:
    StructuredAddressMatchTest()
        : TestCase("StructuredAddress: prefix/suffix/pos matching")
    {
    }

    void DoRun() override
    {
        StructuredAddress a({10u, 3u, 7u, 5u});
        StructuredAddress p({10u, 3u});
        StructuredAddress s({7u, 5u});

        NS_TEST_ASSERT_MSG_EQ(a.MatchesPrefix(p, 2), true, "Prefix must match");
        NS_TEST_ASSERT_MSG_EQ(a.MatchesPrefix(s, 2), false, "Wrong prefix must not match");

        NS_TEST_ASSERT_MSG_EQ(a.MatchesSuffix(s, 2), true, "Suffix must match");
        NS_TEST_ASSERT_MSG_EQ(a.MatchesSuffix(p, 2), false, "Wrong suffix must not match");

        // arbitrary positions: pos0==10 and pos2==7
        NS_TEST_ASSERT_MSG_EQ(a.MatchesAt({{0, 10u}, {2, 7u}}),
                              true,
                              "Positional match should pass");
        // out-of-range should return false
        NS_TEST_ASSERT_MSG_EQ(a.MatchesAt({{10, 1u}}), false, "Out-of-range position should fail");
        // value mismatch
        NS_TEST_ASSERT_MSG_EQ(a.MatchesAt({{0, 11u}}), false, "Value mismatch should fail");

        NS_TEST_ASSERT_MSG_EQ(StructuredAddressHelper::CompareField(a, a, 10),
                              false,
                              "Out-of-range helper field compare should fail");
        StructuredAddress shortAddr({10u, 3u});
        NS_TEST_ASSERT_MSG_EQ(StructuredAddressHelper::ComparePrefix(a, shortAddr, 2),
                              false,
                              "Prefix compare should fail when dst lacks required fields");
        NS_TEST_ASSERT_MSG_EQ(StructuredAddressHelper::ComparePrefix(a, a, 2),
                              true,
                              "Prefix compare should pass for matching suffix fields");
        NS_TEST_ASSERT_MSG_EQ(a.ToStringRange(2, 1, '.', false),
                              std::string(""),
                              "Invalid reverse range should return an empty string");
    }
};

// ---------- TestCase: AddressPattern ----------
// class AddressPatternTest : public TestCase {
// public:
//   AddressPatternTest() : TestCase("AddressPattern: wildcard and equals") {}
//   void DoRun() override {
//     StructuredAddress a({10u, 3u, 7u, 5u});

//     AddressPattern pat;        // empty pattern
//     pat.Set(0, 10u);           // constrain pos0 == 10
//     pat.Set(3, 5u);            // constrain pos3 == 5
//     NS_TEST_ASSERT_MSG_EQ(pat.Size(), 4u, "Pattern auto-extends to pos 3");

//     NS_TEST_ASSERT_MSG_EQ(pat.Matches(a), true, "Pattern should match");

//     // Change constraint to mismatch
//     pat.Set(3, 6u);
//     NS_TEST_ASSERT_MSG_EQ(pat.Matches(a), false, "Pattern should fail after value change");

//     // Wildcard pos3 and constrain pos1
//     pat.SetAny(3);
//     pat.Set(1, 3u);
//     NS_TEST_ASSERT_MSG_EQ(pat.Matches(a), true, "Wildcard pos3 + constrain pos1 should match");

//     // Resize smaller and check minimum length requirement
//     pat.Resize(2); // pattern length 2
//     NS_TEST_ASSERT_MSG_EQ(pat.Matches(a), true, "Address length >= pattern length");
//     StructuredAddress shortAddr({10u}); // length 1
//     NS_TEST_ASSERT_MSG_EQ(pat.Matches(shortAddr), false, "Addr shorter than pattern should
//     fail");
//   }
// };

// ---------- TestCase: exceptions (out-of-range ops) ----------
class StructuredAddressExceptionTest : public TestCase
{
  public:
    StructuredAddressExceptionTest()
        : TestCase("StructuredAddress: exceptions on invalid ops")
    {
    }

    void DoRun() override
    {
        StructuredAddress a({1u, 2u, 3u});

        bool threw = false;
        try
        {
            (void)a.Slice(5, 1);
        } // start > size
        catch (const std::out_of_range&)
        {
            threw = true;
        }
        NS_TEST_ASSERT_MSG_EQ(threw, true, "Slice with invalid start should throw");

        threw = false;
        try
        {
            a.Insert(10, 99u);
        } // pos > size
        catch (const std::out_of_range&)
        {
            threw = true;
        }
        NS_TEST_ASSERT_MSG_EQ(threw, true, "Insert with invalid pos should throw");

        // operator[] at() throws in const/non-const
        const StructuredAddress& cref = a;
        threw = false;
        try
        {
            (void)cref[10];
        }
        catch (const std::out_of_range&)
        {
            threw = true;
        }
        NS_TEST_ASSERT_MSG_EQ(threw, true, "operator[] const-at out of range should throw");

        threw = false;
        try
        {
            (void)a[10];
        }
        catch (const std::out_of_range&)
        {
            threw = true;
        }
        NS_TEST_ASSERT_MSG_EQ(threw, true, "operator[] non-const-at out of range should throw");
    }
};

} // anonymous namespace

// ------------- Suite registration -------------

class StructuredAddressTestSuite : public TestSuite
{
  public:
    StructuredAddressTestSuite()
        : TestSuite("structured-address", UNIT)
    {
        AddTestCase(new StructuredAddressBasicTest, TestCase::QUICK);
        AddTestCase(new StructuredAddressSliceConcatTest, TestCase::QUICK);
        AddTestCase(new StructuredAddressMatchTest, TestCase::QUICK);
        // AddTestCase(new AddressPatternTest, TestCase::QUICK);
        AddTestCase(new StructuredAddressExceptionTest, TestCase::QUICK);
    }
};

// Static instantiation to register the suite
static StructuredAddressTestSuite g_structuredAddressTestSuite;
