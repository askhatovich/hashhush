// localStorage helpers. All keys are namespaced under "hashhush.".
//
// Per-room state is stored under "hashhush.room.<id>" as JSON {token}.
// We deliberately do NOT persist the encryption seed: it lives in the URL
// fragment (#) only, so a user must have the original invite link to open
// the chat. Keeping the key out of localStorage shrinks the blast radius
// if the storage is ever exfiltrated.

const NS = 'hashhush.';

export const storage = {
    // Per-room access token (re-auth without a challenge on reload).
    saveRoomToken(id, token) {
        localStorage.setItem(NS + 'room.' + id, JSON.stringify({ token }));
    },
    loadRoomToken(id) {
        const raw = localStorage.getItem(NS + 'room.' + id);
        if (!raw) return null;
        try {
            return JSON.parse(raw).token || null;
        } catch {
            return null;
        }
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
