// REST helpers. All endpoints return JSON.
// On error responses: throws an Error with `code` and `status` properties.

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
    createRoom(name) {
        return call('/api/rooms', { method: 'POST', body: { name } });
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
