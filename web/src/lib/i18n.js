import { writable, derived } from 'svelte/store';
import { storage } from './storage.js';
import { en } from '../locales/en.js';
import { ru } from '../locales/ru.js';
import { es } from '../locales/es.js';

const DICT = { en, ru, es };

// Walk navigator.languages in browser-preference order (the same order the
// browser advertises in the Accept-Language header) and pick the first tag
// whose primary subtag we ship a dictionary for. "es-419", "es-MX", "es" all
// match "es"; we ignore the region.
function detect() {
    const saved = storage.getLang();
    if (saved && DICT[saved]) return saved;
    const list = (navigator.languages && navigator.languages.length)
        ? navigator.languages
        : [navigator.language || 'en'];
    for (const tag of list) {
        if (!tag) continue;
        const code = tag.slice(0, 2).toLowerCase();
        if (DICT[code]) return code;
    }
    return 'en';
}

export const lang = writable(detect());

lang.subscribe(v => storage.setLang(v));

export const t = derived(lang, $lang => {
    return (key) => {
        const parts = key.split('.');
        let node = DICT[$lang] || DICT.en;
        for (const p of parts) {
            if (node && typeof node === 'object' && p in node) node = node[p];
            else return key;
        }
        return typeof node === 'string' ? node : key;
    };
});
