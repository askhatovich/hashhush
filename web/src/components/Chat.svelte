<script>
    import { onMount, onDestroy, tick } from 'svelte';
    import { t } from '../lib/i18n.js';
    import { storage } from '../lib/storage.js';
    import { deriveKey } from '../lib/crypto.js';
    import { ChatClient } from '../lib/ws.js';
    import { api } from '../lib/api.js';
    import { buildJoinUrl } from '../lib/urlfrag.js';
    import { APP_NAME } from '../lib/brand.js';
    import DeleteSliderModal from './DeleteSliderModal.svelte';

    let { roomId, seed, freshlyCreated = false, knownDeleted = false, onLeave } = $props();

    let status = $state('deriving_key');     // deriving_key | connecting | joining | joined | deleted | lost
    let messages = $state([]);                // {peerId, fromSelf, nick, text, ts, system?}
    let members = $state([]);
    let peerNicks = $state({});               // peer_id -> nick
    let myPeerId = $state(null);
    let roomName = $state('');
    let nickname = $state(storage.getNickname() || 'anon');
    let nickEditing = $state(false);
    let nickDraft = $state('');
    let panelOpen = $state(false);
    let inputText = $state('');
    let key = null;
    let client = null;
    let listEl;
    let copied = $state(false);
    let showDeleteModal = $state(false);
    let errorBanner = $state(null);
    let reconnectTimer = null;
    let reconnectAttempt = 0;
    let unmounted = false;

    onMount(async () => {
        // App.svelte already probed the room via /api/is_alive on mount. If
        // it told us the room is gone, jump straight to the deleted screen
        // and skip both key derivation and the WS connection attempt.
        if (knownDeleted) {
            storage.forgetRoom(roomId);
            status = 'deleted';
            return;
        }

        key = await deriveKey(seed);
        spawnClient(/*isInitial=*/true);
    });

    onDestroy(() => {
        unmounted = true;
        if (reconnectTimer) {
            clearTimeout(reconnectTimer);
            reconnectTimer = null;
        }
        if (client) client.close();
        document.title = APP_NAME;
    });

    // Build a fresh ChatClient and start its WS handshake. Used both for the
    // initial connect and every reconnect attempt — we don't reuse instances
    // because the underlying WebSocket is single-shot.
    function spawnClient(isInitial) {
        const token = storage.loadRoomToken(roomId);
        if (isInitial) {
            status = token ? 'joining' : 'connecting';
        }
        client = new ChatClient({
            roomId, key, token, nickname,
            onJoined: info => {
                reconnectAttempt = 0;
                errorBanner = null;
                handleJoined(info);
            },
            onMessage: handleMessage,
            onPresence: (peerId, nick) => {
                peerNicks = { ...peerNicks, [peerId]: nick };
                // peer_join events are appended before presence has a chance
                // to deliver the nick, so they get stamped with null. Fill
                // those in once — and only once — when we learn the nick.
                let patched = false;
                const next = messages.map(m => {
                    if (m.system && m.peerId === peerId && !m.nick) {
                        patched = true;
                        return { ...m, nick };
                    }
                    return m;
                });
                if (patched) messages = next;
            },
            onPeerJoin: id => {
                if (!members.includes(id)) members = [...members, id];
                // Re-announce our nick so the new peer learns it.
                if (client) client.announcePresence();
                appendSystemEvent('join', id);
            },
            onPeerLeave: id => {
                appendSystemEvent('leave', id);
                members = members.filter(m => m !== id);
                const next = { ...peerNicks };
                delete next[id];
                peerNicks = next;
            },
            onDeleted: () => {
                cancelReconnect();
                storage.forgetRoom(roomId);
                status = 'deleted';
            },
            onError: code => {
                // Token no longer recognised by the server — typical when a
                // peer comes back after the room was deleted and recreated,
                // or after the server purged the room. Promote to deleted.
                if (code === 'invalid_token') {
                    cancelReconnect();
                    storage.forgetRoom(roomId);
                    status = 'deleted';
                    return;
                }
                // Browser fires a generic "network" error before close on
                // every dropped connection; suppress that — the reconnect
                // loop will keep the user informed via the status line.
                if (code === 'network') {
                    return;
                }
                errorBanner = `errors.${code}`;
            },
            onClose: clean => {
                if (unmounted || status === 'deleted') return;
                if (status === 'joined' || status === 'connecting' || status === 'joining') {
                    status = 'lost';
                }
                if (!clean) scheduleReconnect();
            }
        });
        client.connect();
    }

    function cancelReconnect() {
        if (reconnectTimer) {
            clearTimeout(reconnectTimer);
            reconnectTimer = null;
        }
        reconnectAttempt = 0;
    }

    // Backoff: 1, 2, 4, 8, 10 (capped). Each tick first probes /api/is_alive
    // so a deleted-room state is surfaced immediately — without it, a
    // backend that returns up after a delete would keep trying forever.
    function scheduleReconnect() {
        if (unmounted || status === 'deleted') return;
        if (reconnectTimer) return;
        const delays = [1000, 2000, 4000, 8000, 10000];
        const delay = delays[Math.min(reconnectAttempt, delays.length - 1)];
        reconnectAttempt += 1;
        reconnectTimer = setTimeout(tryReconnect, delay);
    }

    async function tryReconnect() {
        reconnectTimer = null;
        if (unmounted || status === 'deleted' || status === 'joined') return;
        try {
            const alive = await api.isAlive([roomId]);
            if (alive.gone && alive.gone.includes(roomId)) {
                storage.forgetRoom(roomId);
                status = 'deleted';
                return;
            }
        } catch {
            // Server still unreachable; keep trying.
            scheduleReconnect();
            return;
        }
        if (client) client.close();
        spawnClient(/*isInitial=*/false);
    }

    // Tab title reflects the current room: "RoomName — HashHush".
    $effect(() => {
        document.title = roomName ? `${roomName} — ${APP_NAME}` : APP_NAME;
    });

    async function handleJoined(info) {
        if (info.token) storage.saveRoomToken(roomId, info.token);
        myPeerId = info.peerId;
        roomName = info.roomName || '';
        members = info.members;
        // Seed our own nick so it appears in the member list immediately.
        peerNicks = { ...peerNicks, [info.peerId]: nickname };
        messages = info.history;
        status = 'joined';
        await tick();
        scrollToBottom();
    }

    function startNickEdit() {
        nickDraft = nickname;
        nickEditing = true;
    }
    function commitNick() {
        const trimmed = nickDraft.trim().slice(0, 40);
        if (trimmed && trimmed !== nickname) {
            nickname = trimmed;
            storage.setNickname(nickname);
            // ChatClient reads opts.nickname dynamically on each sendChat,
            // so updating it here applies to all subsequent messages.
            if (client) {
                client.opts.nickname = nickname;
                client.announcePresence();
            }
            if (myPeerId) peerNicks = { ...peerNicks, [myPeerId]: nickname };
        }
        nickEditing = false;
    }
    function cancelNick() {
        nickEditing = false;
    }
    function onNickKey(e) {
        if (e.key === 'Enter') { e.preventDefault(); commitNick(); }
        else if (e.key === 'Escape') { e.preventDefault(); cancelNick(); }
    }
    async function handleMessage(m) {
        if (m.peerId && m.nick) peerNicks = { ...peerNicks, [m.peerId]: m.nick };
        messages = [...messages, m];
        await tick();
        scrollToBottom();
    }

    // System events (join/leave) are kept client-side only — the server's
    // message cache stays untouched, so they don't replay for new joiners.
    // The nickname is snapshotted into the event at creation time so it
    // survives the peerNicks map being trimmed when that peer leaves.
    async function appendSystemEvent(kind, peerId) {
        messages = [...messages, {
            system: true,
            kind,
            peerId,
            nick: peerNicks[peerId] || null,
            ts: Math.floor(Date.now() / 1000)
        }];
        await tick();
        scrollToBottom();
    }

    function scrollToBottom() {
        if (listEl) listEl.scrollTop = listEl.scrollHeight;
    }

    function send() {
        const text = inputText.trim();
        if (!text || !client) return;
        client.sendChat(text);
        inputText = '';
    }
    function onKey(e) {
        if (e.key === 'Enter' && !e.shiftKey) {
            e.preventDefault();
            send();
        }
    }

    async function copyLink() {
        const url = buildJoinUrl(roomId, seed);
        let ok = false;
        // navigator.clipboard requires a secure context (https or localhost),
        // so it won't work over plain http://<host>. Try it first, then fall
        // back to the legacy execCommand('copy') trick which works anywhere
        // when fired from inside a user gesture.
        if (navigator.clipboard && window.isSecureContext) {
            try { await navigator.clipboard.writeText(url); ok = true; } catch { /* fall through */ }
        }
        if (!ok) {
            const ta = document.createElement('textarea');
            ta.value = url;
            ta.setAttribute('readonly', '');
            ta.style.position = 'fixed';
            ta.style.opacity = '0';
            document.body.appendChild(ta);
            ta.select();
            try { ok = document.execCommand('copy'); } catch { ok = false; }
            document.body.removeChild(ta);
        }
        if (ok) {
            // Flash the success state only when the URL is actually in the
            // clipboard. Otherwise we'd lie to the user.
            copied = true;
            setTimeout(() => copied = false, 1500);
        } else {
            // Last-ditch: show the link in a prompt so the user can copy by
            // hand. No success flash — nothing landed in the clipboard.
            window.prompt('Copy link', url);
        }
    }

    async function confirmDelete() {
        showDeleteModal = false;
        const token = storage.loadRoomToken(roomId);
        if (!token) {
            errorBanner = 'errors.invalid_token';
            return;
        }
        try {
            // Close our own WS first. The server's `deleted`-broadcast will
            // force-close all room peers; closing on our side beforehand
            // avoids the browser firing a misleading 'error' event back at
            // the user that just performed the deletion.
            if (client) client.close();
            await api.deleteRoom(roomId, token);
            storage.forgetRoom(roomId);
            // Show the same "room deleted" landing page that other peers see,
            // instead of bouncing straight home. The user clicks back manually.
            status = 'deleted';
        } catch (e) {
            errorBanner = `errors.${e.code || 'internal'}`;
        }
    }
