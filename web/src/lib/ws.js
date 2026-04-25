import { decrypt, encrypt, fromB64, toB64 } from './crypto.js';

// Bidirectional protocol client. Hides the join handshake (token-or-challenge)
// from the UI and exposes typed callbacks for joined/message/peer-events.
//
// Constructor opts:
//   roomId       — string
//   key          — Uint8Array (32 bytes), already derived from the URL seed
//   token?       — existing access token (returning user); omit to use challenge flow
//   nickname     — string (sent inside encrypted payload)
//   onJoined     — ({peerId, members, history}) => void; history is decrypted
//   onMessage    — ({peerId, fromSelf, text, ts}) => void
//   onPeerJoin   — (peerId) => void
//   onPeerLeave  — (peerId) => void
//   onDeleted    — () => void
//   onError      — (errorCode) => void
//   onClose      — (clean) => void
export class ChatClient {
    constructor(opts) {
        this.opts = opts;
        this.ws = null;
        this.peerId = null;
        this.token = opts.token || null;
        this.joined = false;
        this.closedByUs = false;
    }

    connect() {
        const wsProto = location.protocol === 'https:' ? 'wss' : 'ws';
        const params = new URLSearchParams({ room: this.opts.roomId });
        if (this.token) params.set('token', this.token);
        const url = `${wsProto}://${location.host}/ws?${params.toString()}`;
        this.ws = new WebSocket(url);
        this.ws.addEventListener('open',  () => this.#onOpen());
        this.ws.addEventListener('message', e => this.#onFrame(e.data));
        this.ws.addEventListener('close', e => this.#onClose(e));
        // Browsers fire 'error' before 'close' for any non-clean shutdown,
        // including server-initiated close after the deleter's own DELETE
        // call. Suppress the error path once we know we initiated the close.
        this.ws.addEventListener('error', () => {
            if (this.closedByUs) return;
            this.opts.onError && this.opts.onError('network');
        });
    }

    close() {
        this.closedByUs = true;
        if (this.ws && this.ws.readyState <= 1) this.ws.close();
    }

    async sendChat(text) {
        await this.#sendEncrypted('msg', { kind: 'chat', text, nick: this.opts.nickname });
    }

    // Announces the current nickname to other peers. Sent on join and again
    // whenever a new peer arrives (so they can populate the member list with
    // nicks, not just peer ids). Goes out as `type:"presence"` so the server
    // relays it without writing to the message cache — presence chatter must
    // not evict real chat messages from the ring buffer.
    async announcePresence() {
        await this.#sendEncrypted('presence', { kind: 'presence', nick: this.opts.nickname });
    }

    async #sendEncrypted(wsType, payloadObj) {
        if (!this.joined) return;
        const bytes = new TextEncoder().encode(JSON.stringify(payloadObj));
        const { nonce, ciphertext } = await encrypt(bytes, this.opts.key);
        const blob = new Uint8Array(nonce.length + ciphertext.length);
        blob.set(nonce, 0);
        blob.set(ciphertext, nonce.length);
        const b64 = await toB64(blob);
        this.ws.send(JSON.stringify({ type: wsType, payload: b64 }));
    }

    #onOpen() {
        const hello = { type: 'hello' };
        if (this.token) hello.token = this.token;
        this.ws.send(JSON.stringify(hello));
    }

    async #onFrame(raw) {
        let frame;
        try { frame = JSON.parse(raw); } catch { return; }

        if (frame.type === 'challenge') {
            try {
                const ct = await fromB64(frame.ciphertext);
                const nc = await fromB64(frame.nonce);
                const plaintext = await decrypt(ct, nc, this.opts.key);
                const reply = { type: 'challenge_response', plaintext: await toB64(plaintext) };
                this.ws.send(JSON.stringify(reply));
            } catch {
                this.opts.onError && this.opts.onError('invalid_key');
                this.close();
            }
            return;
        }

        if (frame.type === 'joined') {
            this.peerId = frame.peer_id;
            this.joined = true;
            if (frame.token) this.token = frame.token;
            const decoded = [];
            for (const m of (frame.history || [])) {
                const decrypted = await this.#decryptPayload(m.payload);
                if (decrypted && decrypted.kind === 'chat') {
                    decoded.push({
                        peerId: m.peer_id || null,
                        fromSelf: !!m.peer_id && m.peer_id === this.peerId,
                        text: decrypted.text,
                        nick: decrypted.nick,
                        ts: m.ts,
                        historic: true
                    });
                }
            }
            this.opts.onJoined && this.opts.onJoined({
                peerId: this.peerId,
                token: this.token,
                roomName: frame.room_name || '',
                members: frame.members || [],
                history: decoded
            });
            // Tell other peers our nick.
            this.announcePresence();
            return;
        }

        if (frame.type === 'msg') {
            const decrypted = await this.#decryptPayload(frame.payload);
            if (!decrypted || decrypted.kind !== 'chat') return;
            this.opts.onMessage && this.opts.onMessage({
                peerId: frame.peer_id,
                fromSelf: frame.peer_id === this.peerId,
                text: decrypted.text,
                nick: decrypted.nick,
                ts: frame.ts
            });
            return;
        }

        if (frame.type === 'presence') {
            const decrypted = await this.#decryptPayload(frame.payload);
            if (!decrypted || decrypted.kind !== 'presence') return;
            this.opts.onPresence && this.opts.onPresence(frame.peer_id, decrypted.nick);
            return;
        }

        if (frame.type === 'peer_join') {
            this.opts.onPeerJoin && this.opts.onPeerJoin(frame.peer_id);
            return;
        }
        if (frame.type === 'peer_leave') {
            this.opts.onPeerLeave && this.opts.onPeerLeave(frame.peer_id);
            return;
        }
        if (frame.type === 'deleted') {
            this.closedByUs = true;
            this.opts.onDeleted && this.opts.onDeleted();
            return;
        }
        if (frame.type === 'error') {
            this.opts.onError && this.opts.onError(frame.code || 'internal');
            return;
        }
    }

    async #decryptPayload(b64) {
        try {
            const blob = await fromB64(b64);
            const NPUB = 24;   // XChaCha20-Poly1305 nonce length
            const nonce = blob.slice(0, NPUB);
            const ct = blob.slice(NPUB);
            const pt = await decrypt(ct, nonce, this.opts.key);
            const obj = JSON.parse(new TextDecoder().decode(pt));
            if (obj && (obj.kind === 'chat' || obj.kind === 'presence')) return obj;
        } catch { /* fall through */ }
        return null;
    }

    #onClose() {
        this.opts.onClose && this.opts.onClose(this.closedByUs);
    }
}
