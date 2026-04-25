// Both the room id and the encryption key seed live in the URL fragment (#),
// after a single canonical path "/". Nothing about the room is ever sent to
// the server in the HTTP request line — it only sees the bare path "/" on
// page load. Fragment format: #<roomId>:<seed>
//
// (The room id still goes over the wire on subsequent /api/* calls, but
//  keeping it out of GET / requests lets bookmark managers, browser
//  history sync and stray reverse-proxy logs miss it on the initial hit.)

const FRAGMENT_RE = /^([A-Za-z0-9_-]{8}):([A-Za-z0-9_-]+)$/;

export function buildJoinUrl(roomId, seed) {
    return `${location.origin}/#${roomId}:${seed}`;
}

export function parseLocation() {
    if (!location.hash || location.hash.length < 2) {
        return { roomId: null, seed: null };
    }
    const raw = decodeURIComponent(location.hash.slice(1));
    const m = raw.match(FRAGMENT_RE);
    if (!m) {
        return { roomId: null, seed: null };
    }
    return { roomId: m[1], seed: m[2] };
}

export function goHome() {
    history.replaceState(null, '', '/');
    location.hash = '';
}
