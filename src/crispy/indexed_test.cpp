// SPDX-License-Identifier: Apache-2.0
#include <crispy/indexed.h>

#include <fmt/format.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <vector>

using namespace crispy;

using std::array;
using std::vector;

namespace
{
vector<char> getVec()
{
    vector<char> v;
    v.push_back('a');
    v.push_back('b');
    v.push_back('c');
    return v;
}
} // namespace

TEST_CASE("indexed.basic", "[indexed]")
{
    auto const rng = crispy::indexed(array { 'a', 'b', 'c' });
    auto i = rng.begin();
    REQUIRE(i.index == 0);
    REQUIRE(*i.iter == 'a');

    ++i;
    REQUIRE(i.index == 1);
    REQUIRE(*i.iter == 'b');

    ++i;
    REQUIRE(i.index == 2);
    REQUIRE(*i.iter == 'c');

    ++i;
    REQUIRE(i == rng.end());
}

TEST_CASE("indexed.for_loop_basic_lvalue", "[indexed]")
{
    size_t k = 0;
    auto const a = array { 'a', 'b', 'c' };
    for (auto const&& [i, c]: crispy::indexed(a))
    {
        REQUIRE(i == k);
        switch (k)
        {
            case 0: REQUIRE(c == 'a'); break;
            case 1: REQUIRE(c == 'b'); break;
            case 2: REQUIRE(c == 'c'); break;
            default: REQUIRE(false);
        }
        ++k;
    }
}

TEST_CASE("indexed.for_loop_basic_rvalue", "[indexed]")
{
    size_t k = 0;
    for (auto const&& [i, c]: crispy::indexed(vector { 'a', 'b', 'c' }))
    {
        REQUIRE(i == k);
        switch (k)
        {
            case 0: REQUIRE(c == 'a'); break;
            case 1: REQUIRE(c == 'b'); break;
            case 2: REQUIRE(c == 'c'); break;
            default: REQUIRE(false);
        }
        ++k;
    }
}

TEST_CASE("indexed.for_loop_basic_rvalue_via_call", "[indexed]")
{
    size_t k = 0;
    for (auto const&& [i, c]: crispy::indexed(getVec()))
    {
        REQUIRE(i == k);
        switch (k)
        {
            case 0: REQUIRE(c == 'a'); break;
            case 1: REQUIRE(c == 'b'); break;
            case 2: REQUIRE(c == 'c'); break;
            default: REQUIRE(false);
        }
        ++k;
    }
}
