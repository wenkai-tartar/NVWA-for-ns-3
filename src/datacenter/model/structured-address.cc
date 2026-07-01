/*
 * Copyright (c) 2025 Microsoft Research Asia
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: Wenkai Li <wkli24@stu.xjtu.edu.cn>
 */

#include "structured-address.h"

#include <stdexcept>

namespace ns3
{

StructuredAddress::StructuredAddress() = default;

StructuredAddress::StructuredAddress(std::vector<Field> fields)
    : m_fields(std::move(fields))
{
}

StructuredAddress::StructuredAddress(std::initializer_list<Field> il)
    : m_fields(il)
{
}

size_t
StructuredAddress::Size() const
{
    return m_fields.size();
}

bool
StructuredAddress::Empty() const
{
    return m_fields.empty();
}

const StructuredAddress::Field&
StructuredAddress::operator[](size_t i) const
{
    return m_fields.at(i);
}

StructuredAddress::Field&
StructuredAddress::operator[](size_t i)
{
    return m_fields.at(i);
}

const std::vector<StructuredAddress::Field>&
StructuredAddress::Data() const
{
    return m_fields;
}

std::vector<StructuredAddress::Field>&
StructuredAddress::Data()
{
    return m_fields;
}

void
StructuredAddress::Clear()
{
    m_fields.clear();
}

void
StructuredAddress::Reserve(size_t n)
{
    m_fields.reserve(n);
}

void
StructuredAddress::Append(Field v)
{
    m_fields.push_back(v);
}

void
StructuredAddress::Append(const std::vector<Field>& more)
{
    m_fields.insert(m_fields.end(), more.begin(), more.end());
}

void
StructuredAddress::Insert(size_t pos, Field v)
{
    if (pos > m_fields.size())
    {
        throw std::out_of_range("Insert pos out of range");
    }
    m_fields.insert(m_fields.begin() + static_cast<std::ptrdiff_t>(pos), v);
}

void
StructuredAddress::Prepend(Field v)
{
    m_fields.insert(m_fields.begin(), v);
}

StructuredAddress
StructuredAddress::Slice(size_t start, size_t count) const
{
    if (start > m_fields.size())
    {
        throw std::out_of_range("Slice start out of range");
    }
    const size_t end = std::min(start + count, m_fields.size());
    return StructuredAddress(
        std::vector<Field>(m_fields.begin() + static_cast<std::ptrdiff_t>(start),
                           m_fields.begin() + static_cast<std::ptrdiff_t>(end)));
}

StructuredAddress
StructuredAddress::Concat(const StructuredAddress& other) const
{
    StructuredAddress out;
    out.m_fields.reserve(this->Size() + other.Size());
    out.m_fields.insert(out.m_fields.end(), m_fields.begin(), m_fields.end());
    out.m_fields.insert(out.m_fields.end(), other.m_fields.begin(), other.m_fields.end());
    return out;
}

void
StructuredAddress::ConcatInPlace(const StructuredAddress& other)
{
    m_fields.insert(m_fields.end(), other.m_fields.begin(), other.m_fields.end());
}

bool
StructuredAddress::MatchesPrefix(const StructuredAddress& other, size_t k) const
{
    if (k > this->Size() || k > other.Size())
    {
        return false;
    }
    for (size_t i = 0; i < k; ++i)
    {
        if (m_fields[i] != other.m_fields[i])
        {
            return false;
        }
    }
    return true;
}

bool
StructuredAddress::MatchesSuffix(const StructuredAddress& other, size_t k) const
{
    if (k > this->Size() || k > other.Size())
    {
        return false;
    }
    const size_t a = this->Size() - k;
    const size_t b = other.Size() - k;
    for (size_t i = 0; i < k; ++i)
    {
        if (m_fields[a + i] != other.m_fields[b + i])
        {
            return false;
        }
    }
    return true;
}

bool
StructuredAddress::MatchesAt(const std::vector<std::pair<size_t, Field>>& eq) const
{
    for (const auto& p : eq)
    {
        if (p.first >= m_fields.size())
        {
            return false;
        }
        if (m_fields[p.first] != p.second)
        {
            return false;
        }
    }
    return true;
}

std::string
StructuredAddress::ToString(char sep, bool withBrackets) const
{
    if (m_fields.empty())
    {
        return withBrackets ? "[]" : "";
    }
    std::string s;
    s.reserve(m_fields.size() * 11 + 2);

    if (withBrackets)
    {
        s.push_back('[');
    }

    for (int i = static_cast<int>(m_fields.size()) - 1; i >= 0; --i)
    {
        s += std::to_string(m_fields[i]);
        if (i > 0)
        {
            s.push_back(sep);
        }
    }

    if (withBrackets)
    {
        s.push_back(']');
    }
    return s;
}

std::string
StructuredAddress::ToStringRange(size_t startIndex,
                                 size_t endIndex,
                                 char sep,
                                 bool withBrackets) const
{
    if (m_fields.empty() || startIndex > endIndex || startIndex >= m_fields.size() ||
        endIndex >= m_fields.size())
    {
        return withBrackets ? "[]" : "";
    }

    std::string s;
    s.reserve((endIndex - startIndex + 1) * 11 + 2);

    if (withBrackets)
    {
        s.push_back('[');
    }

    // Iterate from startIndex down to endIndex
    for (int i = static_cast<int>(endIndex); i >= static_cast<int>(startIndex); --i)
    {
        s += std::to_string(m_fields[i]);
        if (i > static_cast<int>(startIndex))
        {
            s.push_back(sep);
        }
    }

    if (withBrackets)
    {
        s.push_back(']');
    }
    return s;
}

std::ostream&
operator<<(std::ostream& os, const StructuredAddress& addr)
{
    os << addr.ToString();
    return os;
}

} // namespace ns3
