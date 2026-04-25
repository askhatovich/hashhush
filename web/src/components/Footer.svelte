<script>
    import { lang, t } from '../lib/i18n.js';
    import { storage } from '../lib/storage.js';
    import { VERSION, GIT_SHORT } from '../version.js';

    import { APP_NAME } from '../lib/brand.js';
    let theme = $state(storage.getTheme());

    function toggleTheme() {
        theme = theme === 'dark' ? 'light' : 'dark';
        storage.setTheme(theme);
        document.documentElement.classList.toggle('light', theme === 'light');
    }

    function setLang(l) {
        lang.set(l);
    }
</script>

<footer>
    <div class="left dim">
        <a href="https://github.com/askhatovich/hashhush" target="_blank" rel="noopener noreferrer">{APP_NAME}</a>
        <span class="mono">v{VERSION}</span>
        {#if VERSION !== 'dev'}<span class="mono">({GIT_SHORT})</span>{/if}
        <span>© 2026</span>
    </div>
    <div class="right">
        <button class="link" onclick={() => setLang('en')} class:active={$lang === 'en'}>EN</button>
        <button class="link" onclick={() => setLang('ru')} class:active={$lang === 'ru'}>RU</button>
        <button class="link" onclick={toggleTheme} title={theme}>
            {theme === 'dark' ? '☾' : '☀'}
        </button>
    </div>
</footer>

<style>
    footer {
        margin-top: 16px;
        padding-top: 14px;
        border-top: 1px solid var(--border);
        display: flex;
        justify-content: space-between;
        align-items: center;
        font-size: 12px;
    }
    .left { display: flex; gap: 8px; align-items: center; }
    .right { display: flex; gap: 6px; }
    .left a {
        color: var(--text);
        text-decoration: none;
        font-weight: 600;
    }
    .left a:hover { color: var(--accent); }
    button.link {
        background: transparent;
        color: var(--text-dim);
        border: 0;
        padding: 4px 8px;
        font-size: 12px;
        cursor: pointer;
        border-radius: 6px;
    }
    button.link:hover { background: var(--bg-elev-2); color: var(--text); }
    button.link.active { color: var(--accent); }
</style>
