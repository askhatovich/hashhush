import { mount } from 'svelte';
import App from './App.svelte';
import './app.css';

// The favicon is declared as an inline-SVG data URL in index.html so the
// browser sees it during HTML parsing and never falls back to /favicon.ico.
// To change the design, edit the data URL in index.html directly.

const app = mount(App, { target: document.getElementById('app') });
export default app;
