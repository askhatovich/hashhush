// localStorage helpers. All keys are namespaced under "hashhush.".
//
// Per-room state is stored under "hashhush.room.<id>" as JSON {token, key}:
//   token — access token to skip the challenge dance on reconnect;
//   key   — hex-encoded 32-byte AEAD key the room uses. Only ever written
//           after a successful join, so it is known to be the right one.
//
// We deliberately do NOT persist the encryption seed or the user-typed
// password: the seed lives in the URL fragment (#) only, and the password
// is dropped from JS memory once the key has been derived. Keeping the
// derived key around is what lets a returning peer reload without being
// asked for the password again.

const NS = 'hashhush.';

function loadRoomEntry(id) {
    const raw = localStorage.getItem(NS + 'room.' + id);
    if (!raw) return {};
    try {
        return JSON.parse(raw) || {};
    } catch {
        return {};
    }
}

function saveRoomEntry(id, entry) {
    localStorage.setItem(NS + 'room.' + id, JSON.stringify(entry));
}

export const storage = {
    saveRoomToken(id, token) {
        const entry = loadRoomEntry(id);
        entry.token = token;
        saveRoomEntry(id, entry);
    },
    loadRoomToken(id) {
        return loadRoomEntry(id).token || null;
    },
    saveRoomKey(id, keyHex) {
        const entry = loadRoomEntry(id);
        entry.key = keyHex;
        saveRoomEntry(id, entry);
    },
    loadRoomKey(id) {
        return loadRoomEntry(id).key || null;
    },
    forgetRoom(id) {
        localStorage.removeItem(NS + 'room.' + id);
    },
    knownRoomIds() {
        const out = [];
        for (let i = 0; i < localStorage.length; ++i) {
            const k = localStorage.key(i);
            if (k && k.startsWith(NS + 'room.')) out.push(k.slice((NS + 'room.').length));
        }
        return out;
    },

    // Profile.
    getNickname() {
        return localStorage.getItem(NS + 'nick') || '';
    },
    setNickname(nick) {
        localStorage.setItem(NS + 'nick', nick);
    },

    // Theme: 'dark' | 'light'.
    getTheme() {
        return localStorage.getItem(NS + 'theme') || 'dark';
    },
    setTheme(t) {
        localStorage.setItem(NS + 'theme', t);
    },

    // Language: 'en' | 'ru'.
    getLang() {
        return localStorage.getItem(NS + 'lang') || '';
    },
    setLang(l) {
        localStorage.setItem(NS + 'lang', l);
    }
};
