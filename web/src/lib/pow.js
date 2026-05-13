// Hashcash-style proof-of-work captcha solver.
//
// Given a server-issued `challenge` token and a `difficulty` (leading zero
// bits required), iterate an ASCII nonce until
//   sha256(challenge || nonce)
// has `difficulty` leading zero bits. Returns the winning nonce string.
//
// Hashing goes through libsodium's WASM sha256 — same dependency the chat
// already loads, so no extra bundle cost. With 16 bits of difficulty the
// expected attempts are ~32k and a laptop finishes in tens of milliseconds.

import _sodium from 'libsodium-wrappers-sumo';

const ENCODER = new TextEncoder();

// First-N-bits check on a digest. `bits` divides into `full` whole zero
// bytes plus `rem` bits of the next byte that must be zero.
function meetsDifficulty(digest, bits) {
    const full = bits >> 3;
    const rem  = bits & 7;
    for (let i = 0; i < full; ++i) {
        if (digest[i] !== 0) return false;
    }
    if (rem > 0) {
        const mask = 0xFF << (8 - rem) & 0xFF;
        if ((digest[full] & mask) !== 0) return false;
    }
    return true;
}

export async function solvePoW(challenge, difficulty) {
    await _sodium.ready;
    const sodium = _sodium;
    const challengeBytes = ENCODER.encode(challenge);

    // Yield to the event loop every YIELD_EVERY tries so the UI thread can
    // paint a busy state. At 16 bits the loop rarely runs long enough to
    // notice, but higher difficulty knobs would otherwise freeze the tab.
    const YIELD_EVERY = 4096;

    let counter = 0;
    while (true) {
        for (let i = 0; i < YIELD_EVERY; ++i) {
            const nonceStr = counter.toString();
            const nonceBytes = ENCODER.encode(nonceStr);
            const buf = new Uint8Array(challengeBytes.length + nonceBytes.length);
            buf.set(challengeBytes, 0);
            buf.set(nonceBytes, challengeBytes.length);
            const digest = sodium.crypto_hash_sha256(buf);
            if (meetsDifficulty(digest, difficulty)) {
                return nonceStr;
            }
            ++counter;
        }
        await new Promise(r => setTimeout(r, 0));
    }
}
