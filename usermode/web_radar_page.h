#ifndef WEB_RADAR_PAGE_H
#define WEB_RADAR_PAGE_H

inline const char* WEB_RADAR_HTML = R"RADARPAGE(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>DBD Radar</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{background:#0a0a0e;color:#e0e0e5;font-family:'Segoe UI',system-ui,sans-serif;overflow:hidden;height:100vh}
#app{display:flex;height:100vh}
#sidebar{width:280px;min-width:280px;background:#12121a;border-right:1px solid #2a2a3a;display:flex;flex-direction:column;overflow-y:auto}
#radar-wrap{flex:1;position:relative;overflow:hidden}
canvas{display:block;width:100%;height:100%;background:#0d0d14}
.section{padding:10px 14px;border-bottom:1px solid #1e1e2e}
.section-title{font-size:11px;text-transform:uppercase;letter-spacing:1.5px;color:#6a6a8a;margin-bottom:8px;font-weight:600}
.status{font-size:12px;padding:6px 10px;display:flex;align-items:center;gap:8px}
.dot{width:8px;height:8px;border-radius:50%;display:inline-block}
.dot-ok{background:#2ecc71;box-shadow:0 0 6px #2ecc7188}
.dot-off{background:#e74c3c;box-shadow:0 0 6px #e74c3c88}
select,input[type=range]{width:100%;background:#1a1a28;border:1px solid #2a2a3a;color:#ccc;padding:4px 8px;border-radius:4px;font-size:13px;margin-top:4px}
select:focus{outline:1px solid #5a5a9a}
label.toggle{display:flex;align-items:center;gap:8px;font-size:13px;padding:3px 0;cursor:pointer;user-select:none}
label.toggle input{accent-color:#2ecc71}
.player-card{background:#16162a;border-radius:6px;padding:8px 10px;margin-bottom:6px;border-left:3px solid #2ecc71}
.player-card.killer{border-left-color:#e74c3c}
.player-card .pname{font-size:13px;font-weight:600}
.player-card .pchar{font-size:11px;color:#8888aa;margin-top:2px}
.player-card .pperks{font-size:10px;color:#6a6a8a;margin-top:3px;word-break:break-all}
.player-card .pmeta{font-size:10px;color:#5a5a7a;margin-top:2px}
.zoom-row{display:flex;align-items:center;gap:8px;font-size:12px}
.zoom-row span{min-width:32px;text-align:right;color:#8888aa}
#conn-status{font-size:11px;padding:8px 14px;background:#0e0e16;color:#666;text-align:center}
</style>
</head>
<body>
<div id="app">
<div id="sidebar">
    <div class="status">
        <span class="dot" id="dot"></span>
        <span id="status-text">Connecting...</span>
    </div>
    <div class="section">
        <div class="section-title">Follow Player</div>
        <select id="follow-select"><option value="-1">Auto (Local)</option></select>
    </div>
    <div class="section">
        <div class="section-title">Zoom</div>
        <div class="zoom-row">
            <input type="range" id="zoom" min="0.2" max="5" step="0.1" value="1">
            <span id="zoom-val">1.0x</span>
        </div>
        <button id="reset-btn" style="margin-top:6px;width:100%;padding:5px;background:#1e1e30;border:1px solid #3a3a5a;color:#8888cc;border-radius:4px;cursor:pointer;font-size:12px">Reset View</button>
    </div>
    <div class="section">
        <div class="section-title">Show Objects</div>
        <label class="toggle"><input type="checkbox" data-obj="Generator" checked>Generators</label>
        <label class="toggle"><input type="checkbox" data-obj="Totem" checked>Totems</label>
        <label class="toggle" style="padding-left:16px"><input type="checkbox" id="hide-dull">Hide Dull</label>
        <label class="toggle"><input type="checkbox" data-obj="Pallet" checked>Pallets</label>
        <label class="toggle"><input type="checkbox" data-obj="Hook" checked>Hooks</label>
        <label class="toggle"><input type="checkbox" data-obj="Hatch" checked>Hatch</label>
        <label class="toggle"><input type="checkbox" data-obj="Chest" checked>Chests</label>
        <label class="toggle"><input type="checkbox" data-obj="Exit Gate" checked>Exit Gates</label>
        <label class="toggle"><input type="checkbox" data-obj="Window">Windows</label>
        <label class="toggle"><input type="checkbox" data-obj="Trap" checked>Traps</label>
        <label class="toggle"><input type="checkbox" data-obj="Breakable">Breakable Walls</label>
        <label class="toggle"><input type="checkbox" data-obj="Event" checked>Event</label>
    </div>
    <div class="section">
        <div class="section-title">Players</div>
        <div id="players-list"></div>
    </div>
    <div id="conn-status"></div>
</div>
<div id="radar-wrap">
    <canvas id="radar"></canvas>
</div>
</div>
<script>
const canvas = document.getElementById('radar');
const ctx = canvas.getContext('2d');
const followSel = document.getElementById('follow-select');
const zoomSlider = document.getElementById('zoom');
const zoomVal = document.getElementById('zoom-val');
const dot = document.getElementById('dot');
const statusText = document.getElementById('status-text');
const playersList = document.getElementById('players-list');
const connStatus = document.getElementById('conn-status');

let state = null;
let zoom = 1.0;
let objFilters = {};
let lastUpdate = 0;
let panX = 0, panY = 0;
let isDragging = false;
let dragStartX = 0, dragStartY = 0;
let panStartX = 0, panStartY = 0;
let smoothPos = {};

document.querySelectorAll('[data-obj]').forEach(cb => {
    objFilters[cb.dataset.obj] = cb.checked;
    cb.addEventListener('change', () => { objFilters[cb.dataset.obj] = cb.checked; });
});

let hideDull = false;
document.getElementById('hide-dull').addEventListener('change', (e) => { hideDull = e.target.checked; });

function syncZoom(v) {
    zoom = Math.max(0.2, Math.min(5, v));
    zoomSlider.value = zoom;
    zoomVal.textContent = zoom.toFixed(1) + 'x';
}

zoomSlider.addEventListener('input', () => syncZoom(parseFloat(zoomSlider.value)));

canvas.addEventListener('wheel', (e) => {
    e.preventDefault();
    const delta = e.deltaY > 0 ? -0.15 : 0.15;
    syncZoom(zoom + delta * zoom);
}, {passive: false});

canvas.addEventListener('mousedown', (e) => {
    if (e.button === 0) {
        isDragging = true;
        dragStartX = e.clientX;
        dragStartY = e.clientY;
        panStartX = panX;
        panStartY = panY;
        canvas.style.cursor = 'grabbing';
    }
});
window.addEventListener('mousemove', (e) => {
    if (!isDragging) return;
    const scale = zoom * Math.min(canvas.clientWidth, canvas.clientHeight) / 20000;
    panX = panStartX - (e.clientX - dragStartX) / scale;
    panY = panStartY - (e.clientY - dragStartY) / scale;
});
window.addEventListener('mouseup', () => {
    isDragging = false;
    canvas.style.cursor = 'default';
});

document.getElementById('reset-btn').addEventListener('click', () => {
    panX = 0; panY = 0;
    syncZoom(1.0);
});

function connectSSE() {
    const es = new EventSource('/events');
    es.onopen = () => {
        dot.className = 'dot dot-ok';
        statusText.textContent = 'Connected';
    };
    es.onmessage = (e) => {
        try {
            state = JSON.parse(e.data);
            lastUpdate = Date.now();
            if (state.players) {
                state.players.forEach((p, i) => {
                    const key = p.name || ('p' + i);
                    if (!smoothPos[key]) smoothPos[key] = {x: p.x||0, y: p.y||0};
                    smoothPos[key].tx = p.x || 0;
                    smoothPos[key].ty = p.y || 0;
                });
            }
            updatePlayersList();
            updateFollowSelect();
        } catch(ex) {}
    };
    es.onerror = () => {
        dot.className = 'dot dot-off';
        statusText.textContent = 'Disconnected';
        es.close();
        setTimeout(connectSSE, 2000);
    };
}

let lastFollowFingerprint = '';
let lastPlayersFingerprint = '';

function updateFollowSelect() {
    if (!state || !state.players) return;
    const fp = state.players.map(p => (p.name||'') + (p.is_local?'L':'')).join('|');
    if (fp === lastFollowFingerprint) return;
    lastFollowFingerprint = fp;
    const cur = followSel.value;
    const opts = ['<option value="-1">Auto (Local)</option>'];
    state.players.forEach((p, i) => {
        const label = (p.name || p.character || 'Player ' + i) + (p.is_local ? ' (You)' : '');
        opts.push('<option value="' + i + '">' + esc(label) + '</option>');
    });
    followSel.innerHTML = opts.join('');
    followSel.value = cur;
}

function updatePlayersList() {
    if (!state || !state.players) { playersList.innerHTML = ''; lastPlayersFingerprint = ''; return; }
    const fp = state.players.map(p =>
        (p.name||'') + (p.character||'') + (p.role||'') + p.prestige + p.level + p.hp +
        (p.perks||[]).join(',')
    ).join('|');
    if (fp === lastPlayersFingerprint) return;
    lastPlayersFingerprint = fp;
    let html = '';
    state.players.forEach(p => {
        const isK = p.role === 'killer';
        const perks = (p.perks||[]).filter(x=>x).join(', ');
        html += '<div class="player-card' + (isK ? ' killer' : '') + '">';
        html += '<div class="pname">' + esc(p.name||'?') + '</div>';
        if (p.character) html += '<div class="pchar">' + esc(p.character) + '</div>';
        if (perks) html += '<div class="pperks">' + esc(perks) + '</div>';
        const meta = [];
        if (p.prestige > 0) meta.push('P' + p.prestige);
        if (p.level >= 0) meta.push('Lv' + p.level);
        if (p.hp >= 0 && !isK) meta.push('HP:' + p.hp);
        if (meta.length) html += '<div class="pmeta">' + meta.join(' | ') + '</div>';
        html += '</div>';
    });
    playersList.innerHTML = html;
}

function esc(s) {
    const d = document.createElement('div');
    d.textContent = s;
    return d.innerHTML;
}

function getSmoothPos(p, i) {
    const key = p.name || ('p' + i);
    const sp = smoothPos[key];
    if (!sp) return {x: p.x||0, y: p.y||0};
    return {x: sp.x, y: sp.y};
}

function getFollowPos() {
    if (!state || !state.players || !state.players.length) return {x:0, y:0};
    let idx = parseInt(followSel.value);
    if (idx < 0) {
        idx = state.players.findIndex(p => p.is_local);
        if (idx < 0) idx = 0;
    }
    if (idx >= state.players.length) idx = 0;
    return getSmoothPos(state.players[idx], idx);
}

const OBJ_COLORS = {
    'Generator': '#ffd700', 'Totem': '#ff69b4', 'Pallet': '#00ced1',
    'Hook': '#ff4500', 'Hatch': '#9400d3', 'Chest': '#daa520',
    'Window': '#87ceeb', 'Trap': '#dc143c', 'Exit Gate': '#32cd32',
    'Breakable': '#d2691e', 'Locker': '#808080', 'Event': '#ff32dc'
};

const OBJ_SYMBOLS = {
    'Generator': '\u26A1', 'Totem': '\u2726', 'Pallet': '\u2588',
    'Hook': '\u2620', 'Hatch': '\u25C6', 'Chest': '\u25A0',
    'Window': '\u25AF', 'Trap': '\u25B2', 'Exit Gate': '\u25CE',
    'Breakable': '\u2502', 'Locker': '\u25A3', 'Event': '\u2605'
};

function render() {
    const LERP = 0.15;
    for (const key in smoothPos) {
        const s = smoothPos[key];
        s.x += (s.tx - s.x) * LERP;
        s.y += (s.ty - s.y) * LERP;
    }

    const dpr = window.devicePixelRatio || 1;
    const w = canvas.clientWidth, h = canvas.clientHeight;
    const cw = Math.round(w * dpr), ch = Math.round(h * dpr);
    if (canvas.width !== cw || canvas.height !== ch) {
        canvas.width = cw;
        canvas.height = ch;
    }
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

    ctx.fillStyle = '#0d0d14';
    ctx.fillRect(0, 0, w, h);

    if (!state || !state.valid) {
        ctx.fillStyle = '#444';
        ctx.font = '16px sans-serif';
        ctx.textAlign = 'center';
        ctx.fillText('Waiting for game data...', w/2, h/2);
        requestAnimationFrame(render);
        return;
    }

    const age = Date.now() - lastUpdate;
    if (age > 5000) {
        ctx.fillStyle = '#663';
        ctx.font = '14px sans-serif';
        ctx.textAlign = 'center';
        ctx.fillText('Data stale (' + (age/1000).toFixed(0) + 's ago)', w/2, 20);
    }

    const follow = getFollowPos();
    const center = {x: follow.x + panX, y: follow.y + panY};
    const scale = zoom * Math.min(w, h) / 20000;
    const cx = w / 2, cy = h / 2;

    function toScreen(wx, wy) {
        return {
            x: cx + (wx - center.x) * scale,
            y: cy + (wy - center.y) * scale
        };
    }

    // Grid
    ctx.strokeStyle = '#1a1a2a';
    ctx.lineWidth = 0.5;
    const gridSize = 1000;
    const gridRange = 30000;
    for (let gx = -gridRange; gx <= gridRange; gx += gridSize) {
        const s1 = toScreen(gx, -gridRange), s2 = toScreen(gx, gridRange);
        ctx.beginPath(); ctx.moveTo(s1.x, s1.y); ctx.lineTo(s2.x, s2.y); ctx.stroke();
        const s3 = toScreen(-gridRange, gx), s4 = toScreen(gridRange, gx);
        ctx.beginPath(); ctx.moveTo(s3.x, s3.y); ctx.lineTo(s4.x, s4.y); ctx.stroke();
    }

    // Objects
    if (state.objects) {
        state.objects.forEach(o => {
            if (!objFilters[o.type]) return;
            if (o.type === 'Totem' && hideDull && o.state !== 2 && o.state !== 3) return;
            const sp = toScreen(o.x, o.y);
            if (sp.x < -20 || sp.x > w+20 || sp.y < -20 || sp.y > h+20) return;

            const col = OBJ_COLORS[o.type] || '#888';
            let drawCol = col;
            ctx.font = '14px sans-serif';
            ctx.textAlign = 'center';

            if (o.type === 'Totem' && o.state === 2) {
                drawCol = '#ff3333';
            } else if (o.type === 'Totem' && o.state === 3) {
                drawCol = '#66b3ff';
            } else if (o.type === 'Totem') {
                drawCol = '#ff69b466';
            }

            ctx.fillStyle = drawCol;
            ctx.fillText(OBJ_SYMBOLS[o.type] || '?', sp.x, sp.y + 5);

            ctx.font = '9px sans-serif';
            ctx.fillStyle = drawCol + (drawCol.length <= 7 ? 'aa' : '');
            let label = o.type;
            if (o.type === 'Generator' && o.progress !== undefined) {
                label = 'Gen ' + o.progress.toFixed(0) + '%';
                if (o.progress >= 99.5) { label = 'Gen DONE'; ctx.fillStyle = '#2f2'; }
            } else if (o.type === 'Totem') {
                if (o.state === 2) { label = 'HEX'; ctx.fillStyle = '#ff3333'; }
                else if (o.state === 3) { label = 'Boon'; ctx.fillStyle = '#66b3ff'; }
                else { label = 'Dull'; }
            }
            ctx.fillText(label, sp.x, sp.y + 18);
        });
    }

    // Players
    if (state.players) {
        state.players.forEach((p, i) => {
            const pos = getSmoothPos(p, i);
            if (!pos.x && !pos.y) return;
            const sp = toScreen(pos.x, pos.y);
            const isK = p.role === 'killer';
            const isLocal = p.is_local;

            const r = isK ? 8 : 6;
            ctx.beginPath();
            ctx.arc(sp.x, sp.y, r, 0, Math.PI * 2);
            if (isLocal) {
                ctx.fillStyle = '#3af';
                ctx.fill();
                ctx.strokeStyle = '#fff';
                ctx.lineWidth = 2;
                ctx.stroke();
            } else if (isK) {
                ctx.fillStyle = '#e33';
                ctx.fill();
                ctx.strokeStyle = '#f66';
                ctx.lineWidth = 1.5;
                ctx.stroke();
            } else {
                const hpCol = p.hp >= 2 ? '#2d2' : p.hp === 1 ? '#dc2' : '#d44';
                ctx.fillStyle = hpCol;
                ctx.fill();
            }

            ctx.font = '11px sans-serif';
            ctx.textAlign = 'center';
            ctx.fillStyle = isLocal ? '#3af' : isK ? '#f66' : '#ccc';
            const pLabel = p.name || p.character || '?';
            ctx.fillText(pLabel, sp.x, sp.y - r - 4);
        });
    }

    // Compass
    ctx.fillStyle = '#555';
    ctx.font = '12px sans-serif';
    ctx.textAlign = 'center';
    ctx.fillText('N', cx, 16);
    ctx.fillText('S', cx, h - 6);
    ctx.textAlign = 'left';
    ctx.fillText('W', 6, cy + 4);
    ctx.textAlign = 'right';
    ctx.fillText('E', w - 6, cy + 4);

    requestAnimationFrame(render);
}

connectSSE();
requestAnimationFrame(render);

connStatus.textContent = 'Port: ' + location.port;
</script>
</body>
</html>
)RADARPAGE";

#endif
