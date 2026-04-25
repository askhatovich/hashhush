<script>
    import { onMount, onDestroy } from 'svelte';
    import Home from './components/Home.svelte';
    import Chat from './components/Chat.svelte';
    import Footer from './components/Footer.svelte';
    import { storage } from './lib/storage.js';
    import { parseLocation, goHome } from './lib/urlfrag.js';
    import { api } from './lib/api.js';
    import { ready as cryptoReady } from './lib/crypto.js';
    import { APP_NAME } from './lib/brand.js';

    let route = $state({ name: 'home' });
    let cryptoLoaded = $state(false);
    let appName = $state('HashHush');
    let serverInfo = $state(null);

    onMount(async () => {
        const theme = storage.getTheme();
        document.documentElement.classList.toggle('light', theme === 'light');

        document.title = APP_NAME;

        await cryptoReady();
        cryptoLoaded = true;

        // Single /api/info fetch per page load — App owns it, Home reads it
        // through a prop. Falls back to defaults when the endpoint is down so
        // the page still renders.
        try {
            const res = await fetch('/api/info');
            if (res.ok) {
                serverInfo = await res.json();
                if (serverInfo.app_name) appName = serverInfo.app_name;
            }
        } catch { /* ignore — keep defaults */ }

        await applyLocation();

        // Pasting a link to a different room into the address bar of an
        // already-open chat tab only changes the URL fragment, not the path,
        // so the browser does NOT reload — the SPA must react to it. We
        // listen for hashchange and re-route; the {#key} block on Chat in
        // the markup forces a fresh component instance per room.
        window.addEventListener('hashchange', applyLocation);
    });

    onDestroy(() => {
        window.removeEventListener('hashchange', applyLocation);
    });

    async function applyLocation() {
        const { roomId, seed } = parseLocation();
        const goneSet = await checkRoomLiveness(roomId);

        if (!roomId) {
            route = { name: 'home' };
            return;
        }
        if (!seed) {
            // Room id without a seed is unusable — bounce home with hint.
            goHome();
            route = { name: 'home', error: 'invalid_key' };
            return;
        }
        route = {
            name: 'chat',
            roomId,
            seed,
            knownDeleted: goneSet.has(roomId)
        };
    }

    // Check known rooms + the URL room (if any) in one shot. Returns a Set
    // of ids reported as gone so the caller can disambiguate routing.
    async function checkRoomLiveness(urlRoomId) {
        const idSet = new Set(storage.knownRoomIds());
        if (urlRoomId) idSet.add(urlRoomId);
        if (idSet.size === 0) return new Set();
        try {
            const res = await api.isAlive([...idSet]);
            const gone = new Set(res.gone || []);
            for (const id of gone) storage.forgetRoom(id);
            return gone;
        } catch {
            // Offline — assume nothing was deleted; let WS surface the error.
            return new Set();
        }
    }

    function gotoChat(detail) {
        route = { name: 'chat', ...detail };
    }
    function gotoHome() {
        goHome();
        route = { name: 'home' };
    }
</script>

<div class="app-shell" class:chat-mode={route.name === 'chat'}>
    {#if !cryptoLoaded}
        <div class="loading dim">Loading…</div>
    {:else if route.name === 'home'}
        <Home initialError={route.error} appName={appName} info={serverInfo} onCreated={gotoChat} />
    {:else if route.name === 'chat'}
        {#key route.roomId}
            <Chat
                roomId={route.roomId}
                seed={route.seed}
                freshlyCreated={!!route.freshlyCreated}
                knownDeleted={!!route.knownDeleted}
                onLeave={gotoHome}
            />
        {/key}
    {/if}
    <Footer appName={appName} />
</div>

<style>
    .loading {
        flex: 1;
        display: flex;
        align-items: center;
        justify-content: center;
    }
</style>
