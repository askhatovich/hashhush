<script>
    import { t } from '../lib/i18n.js';

    let { onConfirm, onCancel } = $props();

    let trackEl;
    let value = $state(0);   // 0..1
    let dragging = false;

    function start(e) {
        dragging = true;
        update(e);
    }
    function update(e) {
        if (!dragging || !trackEl) return;
        const rect = trackEl.getBoundingClientRect();
        const x = (e.touches ? e.touches[0].clientX : e.clientX) - rect.left;
        const ratio = Math.max(0, Math.min(1, x / rect.width));
        value = ratio;
        if (ratio >= 0.98) {
            dragging = false;
            value = 1;
            onConfirm && onConfirm();
        }
    }
    function end() {
        if (dragging && value < 0.98) value = 0;
        dragging = false;
    }
</script>

<svelte:window
    onmousemove={update}
    onmouseup={end}
    ontouchmove={update}
    ontouchend={end}
/>

<div class="backdrop" role="dialog" aria-modal="true" onclick={onCancel}>
    <div class="modal" onclick={(e) => e.stopPropagation()} role="document">
        <h2>{$t('delete_modal.title')}</h2>
        <p class="dim">{$t('delete_modal.body')}</p>

        <div class="track" bind:this={trackEl}>
            <div class="fill" style="width: {value * 100}%"></div>
            <div
                class="thumb"
                style="left: calc({value * 100}% - 18px)"
                onmousedown={start}
                ontouchstart={start}
                role="slider"
                tabindex="0"
                aria-valuemin="0"
                aria-valuemax="1"
                aria-valuenow={value}
            >→</div>
            {#if value < 0.1}
                <span class="hint dim">{$t('delete_modal.slide_to_confirm')}</span>
            {/if}
        </div>

        <div class="actions">
            <button class="ghost" onclick={onCancel}>{$t('delete_modal.cancel')}</button>
        </div>
    </div>
</div>

<style>
    .backdrop {
        position: fixed; inset: 0;
        background: rgba(0, 0, 0, 0.55);
        display: flex; align-items: center; justify-content: center;
        z-index: 1000;
        animation: fade-in 0.16s ease-out;
    }
    .modal {
        background: var(--bg-elev);
        border: 1px solid var(--border);
        border-radius: var(--radius);
        box-shadow: var(--shadow);
        padding: 22px;
        max-width: 420px;
        width: calc(100% - 32px);
    }
    h2 { margin: 0 0 8px; font-size: 18px; }
    p  { margin: 0 0 16px; font-size: 14px; }

    .track {
        position: relative;
        height: 44px;
        background: var(--bg-elev-2);
        border: 1px solid var(--border);
        border-radius: 22px;
        overflow: hidden;
        margin-bottom: 14px;
        user-select: none;
    }
    .fill {
        position: absolute; inset: 0;
        background: var(--danger);
        opacity: 0.3;
        transition: width 0.05s linear;
    }
    .thumb {
        position: absolute;
        top: 4px;
        width: 36px; height: 36px;
        border-radius: 18px;
        background: var(--danger);
        color: #fff;
        display: flex; align-items: center; justify-content: center;
        font-weight: bold;
        cursor: grab;
        transition: left 0.05s linear;
    }
    .thumb:active { cursor: grabbing; }
    .hint {
        position: absolute; inset: 0;
        display: flex; align-items: center; justify-content: center;
        pointer-events: none;
        font-size: 13px;
    }
    .actions { display: flex; justify-content: flex-end; }
</style>
