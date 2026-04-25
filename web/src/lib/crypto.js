// All crypto runs locally via libsodium-wrappers-sumo (WASM). We do not rely
// on the WebCrypto API so the site works over plain http://.

import _sodium from 'libsodium-wrappers-sumo';

let sodium = null;
let readyPromise = null;

export function ready() {
    if (!readyPromise) {
        readyPromise = (async () => {
            await _sodium.ready;
            sodium = _sodium;
        })();
    }
    return readyPromise;
}

// 8-character URL-safe alphabet for the user-visible secret in the URL fragment.
const KEY_ALPHABET = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-';

export function generateKeySeed() {
    const bytes = new Uint8Array(8);
    crypto.getRandomValues(bytes);
    let out = '';
    for (let i = 0; i < 8; ++i) out += KEY_ALPHABET[bytes[i] & 0x3F];
    return out;
}

const ENCODER = new TextEncoder();

function concat(a, b) {
    const out = new Uint8Array(a.length + b.length);
    out.set(a, 0);
    out.set(b, a.length);
    return out;
}

// Four-round salted SHA-256 producing the symmetric key.
//   round 1: sha256("hash" || seed_utf8)
//   round 2: sha256("hush" || round1)
//   round 3: sha256("chat" || round2)
//   round 4: sha256(password_utf8 || round3)
// Salt is concatenated with the data as raw bytes — same on every client.
// `password` is an optional out-of-band shared secret; when omitted it is
// the empty string, which still feeds the fourth round so the chain has a
// single fixed shape regardless of password use.
export async function deriveKey(seed, password = '') {
    await ready();
    const SALTS = ['hash', 'hush', 'chat'].map(s => ENCODER.encode(s));
    let acc = ENCODER.encode(seed);
    for (const salt of SALTS) {
        acc = sodium.crypto_hash_sha256(concat(salt, acc));
    }
    acc = sodium.crypto_hash_sha256(concat(ENCODER.encode(password), acc));
    return acc;   // 32 bytes — XChaCha20-Poly1305 key.
}

// Hex helpers — used to persist a derived key in localStorage so a returning
// peer can reload without re-prompting for the password.
export function bytesToHex(bytes) {
    return Array.from(bytes, b => b.toString(16).padStart(2, '0')).join('');
}

export function hexToBytes(hex) {
    if (typeof hex !== 'string' || hex.length % 2 !== 0) return null;
    const out = new Uint8Array(hex.length / 2);
    for (let i = 0; i < out.length; ++i) {
        const v = parseInt(hex.substr(i * 2, 2), 16);
        if (Number.isNaN(v)) return null;
        out[i] = v;
    }
    return out;
}

export async function randomNonce() {
    await ready();
    return sodium.randombytes_buf(sodium.crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);
}

// Encrypts `plaintextBytes` (Uint8Array) with `key` and a fresh nonce.
// Returns { nonce, ciphertext } as Uint8Array values.
export async function encrypt(plaintextBytes, key) {
    await ready();
    const nonce = sodium.randombytes_buf(sodium.crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);
    const ciphertext = sodium.crypto_aead_xchacha20poly1305_ietf_encrypt(
        plaintextBytes, null, null, nonce, key);
    return { nonce, ciphertext };
}

// Decrypts; throws on auth failure (wrong key, tamper).
export async function decrypt(ciphertextBytes, nonceBytes, key) {
    await ready();
    return sodium.crypto_aead_xchacha20poly1305_ietf_decrypt(
        null, ciphertextBytes, null, nonceBytes, key);
}

// Base64 helpers exported for callers that hand bytes to/from the server.
export async function toB64(bytes) {
    await ready();
    return sodium.to_base64(bytes, sodium.base64_variants.ORIGINAL);
}

export async function fromB64(s) {
    await ready();
    return sodium.from_base64(s, sodium.base64_variants.ORIGINAL);
}

export async function randomBytes(n) {
    await ready();
    return sodium.randombytes_buf(n);
}
