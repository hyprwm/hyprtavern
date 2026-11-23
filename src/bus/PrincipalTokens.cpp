#include "PrincipalTokens.hpp"
#include "../helpers/Token.hpp"

#include <algorithm>

using namespace Bus;

std::string CPrincipalTokens::issue(SPrincipalSnapshot snapshot, uint64_t source, uint64_t recipient, Clock::time_point now) {
    prune(now);

    if (m_tokens.size() >= MAX_PRINCIPAL_TOKENS)
        return "";

    const auto OUTSTANDING = std::ranges::count_if(m_tokens, [source](const auto& entry) { return entry.second.principal.source == source; });
    if (sc<size_t>(OUTSTANDING) >= MAX_PRINCIPAL_TOKENS_PER_SOURCE)
        return "";

    auto token = Token::generate();

    m_tokens.emplace(token,
                     SEntry{
                         .principal = {.snapshot = std::move(snapshot), .source = source},
                         .recipient = recipient,
                         .expiry    = now + PRINCIPAL_TOKEN_TTL,
                     });

    return token;
}

std::optional<SRedeemedPrincipal> CPrincipalTokens::redeem(const std::string& token, uint64_t recipient, Clock::time_point now) {
    prune(now);

    const auto IT = m_tokens.find(token);
    if (IT == m_tokens.end() || IT->second.recipient != recipient)
        return std::nullopt;

    auto principal = std::move(IT->second.principal);
    m_tokens.erase(IT);

    return principal;
}

void CPrincipalTokens::prune(Clock::time_point now) {
    std::erase_if(m_tokens, [now](const auto& entry) { return entry.second.expiry <= now; });
}
