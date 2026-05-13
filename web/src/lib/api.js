// REST helpers. All endpoints return JSON.
// On error responses: throws an Error with `code` and `status` properties.

import { solvePoW } from './pow.js';

async function call(path, opts = {}) {
    const res = await fetch(path, {
        method: opts.method || 'GET',
        headers: opts.body ? { 'Content-Type': 'application/json' } : {},
        body: opts.body ? JSON.stringify(opts.body) : undefined
    });
    let data = null;
    try { data = await res.json(); } catch { /* may be empty */ }
    if (!res.ok) {
        const err = new Error(data?.error_code || `http_${res.status}`);
        err.code = data?.error_code || `http_${res.status}`;
        err.status = res.status;
        throw err;
    }
    return data;
}

export const api = {
    // `onPhase('solving_pow' | 'creating')` lets the UI swap the busy label
    // mid-call. The PoW step runs entirely on the client; the server only
    // sees the solved {challenge, nonce} pair.
    async createRoom(name, requiresPassword, onPhase) {
        onPhase?.('solving_pow');
        const { challenge, difficulty } = await call('/api/pow_challenge');
        const nonce = await solvePoW(challenge, difficulty);
        onPhase?.('creating');
        return call('/api/rooms', {
            method: 'POST',
            body: {
                name,
                requires_password: !!requiresPassword,
                pow: { challenge, nonce }
            }
        });
    },
    activateRoom(id, challenges) {
        return call(`/api/rooms/${encodeURIComponent(id)}/activate`,
                    { method: 'POST', body: { challenges } });
    },
    isAlive(roomIds) {
        return call('/api/is_alive', { method: 'POST', body: { room_ids: roomIds } });
    },
    deleteRoom(id, token) {
        return call(`/api/rooms/${encodeURIComponent(id)}`,
                    { method: 'DELETE', body: { token } });
    }
};
