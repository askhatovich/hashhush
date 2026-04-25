import { writable, derived } from 'svelte/store';
import { storage } from './storage.js';
import { en } from '../locales/en.js';
import { ru } from '../locales/ru.js';

const DICT = { en, ru };

function detect() {
    const saved = storage.getLang();
    if (saved && DICT[saved]) return saved;
    const browser = (navigator.language || 'en').slice(0, 2).toLowerCase();
    return DICT[browser] ? browser : 'en';
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
