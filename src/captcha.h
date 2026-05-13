// Copyright (C) 2026 Roman Lyubimov
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

namespace captcha {

// Result of verifying a client-submitted proof of work. The webapi layer
// maps these to wire error codes so the client can distinguish a transient
// expiry (auto-retryable) from a malformed or replayed submission.
enum class VerifyResult {
    Ok,
    Malformed,    // challenge token format / decoding bad
    BadHmac,      // server did not issue this challenge
    Expired,      // exp < now
    BadProof,     // hash does not meet difficulty
    Reused        // already consumed in a previous accept
};

// Initialise the per-process HMAC secret. Idempotent. Must be called before
// any issueChallenge() / verifyChallenge() call.
void init();

// Returns a challenge token of the form "nonce_hex:exp_unix:hmac_hex".
// The token is self-describing — the server holds no state about it until
// the proof is accepted, at which point the nonce_hex is recorded for
// single-use enforcement.
std::string issueChallenge(int validityWindowSeconds = 60);

// Verifies `challenge` (the full token string the client received) and
// `nonce` (the ASCII counter the client found). `difficultyBits` is the
// required count of leading zero bits in sha256(challenge || nonce).
// On VerifyResult::Ok, the challenge's nonce_hex is recorded so it cannot
// be reused.
VerifyResult verifyChallenge(const std::string& challenge,
                             const std::string& nonce,
                             int difficultyBits);

}  // namespace captcha
