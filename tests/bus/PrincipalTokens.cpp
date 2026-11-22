#include <bus/PrincipalTokens.hpp>
#include <helpers/Token.hpp>

#include <gtest/gtest.h>

using namespace Bus;

namespace {
    const SPrincipalSnapshot SNAPSHOT = {
        .principal = {.identity = {.kind = Security::eIdentityKind::FLATPAK, .id = "org.example.App", .stable = true}, .pid = 42},
        .display   = {.name = "Example"},
        .grants    = {"example.settings"},
    };

    constexpr uint64_t SOURCE = 1, RECIPIENT = 2, OTHER = 3;
}

TEST(PrincipalTokens, RedeemsOnceWithTheAdmissionSnapshot) {
    CPrincipalTokens tokens;
    const auto       TOKEN = tokens.issue(SNAPSHOT, SOURCE, RECIPIENT);

    ASSERT_TRUE(Token::isWellFormed(TOKEN));

    const auto REDEEMED = tokens.redeem(TOKEN, RECIPIENT);
    ASSERT_TRUE(REDEEMED);
    EXPECT_EQ(REDEEMED->source, SOURCE);
    EXPECT_EQ(REDEEMED->snapshot.principal.pid, 42);
    EXPECT_EQ(REDEEMED->snapshot.grants, SNAPSHOT.grants);

    EXPECT_FALSE(tokens.redeem(TOKEN, RECIPIENT));
}

TEST(PrincipalTokens, RejectsOtherRecipientsWithoutConsuming) {
    CPrincipalTokens tokens;
    const auto       TOKEN = tokens.issue(SNAPSHOT, SOURCE, RECIPIENT);

    EXPECT_FALSE(tokens.redeem(TOKEN, OTHER));
    EXPECT_FALSE(tokens.redeem("unknown", RECIPIENT));
    EXPECT_TRUE(tokens.redeem(TOKEN, RECIPIENT));
}

TEST(PrincipalTokens, ExpiresTokens) {
    CPrincipalTokens tokens;
    const auto       NOW   = CPrincipalTokens::Clock::now();
    const auto       TOKEN = tokens.issue(SNAPSHOT, SOURCE, RECIPIENT, NOW);

    EXPECT_FALSE(tokens.redeem(TOKEN, RECIPIENT, NOW + PRINCIPAL_TOKEN_TTL));
}

TEST(PrincipalTokens, BoundsOutstandingTokens) {
    CPrincipalTokens tokens;
    const auto       NOW = CPrincipalTokens::Clock::now();

    for (size_t i = 0; i < MAX_PRINCIPAL_TOKENS; ++i) {
        ASSERT_FALSE(tokens.issue(SNAPSHOT, SOURCE, RECIPIENT, NOW).empty());
    }

    EXPECT_TRUE(tokens.issue(SNAPSHOT, SOURCE, RECIPIENT, NOW).empty());
    EXPECT_FALSE(tokens.issue(SNAPSHOT, SOURCE, RECIPIENT, NOW + PRINCIPAL_TOKEN_TTL).empty());
}

TEST(Token, GeneratesDistinctWellFormedTokens) {
    const auto A = Token::generate(), B = Token::generate();

    EXPECT_NE(A, B);
    EXPECT_TRUE(Token::isWellFormed(A));
    EXPECT_FALSE(Token::isWellFormed(A.substr(1)));
    EXPECT_FALSE(Token::isWellFormed(std::string(32, 'G')));
}
