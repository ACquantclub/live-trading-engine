#include <csp/core/Time.h>
#include <csp/engine/CspType.h>
#include <csp/engine/Engine.h>
#include <gtest/gtest.h>

TEST(CSPIntegration, BasicEngineCreation) {
    // Test that we can create CSP engine components
    csp::DateTime now = csp::DateTime::now();
    EXPECT_TRUE(now.asNanoseconds() > 0);

    // Test basic time operations
    csp::TimeDelta delta = csp::TimeDelta::fromMinutes(5);
    EXPECT_EQ(delta.minutes(), 5);
}

TEST(CSPIntegration, BasicTypes) {
    // Test CSP type system works
    auto boolType = csp::CspType::BOOL();
    EXPECT_TRUE(boolType->isNative());

    auto doubleType = csp::CspType::DOUBLE();
    EXPECT_TRUE(doubleType->isNative());
}

TEST(CSPIntegration, TimeDelta) {
    // Test TimeDelta creation and operations
    csp::TimeDelta td1 = csp::TimeDelta::fromSeconds(60);
    EXPECT_EQ(td1.minutes(), 1);
    EXPECT_EQ(td1.asSeconds(), 60);

    csp::TimeDelta td2 = csp::TimeDelta::fromMilliseconds(1500);
    EXPECT_EQ(td2.asMilliseconds(), 1500);
    EXPECT_EQ(td2.asSeconds(), 1);

    // Test TimeDelta arithmetic
    csp::TimeDelta sum = td1 + td2;
    EXPECT_EQ(sum.asMilliseconds(), 61500);
}

TEST(CSPIntegration, DateTime) {
    // Test DateTime creation and basic operations
    csp::DateTime dt1 = csp::DateTime::now();
    csp::DateTime dt2 = dt1 + csp::TimeDelta::fromSeconds(30);

    EXPECT_GT(dt2.asNanoseconds(), dt1.asNanoseconds());

    csp::TimeDelta diff = dt2 - dt1;
    EXPECT_EQ(diff.asSeconds(), 30);
}