<script>
    import { t, lang } from '../lib/i18n.js';
    import { api } from '../lib/api.js';
    import { storage } from '../lib/storage.js';
    import { bytesToHex, deriveKey, encrypt, generateKeySeed, randomBytes, toB64 } from '../lib/crypto.js';
    import { randomNickname } from '../lib/words.js';

    let { initialError = null, appName = 'HashHush', info = null, onCreated } = $props();

    let nickname = $state(storage.getNickname() || randomNickname());
    let roomName = $state('');
    let password = $state('');
    let busy = $state(false);
    // Sub-phase of the busy state. Empty when idle; otherwise one of
    // 'solving_pow' | 'creating' | 'activating' so the button label can
    // mirror what's actually happening.
    let phase = $state('');
    let error = $state(initialError ? `errors.${initialError}` : null);

    $effect(() => { if (nickname) storage.setNickname(nickname); });

    // Russian uses three plural forms (one/few/many); English uses two.
    // Pick the matching word so we render "1 день" / "2 дня" / "5 дней"
    // and "1 day" / "2 days" rather than the abbreviated "1 д".
    function pluralKey(n) {
        if ($lang === 'ru') {
            const mod10 = n % 10, mod100 = n % 100;
            if (mod10 === 1 && mod100 !== 11) return 'one';
            if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14)) return 'few';
            return 'many';
        }
        return n === 1 ? 'one' : 'many';
    }

    function unitWord(unit, n) {
        return $t(`home.unit_${unit}_${pluralKey(n)}`);
    }

    function formatTtl(seconds) {
        // Pick the coarsest readable unit so the home page reads naturally
        // ("1 day" / "12 hours") rather than "86400 seconds".
        if (seconds % 86400 === 0) {
            const n = seconds / 86400;
            return `${n} ${unitWord('day', n)}`;
        }
        if (seconds >= 3600) {
            const n = Math.round(seconds / 3600);
            return `${n} ${unitWord('hour', n)}`;
        }
        const n = Math.max(1, Math.round(seconds / 60));
        return `${n} ${unitWord('minute', n)}`;
    }

    async function create() {
        if (busy) return;
        busy = true;
        phase = 'solving_pow';
        error = null;
        try {
            const requiresPassword = password.length > 0;
            const created = await api.createRoom(
                roomName.trim() || 'Secret chat',
                requiresPassword,
                p => { phase = p; }
            );
            const seed = generateKeySeed();
            const key = await deriveKey(seed, password);

            const challenges = [];
            for (let i = 0; i < created.challenges_required; ++i) {
                const plaintext = await randomBytes(32);
                const { nonce, ciphertext } = await encrypt(plaintext, key);
                challenges.push({
                    plaintext:  await toB64(plaintext),
                    ciphertext: await toB64(ciphertext),
                    nonce:      await toB64(nonce)
                });
            }
            await api.activateRoom(created.room_id, challenges);

            // Cache the derived key so this browser can reload the room
            // without asking for the password again. Drop the password
            // itself from JS memory.
            storage.saveRoomKey(created.room_id, bytesToHex(key));
            password = '';

            // Don't persist the seed; it stays in the URL fragment only.
            // Path stays "/" — both room id and seed live in the fragment.
            history.replaceState(null, '', `/#${created.room_id}:${seed}`);
            onCreated && onCreated({ roomId: created.room_id, seed, freshlyCreated: true });
        } catch (e) {
            error = `errors.${e.code || 'internal'}`;
        } finally {
            busy = false;
            phase = '';
        }
    }

    function busyLabel() {
        if (phase === 'solving_pow') return $t('home.solving_captcha');
        return $t('home.creating');
    }
</script>

