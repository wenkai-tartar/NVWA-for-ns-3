/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#ifndef STRUCTURED_ADDRESS_H
#define STRUCTURED_ADDRESS_H

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace ns3
{

enum class MatchCondition
{
    MatchSuccess,
    MatchFailure
};

/**
 * StructuredAddress
 * - A variable-length, field-based address.
 * - Each field is a 32-bit unsigned integer.
 * - Supports prefix/suffix matching, arbitrary-position matching,
 *   slice/concat operations, and field insertion/appending.
 */
class StructuredAddress
{
  public:
    using Field = uint32_t;

    StructuredAddress();
    explicit StructuredAddress(std::vector<Field> fields);
    StructuredAddress(std::initializer_list<Field> il);

    size_t Size() const;
    bool Empty() const;

    const Field& operator[](size_t i) const;
    Field& operator[](size_t i);

    const std::vector<Field>& Data() const;
    std::vector<Field>& Data();

    void Clear();
    void Reserve(size_t n);

    void Append(Field v);
    void Append(const std::vector<Field>& more);
    void Insert(size_t pos, Field v);
    void Prepend(Field v);

    StructuredAddress Slice(size_t start, size_t count) const;
    StructuredAddress Concat(const StructuredAddress& other) const;
    void ConcatInPlace(const StructuredAddress& other);

    // matching: prefix / suffix / arbitrary positions
    bool MatchesPrefix(const StructuredAddress& other, size_t k) const;
    bool MatchesSuffix(const StructuredAddress& other, size_t k) const;

    // Check equality on arbitrary positions: every (pos,val) in 'eq' must match.
    // Positions are 0-based. Returns false if any pos is out of range.
    bool MatchesAt(const std::vector<std::pair<size_t, Field>>& eq) const;

    std::string ToString(char sep = '.', bool withBrackets = true) const;
    std::string ToStringRange(size_t start,
                              size_t end,
                              char sep = '.',
                              bool withBrackets = true) const;

  private:
    std::vector<Field> m_fields;
};

std::ostream& operator<<(std::ostream& os, const StructuredAddress& addr);

} // namespace ns3

#endif // STRUCTURED_ADDRESS_H
