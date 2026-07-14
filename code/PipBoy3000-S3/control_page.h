// PipBoy control page (HTML/CSS/JS) — kept in a .h so the Arduino .ino
// preprocessor never touches the raw string literal (its prototype
// generator mangles JS 'function' declarations inside .ino files).
#pragma once

const char CONTROL_PAGE[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>PIPBOY 3000</title>
<style>
:root{--g:#30ff50;--dk:#0a3c12;--bg:#0b1a0e;--bd:#1a5c2a;--dim:#186828;
--glow:rgba(48,255,80,.15)}
*{box-sizing:border-box}
body{background:var(--bg);color:var(--g);font-family:'Share Tech Mono','Courier New',monospace;
margin:0;padding:16px;min-height:100vh;
background-image:repeating-linear-gradient(0deg,transparent,transparent 2px,rgba(0,0,0,.15) 2px,rgba(0,0,0,.15) 4px)}
body::after{content:'';position:fixed;inset:0;pointer-events:none;
background:repeating-linear-gradient(0deg,rgba(0,0,0,0) 0 1px,rgba(0,0,0,.1) 1px 2px);z-index:9999}
.wrap{max-width:480px;margin:0 auto;padding:16px;border:1px solid var(--bd);
background:rgba(10,60,18,.3);box-shadow:0 0 30px var(--glow),inset 0 0 30px rgba(0,0,0,.5)}
h2{margin:0 0 2px;font-weight:normal;letter-spacing:2px;text-transform:uppercase;text-align:center;
text-shadow:0 0 10px var(--g)}
.sub{color:var(--dim);font-size:11px;letter-spacing:3px;text-align:center}
fieldset{border:1px solid var(--dim);margin:14px 0;padding:10px}
legend{color:var(--g);text-transform:uppercase;letter-spacing:2px;font-size:13px;padding:0 6px}
label{color:var(--dim);font-size:12px;text-transform:uppercase;display:block;margin-top:8px}
input[type=text],input[type=password],select{width:100%;background:var(--dk);color:var(--g);
border:1px solid var(--dim);font-family:inherit;font-size:15px;padding:8px;border-radius:0}
input[type=range]{width:100%;accent-color:#30ff50}
input[type=file]{color:var(--dim);width:100%}
button{width:100%;background:transparent;color:var(--g);border:1px solid var(--g);
font-family:inherit;font-size:14px;padding:10px;margin:6px 0;cursor:pointer;
text-transform:uppercase;letter-spacing:2px}
button:hover{background:var(--g);color:var(--bg)}
.row{display:flex;gap:8px}.row>*{flex:1}
#st{font-size:13px;line-height:1.7}
.ok{color:var(--g)}.warn{color:#fda000}.err{color:#ff3030}
</style></head><body><div class="wrap">
<h2>// PIPBOY 3000 //</h2>
<div class="sub">ROBCO INDUSTRIES UNIFIED OS</div>
<div class="sub">REMOTE ACCESS MODULE</div>

<fieldset><legend>Status</legend><div id="st">CONNECTING...</div></fieldset>

<fieldset><legend>Clock</legend>
<button onclick="syncTime()">Sync clock from this phone</button>
</fieldset>

<fieldset><legend>Audio</legend>
<label>Volume: <span id="vv"></span>/30</label>
<input type="range" id="vol" min="0" max="30" onchange="setVol(this.value)">
<div class="row">
<button onclick="api('/api/play?n=5',{method:'POST'})">RADIO 1</button>
<button onclick="api('/api/play?n=6',{method:'POST'})">RADIO 2</button>
<button onclick="api('/api/play?n=7',{method:'POST'})">RADIO 3</button>
</div>
<div class="row">
<button onclick="api('/api/play?n=8',{method:'POST'})">RADIO 4</button>
<button onclick="api('/api/play?n=9',{method:'POST'})">RADIO 5</button>
<button onclick="api('/api/stop',{method:'POST'})">STOP</button>
</div>
</fieldset>

<fieldset><legend>Lights</legend>
<label>Mode</label>
<select id="lm" onchange="setLed()">
<option value="0">OFF</option><option value="1">STEADY</option>
<option value="2">BREATH</option><option value="3">FLICKER</option>
</select>
<label>Brightness: <span id="lbv"></span></label>
<input type="range" id="lb" min="0" max="255" onchange="setLed()">
</fieldset>

<fieldset><legend>Home WiFi</legend>
<label>SSID</label><input type="text" id="ssid">
<label>Password</label><input type="password" id="pass">
<button onclick="saveWifi()">Save &amp; connect</button>
</fieldset>

<fieldset><legend>Firmware</legend>
<form method="POST" action="/update" enctype="multipart/form-data">
<input type="file" name="fw" accept=".bin">
<button type="submit">Upload firmware</button>
</form>
</fieldset>

<div class="sub">VAULT-TEC (C) 2077</div>
</div>
<script>
function api(u,o){return fetch(u,o).then(r=>r.text())}
function syncTime(){api('/api/time?epoch='+Math.floor(Date.now()/1000),{method:'POST'}).then(refresh)}
function setVol(v){document.getElementById('vv').textContent=v;api('/api/volume?v='+v,{method:'POST'})}
function setLed(){var m=document.getElementById('lm').value,b=document.getElementById('lb').value;
document.getElementById('lbv').textContent=b;api('/api/led?mode='+m+'&bright='+b,{method:'POST'})}
function saveWifi(){api('/api/wifi',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
body:'ssid='+encodeURIComponent(document.getElementById('ssid').value)+
'&pass='+encodeURIComponent(document.getElementById('pass').value)}).then(()=>alert('Saved. Connecting...'))}
function pill(b,t){return '<span class="'+(b?'ok':'err')+'">'+t+'['+(b?'OK':'!!')+']</span> '}
function refresh(){fetch('/api/status').then(r=>r.json()).then(s=>{
document.getElementById('st').innerHTML=
pill(s.audio,'AUD')+pill(s.sensor,'SENS')+pill(s.rtc,'RTC')+pill(s.clock,'CLK')+pill(s.wifi,'NET')+'<br>'+
'TIME: '+s.time+'<br>'+
(s.batt>=0?'BATTERY: '+s.batt+'% ('+s.volt+'V)<br>':'')+
(s.temp!==null?'TEMP: '+s.temp+'&deg; &nbsp; HUM: '+s.hum+'%<br>':'')+
'STA IP: '+s.ip;
document.getElementById('vol').value=s.volume;document.getElementById('vv').textContent=s.volume;
document.getElementById('lm').value=s.ledmode;document.getElementById('lb').value=s.ledbright;
document.getElementById('lbv').textContent=s.ledbright;
}).catch(()=>{document.getElementById('st').textContent='LINK LOST - RETRYING...'})}
refresh();setInterval(refresh,5000);
</script></body></html>)rawliteral";
