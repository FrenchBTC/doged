// Copyright (c) 2023-2026 The Dogecoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <api/dashboard_handler.h>
#include <rpc/protocol.h>

namespace api {

static const char *DASHBOARD_HTML = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Doged Node Dashboard</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
:root{--bg:#0c0e14;--card:#151820;--border:#1e2230;--text:#c8ccd4;--dim:#6b7280;
--accent:#f0b90b;--green:#34d399;--orange:#fbbf24;--red:#f87171;--font:system-ui,-apple-system,sans-serif}
body{background:var(--bg);color:var(--text);font-family:var(--font);font-size:14px;line-height:1.5;padding:16px 24px}
a{color:var(--accent);text-decoration:none}
a:hover{text-decoration:underline}
h1{font-size:22px;font-weight:700;color:#fff;margin-bottom:4px}
.subtitle{color:var(--dim);font-size:13px;margin-bottom:20px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:14px;margin-bottom:18px}
.card{background:var(--card);border:1px solid var(--border);border-radius:8px;padding:16px}
.card h2{font-size:13px;text-transform:uppercase;letter-spacing:.5px;color:var(--dim);margin-bottom:10px;font-weight:600}
.stat{display:flex;justify-content:space-between;padding:4px 0;border-bottom:1px solid var(--border)}
.stat:last-child{border-bottom:none}
.stat .label{color:var(--dim);font-size:13px}
.stat .value{font-weight:600;color:#fff;font-variant-numeric:tabular-nums;text-align:right;word-break:break-all;max-width:60%}
.badge{display:inline-block;padding:1px 8px;border-radius:10px;font-size:11px;font-weight:700}
.badge-green{background:#064e3b;color:var(--green)}
.badge-orange{background:#78350f;color:var(--orange)}
.badge-red{background:#7f1d1d;color:var(--red)}
table{width:100%;border-collapse:collapse}
th{text-align:left;color:var(--dim);font-size:12px;text-transform:uppercase;letter-spacing:.4px;
padding:6px 8px;border-bottom:1px solid var(--border);font-weight:600}
td{padding:6px 8px;border-bottom:1px solid var(--border);font-size:13px;font-variant-numeric:tabular-nums}
tr:hover td{background:#1a1e2a}
.mono{font-family:'SF Mono',SFMono-Regular,Consolas,monospace;font-size:12px}
.hash{max-width:160px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;display:inline-block}
.tabs{display:flex;gap:2px;margin-bottom:14px;background:var(--card);border-radius:8px;padding:3px;border:1px solid var(--border)}
.tab{padding:6px 14px;border-radius:6px;cursor:pointer;font-size:13px;font-weight:500;color:var(--dim);transition:.15s}
.tab:hover{color:var(--text)}
.tab.active{background:var(--accent);color:#000}
.panel{display:none}.panel.active{display:block}
.loader{color:var(--dim);padding:20px;text-align:center}
#last-update{color:var(--dim);font-size:12px;float:right;margin-top:6px}
</style>
</head>
<body>
<h1>&#x1F415; Doged Node Dashboard</h1>
<p class="subtitle">Built-in dashboard &mdash; data from <a href="/api/v1/">/api/v1/</a></p>
<span id="last-update"></span>

<div class="tabs" id="main-tabs">
<div class="tab active" data-tab="overview">Overview</div>
<div class="tab" data-tab="blocks">Blocks</div>
<div class="tab" data-tab="mempool">Mempool</div>
<div class="tab" data-tab="peers">Peers</div>
<div class="tab" data-tab="stratum">Stratum</div>
<div class="tab" data-tab="mergemine">Merge Mining</div>
</div>

<div id="overview" class="panel active">
<div class="grid">
<div class="card" id="chain-card"><h2>Chain</h2><div class="loader">Loading...</div></div>
<div class="card" id="network-card"><h2>Network</h2><div class="loader">Loading...</div></div>
<div class="card" id="mining-card"><h2>Mining</h2><div class="loader">Loading...</div></div>
</div>
</div>

<div id="blocks" class="panel">
<div class="card"><h2>Recent Blocks</h2><div id="blocks-table" class="loader">Loading...</div></div>
</div>

<div id="mempool" class="panel">
<div class="card"><h2>Mempool</h2><div id="mempool-content" class="loader">Loading...</div></div>
</div>

<div id="peers" class="panel">
<div class="card"><h2>Connected Peers</h2><div id="peers-table" class="loader">Loading...</div></div>
</div>

<div id="stratum" class="panel">
<div class="grid">
<div class="card"><h2>Stratum Server</h2><div id="st-info" class="loader">Loading...</div></div>
<div class="card" style="grid-column: 1/-1"><h2>Workers</h2><div id="st-workers" class="loader">Loading...</div></div>
</div>
</div>

<div id="mergemine" class="panel">
<div class="grid">
<div class="card"><h2>Multi-Chain Merge Mining</h2><div id="mm-info" class="loader">Loading...</div></div>
<div class="card" style="grid-column: 1/-1"><h2>External Chains</h2><div id="mm-chains" class="loader">Loading...</div></div>
</div>
</div>

<script>
const $ = s => document.querySelector(s);
const api = async p => { try { const r = await fetch('/api/v1/' + p); return r.ok ? r.json() : null; } catch(e) { return null; } };
const esc = s => String(s??'').replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
const num = n => Number(n||0).toLocaleString();
const ago = ts => { const s=Math.floor(Date.now()/1000)-ts; if(s<60) return s+'s ago'; if(s<3600) return Math.floor(s/60)+'m ago'; if(s<86400) return Math.floor(s/3600)+'h ago'; return Math.floor(s/86400)+'d ago'; };
const shortHash = h => h ? h.slice(0,8)+'\u2026'+h.slice(-6) : '\u2014';

function stat(label, value) { return '<div class="stat"><span class="label">'+esc(label)+'</span><span class="value">'+value+'</span></div>'; }

document.querySelectorAll('.tab').forEach(t => t.onclick = function() {
  document.querySelectorAll('.tab').forEach(x => x.classList.remove('active'));
  document.querySelectorAll('.panel').forEach(x => x.classList.remove('active'));
  this.classList.add('active');
  document.getElementById(this.dataset.tab).classList.add('active');
  loadTab(this.dataset.tab);
});

async function loadOverview() {
  const [chain, net, mining] = await Promise.all([api('chain'), api('network'), api('mining')]);
  if (chain) {
    const ibd = chain.initial_block_download;
    const syncBadge = ibd ? '<span class="badge badge-orange">Syncing</span>' : '<span class="badge badge-green">Synced</span>';
    $('#chain-card').innerHTML = '<h2>Chain</h2>' +
      stat('Status', syncBadge) +
      stat('Height', num(chain.height)) +
      stat('Difficulty', Number(chain.difficulty||0).toFixed(2)) +
      stat('Best Block', '<span class="mono hash" title="'+esc(chain.best_block_hash)+'">'+shortHash(chain.best_block_hash)+'</span>') +
      stat('Median Time', chain.median_time ? new Date(chain.median_time*1000).toUTCString() : '\u2014');
  }
  if (net) {
    $('#network-card').innerHTML = '<h2>Network</h2>' +
      stat('Connections', num(net.connections || net.peers)) +
      stat('In / Out', num(net.connections_in||0) + ' / ' + num(net.connections_out||0)) +
      stat('Protocol', esc(net.protocol_version)) +
      stat('User Agent', esc(net.subversion || ''));
  } else {
    $('#network-card').innerHTML = '<h2>Network</h2>' + stat('Status','<span class="badge badge-red">Unavailable</span>');
  }
  if (mining) {
    $('#mining-card').innerHTML = '<h2>Mining</h2>' +
      stat('Height', num(mining.height)) +
      stat('Difficulty', Number(mining.difficulty||0).toFixed(2)) +
      stat('Mempool', num(mining.mempool_size||0) + ' txs') +
      stat('Chain', esc(mining.chain || '\u2014'));
  } else {
    $('#mining-card').innerHTML = '<h2>Mining</h2>' + stat('Status','<span class="badge badge-red">Unavailable</span>');
  }
}

async function loadBlocks() {
  const d = await api('blocks?limit=15');
  if (!d || !d.data || !d.data.length) { $('#blocks-table').innerHTML = '<p style="color:var(--dim);padding:12px">No blocks yet</p>'; return; }
  let h = '<table><tr><th>Height</th><th>Hash</th><th>Txs</th><th>Size</th><th>Time</th></tr>';
  d.data.forEach(b => {
    h += '<tr><td>'+num(b.height)+'</td><td class="mono"><span class="hash" title="'+esc(b.hash)+'">'+shortHash(b.hash)+'</span></td>' +
      '<td>'+num(b.n_tx)+'</td><td>'+num(b.size)+' B</td><td>'+ago(b.time)+'</td></tr>';
  });
  h += '</table>';
  if (d.pagination) h += '<p style="color:var(--dim);font-size:12px;margin-top:8px">Total: '+num(d.pagination.total)+' blocks</p>';
  $('#blocks-table').innerHTML = h;
}

async function loadMempool() {
  const d = await api('mempool');
  if (!d) { $('#mempool-content').innerHTML = '<p style="color:var(--dim);padding:12px">Unavailable</p>'; return; }
  let h = stat('Size', num(d.size) + ' txs') + stat('Bytes', num(d.bytes) + ' B');
  if (d.transactions && d.transactions.length) {
    h += '<table style="margin-top:12px"><tr><th>TxID</th><th>Size</th><th>Fee</th></tr>';
    d.transactions.slice(0,20).forEach(tx => {
      const feeDoge = (tx.fee_sats||0) / 100000000;
      h += '<tr><td class="mono"><span class="hash" title="'+esc(tx.txid)+'">'+shortHash(tx.txid)+'</span></td>' +
        '<td>'+num(tx.size||0)+' B</td><td>'+feeDoge.toFixed(8)+' DOGE</td></tr>';
    });
    h += '</table>';
  } else {
    h += '<p style="color:var(--dim);font-style:italic;margin-top:10px">Mempool is empty</p>';
  }
  $('#mempool-content').innerHTML = h;
}

async function loadPeers() {
  const d = await api('network/peers');
  if (!d || !d.length) { $('#peers-table').innerHTML = '<p style="color:var(--dim);padding:12px">No peers connected</p>'; return; }
  let h = '<table><tr><th>Address</th><th>Agent</th><th>Height</th><th>Dir</th><th>Ping</th></tr>';
  d.forEach(p => {
    const dir = p.inbound ? 'In' : 'Out';
    const ping = p.ping_ms >= 0 ? p.ping_ms.toFixed(0)+'ms' : '\u2014';
    h += '<tr><td class="mono" style="font-size:11px">'+esc(p.addr)+'</td>' +
      '<td style="font-size:12px;max-width:200px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap">'+esc(p.subver||'')+'</td>' +
      '<td>'+num(p.startingheight||0)+'</td>' +
      '<td>'+dir+'</td><td>'+ping+'</td></tr>';
  });
  h += '</table>';
  $('#peers-table').innerHTML = h;
}

async function loadStratum() {
  const [info, workers] = await Promise.all([api('stratum'), api('stratum/workers')]);
  if (info) {
    const badge = info.enabled ?
      '<span style="color:var(--green)">Running</span>' :
      '<span style="color:var(--red)">Stopped</span>';
    const uptimeH = info.uptime ? Math.floor(info.uptime/3600)+'h '+Math.floor((info.uptime%3600)/60)+'m' : '\u2014';
    let h = stat('Status', badge);
    h += stat('Uptime', uptimeH);
    h += stat('Workers', num(info.activeWorkers||0));
    h += stat('Accepted', num(info.totalSharesAccepted||0));
    h += stat('Rejected', num(info.totalSharesRejected||0));
    h += stat('Blocks Found', '<span style="color:var(--green)">'+num(info.blocksFound||0)+'</span>');
    h += stat('Net Difficulty', num(Math.round(info.networkDifficulty||0)));
    $('#st-info').innerHTML = h;
  } else {
    $('#st-info').innerHTML = '<p style="color:var(--dim)">Stratum server not enabled. Start with -stratum flag.</p>';
  }
  if (workers && workers.length) {
    let h = '<table><tr><th>Name</th><th>Address</th><th>Algo</th><th>Diff</th><th>Accepted</th><th>Hashrate</th><th>State</th></tr>';
    workers.forEach(w => {
      const addr = (w.payoutAddress||'').length > 20 ?
        w.payoutAddress.substr(0,10)+'...'+w.payoutAddress.substr(-8) : (w.payoutAddress||'\u2014');
      h += '<tr><td>'+esc(w.name)+'</td>' +
        '<td class="mono"><span class="hash">'+esc(addr)+'</span></td>' +
        '<td>'+esc(w.algorithm||'')+'</td>' +
        '<td>'+Number(w.difficulty||0).toFixed(4)+'</td>' +
        '<td>'+num(w.accepted||0)+'</td>' +
        '<td>'+Number(w.hashrate||0).toFixed(2)+'</td>' +
        '<td>'+esc(w.state||'')+'</td></tr>';
    });
    h += '</table>';
    $('#st-workers').innerHTML = h;
  } else {
    $('#st-workers').innerHTML = '<p style="color:var(--dim)">No workers connected.</p>';
  }
}

async function loadMergeMine() {
  const d = await api('mergemine');
  if (d) {
    let s = stat('Enabled', d.enabled ?
      '<span class="badge badge-green">Yes</span>' :
      '<span class="badge badge-red">No</span>');
    s += stat('External Chains', num(d.chainCount || 0));
    $('#mm-info').innerHTML = s;

    const chains = d.chains || [];
    if (chains.length > 0) {
      let h = '<table><tr><th>Chain</th><th>ID</th><th>Height</th><th>Aux Hash</th><th>Age</th></tr>';
      chains.forEach(c => {
        const age = c.fetchedAt ? Math.round(Date.now()/1000 - c.fetchedAt) + 's ago' : '-';
        h += '<tr><td>'+esc(c.name)+'</td>' +
          '<td>'+num(c.chainId)+'</td>' +
          '<td>'+num(c.height)+'</td>' +
          '<td class="mono"><span class="hash">'+esc((c.auxHash||'').slice(0,16))+'...</span></td>' +
          '<td>'+age+'</td></tr>';
      });
      h += '</table>';
      $('#mm-chains').innerHTML = h;
    } else {
      $('#mm-chains').innerHTML = '<p style="color:var(--dim)">No external chains configured. Use -mergemine flag.</p>';
    }
  } else {
    $('#mm-info').innerHTML = '<p style="color:var(--dim)">Merge mining data unavailable.</p>';
  }
}

function loadTab(tab) {
  switch(tab) {
    case 'overview': loadOverview(); break;
    case 'blocks': loadBlocks(); break;
    case 'mempool': loadMempool(); break;
    case 'peers': loadPeers(); break;
    case 'stratum': loadStratum(); break;
    case 'mergemine': loadMergeMine(); break;
  }
}

function tick() {
  const active = document.querySelector('.tab.active');
  loadTab(active ? active.dataset.tab : 'overview');
  $('#last-update').textContent = 'Updated ' + new Date().toLocaleTimeString();
}

tick();
setInterval(tick, 10000);
</script>
</body>
</html>)HTML";

bool HandleGetDashboard(HTTPRequest *req) {
    req->WriteHeader("Content-Type", "text/html; charset=utf-8");
    req->WriteHeader("Cache-Control", "no-store");
    req->WriteReply(HTTP_OK, DASHBOARD_HTML);
    return true;
}

} // namespace api