</script>

<section class="chat">
{#if status === 'deleted'}
    <div class="deleted-screen">
        <div class="deleted-msg">{$t('chat.room_deleted')}</div>
        <button class="ghost" onclick={() => onLeave && onLeave()}>{$t('chat.back_home')}</button>
    </div>
{:else}
    <header class="bar">
        <button class="ghost back" onclick={() => onLeave && onLeave()} aria-label="back">←</button>

        <button
            class="hdr-toggle"
            onclick={() => panelOpen = !panelOpen}
            aria-expanded={panelOpen}
            aria-controls="member-panel"
        >
            <div class="hdr-line">
                {#if nickEditing}
                    <input
                        class="nick-input"
                        type="text"
                        bind:value={nickDraft}
                        maxlength="40"
                        autofocus
                        onkeydown={onNickKey}
                        onblur={commitNick}
                        onclick={(e) => e.stopPropagation()}
                    />
                {:else}
                    <span
                        class="nick"
                        role="button"
                        tabindex="0"
                        onclick={(e) => { e.stopPropagation(); startNickEdit(); }}
                        onkeydown={(e) => { if (e.key === 'Enter') { e.stopPropagation(); startNickEdit(); } }}
                        title="Edit nickname"
                    >{nickname}<span class="pencil" aria-hidden="true">✎</span></span>
                {/if}
                <span class="room-id dim">{roomName || `/r/${roomId}`}</span>
                <span class="caret dim" class:open={panelOpen}>▾</span>
            </div>
            <div class="status dim">
                {#if status === 'joined'}
                    {members.length} · {$t('chat.members').toLowerCase()}
                {:else}
                    {$t(`chat.${status === 'lost' ? 'connection_lost' : status}`)}
                {/if}
            </div>
        </button>

        <button
            class="ghost copy-btn"
            class:copied
            onclick={copyLink}
            title={copied ? $t('chat.copied') : $t('chat.copy_link')}
            aria-label={copied ? $t('chat.copied') : $t('chat.copy_link')}
        >🔗</button>
        <button class="danger" onclick={() => showDeleteModal = true}>{$t('chat.delete')}</button>
    </header>

    {#if panelOpen}
        <div class="member-panel" id="member-panel">
            <div class="panel-title dim">{$t('chat.members')}</div>
            <ul class="member-list">
                {#each members as id (id)}
                    <li class="member" class:you={id === myPeerId}>
                        <span class="dot"></span>
                        <span class="m-nick">{peerNicks[id] || '—'}</span>
                        <span class="m-id mono dim">{id.slice(0, 8)}</span>
                        {#if id === myPeerId}<span class="dim"> · {$t('chat.you')}</span>{/if}
                    </li>
                {/each}
                {#if members.length === 0}
                    <li class="dim">—</li>
                {/if}
            </ul>
        </div>
    {/if}

    {#if errorBanner}
        <div class="banner error">{$t(errorBanner)}</div>
    {/if}

        <div class="msglist" bind:this={listEl}>
            {#each messages as m, i (i)}
                {#if m.system}
                    <div class="sys-msg dim">
                        {#if m.kind === 'join'}
                            {$t('chat.sys_joined').replace('{nick}', m.nick || $t('chat.unknown_peer'))}
                        {:else if m.kind === 'leave'}
                            {$t('chat.sys_left').replace('{nick}', m.nick || $t('chat.unknown_peer'))}
                        {/if}
                    </div>
                {:else}
                    <div class="msg" class:self={m.fromSelf}>
                        <div class="meta dim">
                            <span class="nick">{m.nick || (m.fromSelf ? $t('chat.you') : '·')}</span>
                            <span class="ts">{new Date((m.ts || 0) * 1000).toLocaleTimeString()}</span>
                        </div>
                        <div class="bubble">{m.text}</div>
                    </div>
                {/if}
            {/each}
        </div>

        <div class="composer">
            <textarea
                rows="1"
                placeholder={$t('chat.send_placeholder')}
                bind:value={inputText}
                onkeydown={onKey}
                disabled={status !== 'joined'}
            ></textarea>
            <button onclick={send} disabled={status !== 'joined' || !inputText.trim()}>↵</button>
        </div>
{/if}
</section>

{#if showDeleteModal}
    <DeleteSliderModal
        onConfirm={confirmDelete}
        onCancel={() => showDeleteModal = false}
    />
{/if}

<style>
    .chat {
        flex: 1;
        display: flex;
        flex-direction: column;
        min-height: 0;
    }
    .bar {
        display: flex;
        align-items: center;
        gap: 8px;
        padding-bottom: 12px;
        border-bottom: 1px solid var(--border);
        margin-bottom: 12px;
    }
    .bar .back { padding: 6px 10px; }
    .bar .status { font-size: 12px; }

    .hdr-toggle {
        flex: 1;
        min-width: 0;
        display: flex;
        flex-direction: column;
        align-items: flex-start;
        gap: 2px;
        background: transparent;
        border: 0;
        padding: 6px 8px;
        text-align: left;
        color: inherit;
        cursor: pointer;
        border-radius: var(--radius);
    }
    .hdr-toggle:hover { background: var(--bg-elev-2); }

    .hdr-line {
        display: flex;
        align-items: center;
        gap: 8px;
        width: 100%;
        font-size: 14px;
        flex-wrap: wrap;
    }
    /* Narrow screens: stack the nick above the room name so they don't
       collide. The caret stays on the first row, anchored right. */
    @media (max-width: 540px) {
        .hdr-line { gap: 2px 8px; }
        .hdr-line .room-id {
            flex-basis: 100%;
            order: 3;
        }
        .hdr-line .caret { order: 2; }
        .hdr-line .nick, .hdr-line .nick-input { order: 1; }
    }
    .nick {
        font-weight: 600;
        cursor: text;
        padding: 2px 6px;
        border-radius: 6px;
        display: inline-flex;
        align-items: center;
        gap: 4px;
    }
    .nick:hover { background: var(--bg-elev-2); }
    .pencil { font-size: 11px; opacity: 0.55; }
    .nick-input {
        font-weight: 600;
        padding: 2px 6px;
        width: auto;
        max-width: 240px;
    }
    .room-id { flex-shrink: 1; min-width: 0; overflow: hidden; text-overflow: ellipsis; }
    .caret {
        margin-left: auto;
        transition: transform 0.16s ease;
        font-size: 12px;
    }
    .caret.open { transform: rotate(180deg); }

    .member-panel {
        background: var(--bg-elev);
        border: 1px solid var(--border);
        border-radius: var(--radius);
        padding: 10px 12px;
        margin: -4px 0 12px;
        animation: fade-in 0.16s ease-out;
    }
    .panel-title {
        font-size: 11px;
        text-transform: uppercase;
        letter-spacing: 0.06em;
        margin-bottom: 6px;
    }
    .member-list {
        list-style: none;
        margin: 0;
        padding: 0;
        display: flex;
        flex-direction: column;
        gap: 4px;
    }
    .member {
        display: flex;
        align-items: center;
        gap: 8px;
        font-size: 13px;
        padding: 4px 0;
    }
    .member .dot {
        width: 8px;
        height: 8px;
        border-radius: 50%;
        background: var(--accent);
    }
    .member.you { font-weight: 600; }
    .m-nick { font-weight: 500; }
    .m-id { font-size: 12px; }

    .banner {
        padding: 10px 12px;
        border-radius: var(--radius);
        margin-bottom: 8px;
        font-size: 14px;
    }
    .banner.error {
        background: rgba(239, 102, 113, 0.08);
        border: 1px solid rgba(239, 102, 113, 0.4);
        color: var(--danger);
    }

    .deleted-screen {
        flex: 1;
        display: flex;
        flex-direction: column;
        align-items: center;
        justify-content: center;
        gap: 18px;
        text-align: center;
        animation: fade-in 0.18s ease-out;
    }
    /* The copy-link button keeps a fixed icon-only size; success is signalled
       via a transient accent tint instead of swapping in a long text label. */
    .copy-btn { transition: background 0.18s ease, color 0.18s ease, border-color 0.18s ease; }
    .copy-btn.copied {
        background: var(--accent);
        color: #0a1614;
        border-color: transparent;
    }

    .deleted-msg {
        font-size: 16px;
        color: var(--text-dim);
    }

    .msglist {
        flex: 1;
        /* min-height:0 is what unlocks scrolling for a flex child whose
           contents would otherwise push the parent past the viewport. */
        min-height: 0;
        overflow-y: auto;
        overscroll-behavior: contain;
        /* Reserve a stable gutter so messages don't jump horizontally when
           the scrollbar appears/disappears. */
        scrollbar-gutter: stable;
        padding: 4px 6px 8px 2px;
        display: flex;
        flex-direction: column;
        gap: 8px;

        /* Firefox: slim, themed scrollbar instead of the OS default. */
        scrollbar-width: thin;
        scrollbar-color: var(--border) transparent;
    }
    /* WebKit/Blink (Chrome, Safari, Edge): match the slim themed look. */
    .msglist::-webkit-scrollbar {
        width: 8px;
    }
    .msglist::-webkit-scrollbar-track {
        background: transparent;
    }
    .msglist::-webkit-scrollbar-thumb {
        background: var(--border);
        border-radius: 8px;
        /* Inset border creates breathing room around the thumb so it doesn't
           touch the chat edge. */
        border: 2px solid transparent;
        background-clip: padding-box;
    }
    .msglist::-webkit-scrollbar-thumb:hover {
        background: var(--text-dim);
        background-clip: padding-box;
    }
    .msglist:hover {
        scrollbar-color: var(--text-dim) transparent;
    }
    .msg { animation: fade-in 0.15s ease-out; }
    .msg.self { align-self: flex-end; max-width: 85%; }
    .msg:not(.self) { align-self: flex-start; max-width: 85%; }
    .msg .meta {
        font-size: 11px;
        display: flex;
        gap: 8px;
        margin-bottom: 2px;
    }
    .msg.self .meta { justify-content: flex-end; }
    .bubble {
        padding: 8px 12px;
        background: var(--bg-elev);
        border: 1px solid var(--border);
        border-radius: 14px;
        white-space: pre-wrap;
        word-wrap: break-word;
    }
    .msg.self .bubble {
        background: var(--accent);
        color: #0a1614;
        border-color: transparent;
    }

    .sys-msg {
        align-self: center;
        font-size: 11px;
        font-style: italic;
        padding: 2px 10px;
        opacity: 0.75;
        animation: fade-in 0.15s ease-out;
    }

    .composer {
        display: flex;
        gap: 8px;
        padding-top: 12px;
        border-top: 1px solid var(--border);
    }
    .composer textarea {
        flex: 1;
        resize: none;
        max-height: 160px;
    }
    .composer button {
        align-self: stretch;
        padding: 0 16px;
    }
</style>
