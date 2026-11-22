#pragma once
// Bedienoberflaeche, wird vom ESP32 unter http://192.168.4.1 ausgeliefert
const char WEB_PAGE[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Bremsdreirad</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--tx:#1d2330;--mu:#6b7280;--ac:#2563eb;--ok:#16a34a;--wa:#d97706;--er:#dc2626}
*{box-sizing:border-box}body{margin:0;font:15px system-ui,sans-serif;background:var(--bg);color:var(--tx)}
header{padding:12px 16px;background:#1d2330;color:#fff;display:flex;justify-content:space-between;align-items:center}
header b{font-size:17px}#mode{padding:3px 10px;border-radius:99px;background:#374151;font-size:13px}
main{max-width:760px;margin:auto;padding:12px;display:grid;gap:12px}
.card{background:var(--card);border-radius:12px;padding:14px;box-shadow:0 1px 3px #0001}
h2{margin:0 0 10px;font-size:14px;text-transform:uppercase;letter-spacing:.04em;color:var(--mu)}
.big{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;text-align:center}
.big div{background:var(--bg);border-radius:10px;padding:8px}.big span{display:block;font-size:26px;font-weight:700}
.big small{color:var(--mu)}
.row{display:flex;flex-wrap:wrap;gap:8px;margin:6px 0}
button{flex:1;min-width:120px;padding:12px;border:0;border-radius:10px;background:var(--ac);color:#fff;font-size:15px;font-weight:600}
button.sec{background:#e5e7eb;color:var(--tx);min-width:60px}button.ok{background:var(--ok)}button.wa{background:var(--wa)}
button.stop{background:var(--er);font-size:18px;padding:16px}
label{display:flex;justify-content:space-between;align-items:center;gap:8px;margin:6px 0}
input{width:110px;padding:8px;border:1px solid #d1d5db;border-radius:8px;font-size:15px;text-align:right}
.dot{display:inline-block;width:11px;height:11px;border-radius:50%;background:#d1d5db;margin-right:6px;vertical-align:-1px}
.dot.on{background:var(--ok)}#msg{white-space:pre-wrap;font:13px ui-monospace,monospace;color:var(--mu);min-height:1em}
canvas{width:100%;height:200px}table{width:100%;border-collapse:collapse;font-size:13px}
td,th{padding:4px 6px;text-align:right;border-bottom:1px solid #eee}tr.bad td{color:var(--er)}
.kv{display:grid;grid-template-columns:auto 1fr;gap:4px 12px;font-size:14px}.kv span:nth-child(odd){color:var(--mu)}
a{color:var(--ac)}
</style></head><body>
<header><b>Bremsdreirad</b><span id="mode">…</span></header>
<main>
<div class="card"><h2>Live</h2>
 <div class="big">
  <div><span id="hz">–</span><small>Drehzahl gemessen (Hz)</small></div>
  <div><span id="soll">–</span><small>Soll (Hz)</small></div>
  <div><span id="pulses">–</span><small>Hall-Impulse</small></div>
 </div>
 <div class="row" style="margin-top:10px">
  <div style="flex:1"><span class="dot" id="hExz"></span>Hallsensor Exzenter</div>
  <div style="flex:1"><span class="dot" id="hRamp"></span>Hallsensor Rampe</div>
 </div>
</div>

<div class="card"><h2>Steuerung</h2>
 <div class="row"><button class="stop" onclick="cmd('stop')">STOP</button></div>
 <div class="row">
  <button class="ok" onclick="cmd('open')">Bremse öffnen</button>
  <button onclick="cmd('home')">Referenzfahrt</button>
 </div>
 <div class="row">
  <button class="wa" onclick="cmd('run')">Testlauf</button>
  <button class="wa" onclick="cmd('arm')">Scharf (Rampe)</button>
 </div>
 <div class="row">
  <button class="sec" onclick="cmd('j -50')">−50</button><button class="sec" onclick="cmd('j -10')">−10</button>
  <button class="sec" onclick="cmd('j 10')">+10</button><button class="sec" onclick="cmd('j 50')">+50</button>
 </div>
 <div class="row"><button class="sec" onclick="cmd('cal')">Aktuelle Stellung als „offen“ speichern</button></div>
 <div class="row">
  <button class="sec" onclick="cmd('off')">Motor stromlos</button><button class="sec" onclick="cmd('on')">Motor bestromt</button>
 </div>
 <div id="msg"></div>
</div>

<div class="card"><h2>Einstellungen</h2>
 <label>Bremsfrequenz (Hz)<input id="f" type="number" step="0.5" min="0.5" max="25"></label>
 <label>Hochlauf (Hz/s)<input id="a" type="number" step="10" min="10" max="2000"></label>
 <label>Laufzeit (ms)<input id="t" type="number" step="100" min="100" max="60000"></label>
 <label>Motorstrom (mA)<input id="i" type="number" step="50" min="200" max="1400"></label>
 <div class="row"><button onclick="save()">Übernehmen</button></div>
</div>

<div class="card"><h2>Letzter Bremslauf</h2>
 <canvas id="ch" width="700" height="200"></canvas>
 <div class="kv" id="sum" style="margin:8px 0"></div>
 <div class="row"><button class="sec" onclick="loadLog()">Aktualisieren</button>
  <a href="/api/log.csv" download="bremslauf.csv" style="flex:1"><button class="sec" style="width:100%">CSV herunterladen</button></a></div>
 <div style="max-height:260px;overflow:auto"><table id="tb"></table></div>
</div>

<div class="card"><h2>Treiber</h2><div class="kv" id="drv"></div></div>
</main>
<script>
const $=id=>document.getElementById(id);let editing=false,lastLogId=-1;
document.querySelectorAll('input').forEach(e=>{e.onfocus=()=>editing=true;e.onblur=()=>editing=false});
async function cmd(c){const r=await fetch('/api/cmd?c='+encodeURIComponent(c));$('msg').textContent=await r.text();poll();}
async function save(){for(const k of['f','a','t','i'])await fetch('/api/cmd?c='+encodeURIComponent(k+' '+$(k).value));
 $('msg').textContent='Gespeichert.';editing=false;poll();}
async function poll(){try{const s=await(await fetch('/api/status')).json();
 $('mode').textContent=s.mode+(s.homed?'':' · keine Referenz');
 $('hz').textContent=s.hz.toFixed(2);$('soll').textContent=s.f.toFixed(1);$('pulses').textContent=s.pulses;
 $('hExz').className='dot'+(s.hallExz?' on':'');$('hRamp').className='dot'+(s.hallRamp?' on':'');
 if(!editing){$('f').value=s.f;$('a').value=s.a;$('t').value=s.t;$('i').value=s.i;}
 $('drv').innerHTML=`<span>UART</span><span>${s.uart?'OK':'FEHLER'}</span><span>Übertemperatur</span><span>${s.ot?'JA':'nein'}${s.otpw?' (Vorwarnung)':''}</span><span>Position</span><span>${s.pos}</span><span>Offset „offen“</span><span>${s.off}</span>`;
 if(s.logId!==lastLogId){lastLogId=s.logId;loadLog();}
}catch(e){$('mode').textContent='keine Verbindung';}}
async function loadLog(){const L=await(await fetch('/api/log')).json();const tb=$('tb');
 if(!L.rows.length){tb.innerHTML='<tr><td style="text-align:left">Noch kein Lauf.</td></tr>';$('sum').innerHTML='';draw(L);return;}
 let bad=0,h='<tr><th>#</th><th>t (ms)</th><th>Periode (ms)</th><th>f (Hz)</th><th>Schritte</th><th>Fehler</th></tr>';
 for(const r of L.rows){const b=Math.abs(r[5])>L.spr/20;bad+=b;h+=`<tr class="${b?'bad':''}"><td>${r[0]}</td><td>${r[1].toFixed(1)}</td><td>${r[2].toFixed(2)}</td><td>${r[3].toFixed(2)}</td><td>${r[4]}</td><td>${r[5]}</td></tr>`;}
 tb.innerHTML=h;const fs=L.rows.map(r=>r[3]),n=fs.length,st=fs.slice(Math.floor(n/2));
 const mean=st.reduce((a,b)=>a+b,0)/st.length;
 $('sum').innerHTML=`<span>Soll</span><span>${L.soll.toFixed(2)} Hz</span><span>Ist (2. Hälfte, Mittel)</span><span>${mean.toFixed(2)} Hz</span><span>Umdrehungen</span><span>${n}</span><span>Schrittverlust-Verdacht</span><span style="color:${bad?'var(--er)':'var(--ok)'}">${bad}</span><span>Trigger → Start</span><span>${(L.delayUs/1000).toFixed(2)} ms</span>`;
 draw(L);}
function draw(L){const c=$('ch'),g=c.getContext('2d'),W=c.width,H=c.height,p=30;g.clearRect(0,0,W,H);
 const rows=L.rows,ymax=Math.max(L.soll*1.2,...rows.map(r=>r[3]),1),tmax=Math.max(...rows.map(r=>r[1]),1);
 g.strokeStyle='#e5e7eb';g.fillStyle='#6b7280';g.font='11px sans-serif';
 for(let k=0;k<=4;k++){const y=H-p-(H-2*p)*k/4;g.beginPath();g.moveTo(p,y);g.lineTo(W-5,y);g.stroke();g.fillText((ymax*k/4).toFixed(0),2,y+4);}
 const X=t=>p+(W-p-5)*t/tmax,Y=v=>H-p-(H-2*p)*v/ymax;
 g.strokeStyle='#d97706';g.setLineDash([5,4]);g.beginPath();g.moveTo(p,Y(L.soll));g.lineTo(W-5,Y(L.soll));g.stroke();g.setLineDash([]);
 g.strokeStyle='#2563eb';g.lineWidth=2;g.beginPath();rows.forEach((r,k)=>k?g.lineTo(X(r[1]),Y(r[3])):g.moveTo(X(r[1]),Y(r[3])));g.stroke();g.lineWidth=1;
 g.fillText('Zeit (ms) →',W-70,H-8);}
setInterval(poll,400);poll();
</script></body></html>)HTML";