<section class="home">
    <header class="hero">
        <h1 class="title">{appName}</h1>
        <p class="subtitle dim">{$t('home.subtitle')}</p>
    </header>

    <div class="card">
        <div class="field">
            <label for="room">{$t('home.room_name')}</label>
            <input id="room" type="text" placeholder={$t('home.room_name_placeholder')} bind:value={roomName} maxlength="80" />
        </div>

        <div class="field">
            <label for="nick">{$t('home.nickname')}</label>
            <input id="nick" type="text" bind:value={nickname} maxlength="40" />
        </div>

        <div class="field">
            <label for="password">{$t('home.password_label')}</label>
            <input
                id="password"
                type="password"
                autocomplete="new-password"
                placeholder={$t('home.password_placeholder')}
                bind:value={password}
            />
            <p class="hint dim">{$t('home.password_hint')}</p>
        </div>

        {#if error}<p class="error">{$t(error)}</p>{/if}

        <button class="primary" onclick={create} disabled={busy}>
            {busy ? busyLabel() : $t('home.create')}
        </button>
    </div>

    {#if info}
        <ul class="policy dim">
            <li>{$t('home.info_max').replace('{count}', info.max_participants)}</li>
            <li>{$t('home.info_ttl').replace('{duration}', formatTtl(info.idle_ttl_seconds))}</li>
            <li>{$t('home.info_cache').replace('{count}', info.message_cache_size)}</li>
            <li>{$t('home.privacy_note')}</li>
        </ul>
    {/if}

    {#if info?.admin_text}
        <!-- Rendered as raw HTML. The value comes from the operator-controlled
             server config; the operator already has full code execution on
             this host, so trusting their markup adds no new attack surface.
             Anyone with write access to config.ini gets the same trust. -->
        <p class="admin-text dim">{@html info.admin_text}</p>
    {/if}
</section>

<style>
    .home {
        flex: 1;
        display: flex;
        flex-direction: column;
        gap: 28px;
    }

    /* Hero */
    .hero {
        text-align: center;
        padding-top: 8px;
    }
    .title {
        font-size: 34px;
        line-height: 1.1;
        letter-spacing: -0.02em;
        margin: 0 0 6px;
    }
    .subtitle {
        margin: 0 auto;
        max-width: 460px;
        font-size: 14px;
    }

    /* Form card */
    .card {
        background: var(--bg-elev);
        border: 1px solid var(--border);
        border-radius: 14px;
        padding: 18px;
        box-shadow: var(--shadow);
    }
    .card .field { margin-bottom: 12px; }
    .card .primary {
        width: 100%;
        padding: 12px 16px;
        font-size: 15px;
        margin-top: 4px;
    }

    /* Policy facts */
    .policy {
        list-style: none;
        margin: 0;
        padding: 0 4px;
        font-size: 12px;
        line-height: 1.7;
    }
    .policy li::before {
        content: '·';
        margin-right: 6px;
        opacity: 0.55;
    }

    /* Operator-supplied free-form text. Same visual weight as .policy so
       it reads as part of the server-info column, not as chrome. */
    .admin-text {
        margin: 0;
        padding: 0 4px;
        font-size: 12px;
        line-height: 1.7;
        white-space: pre-wrap;
    }
    /* Inherit the dim body colour and only highlight on hover, so links
       sit inside the paragraph instead of glowing default-browser-blue
       (which is unreadable on the dark theme). */
    .admin-text :global(a) {
        color: inherit;
        text-decoration: underline;
        text-decoration-color: var(--border);
        text-underline-offset: 2px;
        cursor: pointer;
        transition: color 0.12s ease, text-decoration-color 0.12s ease;
    }
    .admin-text :global(a:hover) {
        color: var(--accent);
        text-decoration-color: var(--accent);
    }

    .hint {
        font-size: 11px;
        margin: 4px 2px 0;
    }

    /* Mirror the chat composer rule: at narrow widths the placeholder text
       would wrap or clip; shrinking only the placeholder font (not the
       typed value) keeps the field looking the same when filled. */
    @media (max-width: 540px) {
        .card input::placeholder { font-size: 12px; }
    }


    .error {
        background: rgba(239, 102, 113, 0.08);
        border: 1px solid rgba(239, 102, 113, 0.4);
        color: var(--danger);
        padding: 10px 12px;
        border-radius: var(--radius);
        margin: 0 0 12px;
        font-size: 14px;
    }
</style>
