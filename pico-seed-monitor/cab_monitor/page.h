// Monitor page served by the cab unit. Polls /data twice a second.
#pragma once

const char PAGE_HTML[] = R"HTML(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Seed Blockage</title>
<style>
:root{--bg:#111418;--card:#1c2128;--line:#2c333c;--text:#e8ecf1;--dim:#8a95a3;
--ok:#2fb35a;--low:#e0a400;--bad:#e5322d;--idle:#3d4651;--ign:#555}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:16px system-ui,Segoe UI,Arial,sans-serif}
header{display:flex;flex-wrap:wrap;gap:8px;align-items:center;padding:10px 12px;background:var(--card);border-bottom:1px solid var(--line)}
.pill{padding:6px 12px;border-radius:999px;background:var(--idle);font-weight:600;font-size:15px}
.pill.ok{background:var(--ok)}.pill.bad{background:var(--bad)}.pill.low{background:var(--low);color:#111}
.sp{flex:1}
button{font:inherit;font-weight:600;padding:8px 14px;border-radius:8px;border:1px solid var(--line);background:#2a313a;color:var(--text)}
button.on{background:var(--ok);border-color:var(--ok)}
#banner{display:none;padding:14px;font-size:22px;font-weight:800;text-align:center;background:var(--bad);color:#fff}
#banner.show{display:block;animation:fl 1s steps(2) infinite}
@keyframes fl{50%{background:#8f1512}}
.mapwrap{padding:12px 12px 0}
#map{width:100%;max-height:46vh;display:block;background:var(--card);border:1px solid var(--line);border-radius:10px}
#map .twr{cursor:pointer}
#map .bar{stroke:#5a6573;stroke-width:6;stroke-linecap:round}
#map text{fill:var(--text);font-weight:700;text-anchor:middle;dominant-baseline:central}
#map .sub{fill:var(--dim);font-size:11px;font-weight:500}
#map .front{fill:var(--dim);font-size:13px;text-anchor:start}
#map .hub{fill:#0c0f12;stroke:var(--line);stroke-width:2}
#map .hub.alarm{fill:#5a1210;stroke:var(--bad);stroke-width:3;animation:fl 1s steps(2) infinite}
#map .twr.dis{opacity:.3}
.tw.dis{opacity:.35}
#ranks{display:flex;gap:6px}
#ranks button:not(.on){background:#4a1f1f;border-color:#7a2a2a}
#map .hub.dead{stroke:var(--bad);stroke-dasharray:5 4}
.tw.flash{outline:3px solid #fff}
#grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(140px,1fr));gap:10px;padding:12px}
.tw{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:8px}
.tw.off{opacity:.55;border-color:var(--bad)}
.th{display:flex;justify-content:space-between;font-weight:700;margin-bottom:6px}
.th small{color:var(--dim);font-weight:500}
.bars{display:flex;gap:3px;height:150px;align-items:flex-end}
.run{flex:1;display:flex;flex-direction:column;align-items:center;height:100%;cursor:pointer}
.well{flex:1;width:100%;background:#0c0f12;border-radius:4px;display:flex;align-items:flex-end;overflow:hidden}
.fill{width:100%;background:var(--idle);transition:height .3s}
.s0 .fill{background:var(--ok)}.s1 .fill{background:var(--low)}
.s2 .well{background:var(--bad)}.s2 .fill{background:var(--bad)}
.s3 .fill{background:repeating-linear-gradient(45deg,#444 0 4px,#666 4px 8px)}
.lbl{font-size:12px;color:var(--dim);margin-top:2px}
.s2 .lbl{color:#fff;font-weight:800}
.msg{color:var(--dim);font-size:13px;text-align:center;padding:30px 0}
details{margin:0 12px 20px;background:var(--card);border:1px solid var(--line);border-radius:10px;padding:10px}
summary{font-weight:700;cursor:pointer}
.set{display:grid;grid-template-columns:repeat(auto-fill,minmax(230px,1fr));gap:10px;margin-top:10px}
.set label{display:flex;justify-content:space-between;align-items:center;gap:8px}
.set input{width:80px;font:inherit;padding:4px;background:#0c0f12;color:var(--text);border:1px solid var(--line);border-radius:6px}
.note{color:var(--dim);font-size:13px;margin-top:8px}
</style></head><body>
<header>
  <span id="st" class="pill">Connecting...</span>
  <span id="aog" class="pill">AgOpenGPS</span>
  <span id="spd" class="pill">-- km/h</span>
  <span class="sp"></span>
  <span id="ranks"></span>
  <button id="snd">Sound off</button>
  <button id="mute">Mute 2 min</button>
</header>
<div id="banner"></div>
<div class="mapwrap"><svg id="map" xmlns="http://www.w3.org/2000/svg"></svg></div>
<div id="grid"><div class="msg">Waiting for cab unit...</div></div>
<details><summary>Settings</summary>
<div class="set">
 <label>Towers on the bus <input id="towers" type="number" min="1" max="15"></label>
 <label>Blocked below % of tower avg <input id="block" type="number" min="1" max="95"></label>
 <label>Low (yellow) below % <input id="warn" type="number" min="1" max="99"></label>
 <label>Seconds low before alarm <input id="secs" type="number" min="1" max="60"></label>
 <label>Start delay after section on (s) <input id="delay" type="number" min="0" max="60"></label>
 <label>Tower min avg hits/s <input id="min" type="number" min="0" max="10000"></label>
 <label>Map layout <input id="layout" type="text" style="width:150px" placeholder="1,2,3,4/5,6,7,8"></label>
 <label>Monitor without AgOpenGPS <input id="always" type="checkbox" style="width:auto"></label>
</div>
<p><button id="save">Save settings</button></p>
<p class="note">Tap a run's bar to ignore it (striped) or bring it back, e.g. for a broken sensor.
Monitoring follows AgOpenGPS section 1: it starts after the delay when the section turns on and stops when it turns off.</p>
</details>
<script>
const $=id=>document.getElementById(id);
let cfgLoaded=false, ac=null, soundOn=false, alarm=false, mute=0, last=null;
function beepLoop(){ if(soundOn&&alarm&&!mute&&ac){const o=ac.createOscillator(),g=ac.createGain();
  o.frequency.value=2200;o.connect(g);g.connect(ac.destination);g.gain.value=.25;o.start();o.stop(ac.currentTime+.25);} }
setInterval(beepLoop,600);
$('snd').onclick=()=>{ if(!ac) ac=new (window.AudioContext||window.webkitAudioContext)();
  ac.resume(); soundOn=!soundOn; $('snd').textContent=soundOn?'Sound on':'Sound off'; $('snd').classList.toggle('on',soundOn); };
$('mute').onclick=()=>fetch('/mute?s='+(mute?0:120));
$('save').onclick=()=>{ const q=['towers','block','warn','secs','delay','min'].map(k=>k+'='+$(k).value);
  q.push('always='+($('always').checked?1:0)); q.push('layout='+encodeURIComponent($('layout').value)); fetch('/set?'+q.join('&')).then(()=>{cfgLoaded=false;}); };
function ignore(t,r){ if(confirm('Toggle ignore for tower '+t+' run '+r+'?')) fetch('/ignore?t='+t+'&r='+r); }
const SC=['#2fb35a','#e0a400','#e5322d','#555','#3d4651'];
function wedge(cx,cy,r0,r1,a0,a1){const p=(r,a)=>(cx+r*Math.cos(a)).toFixed(1)+' '+(cy+r*Math.sin(a)).toFixed(1);
  return 'M'+p(r1,a0)+'A'+r1+' '+r1+' 0 0 1 '+p(r1,a1)+'L'+p(r0,a1)+'A'+r0+' '+r0+' 0 0 0 '+p(r0,a0)+'Z';}
const RANK=['Front','Back'];
function rankName(i){return RANK[i]||('Row '+(i+1));}
function toggleRank(i,on){ if(confirm((on?'Turn OFF ':'Turn ON ')+rankName(i)+' rank monitoring?')) fetch('/rank?r='+i); }
function showTower(id){const el=$('tw'+id); if(!el) return; el.scrollIntoView({behavior:'smooth',block:'center'});
  el.classList.add('flash'); setTimeout(()=>el.classList.remove('flash'),1500);}
function drawMap(d){
  const rows=(d.cfg.layout||'1,2,3,4/5,6,7,8').split('/').map(r=>r.split(',').map(x=>parseInt(x)||0));
  const cols=Math.max(...rows.map(r=>r.length)), DX=130, DY=130, W=cols*DX+40, H=rows.length*DY+30;
  const byId={}; d.t.forEach(t=>byId[t.id]=t);
  let rb=''; rows.forEach((row,ri)=>{ const on=!((d.cfg.rowoff||0)&(1<<ri));
    rb+='<button class="'+(on?'on':'')+'" onclick="toggleRank('+ri+','+on+')">'+rankName(ri)+' '+(on?'ON':'OFF')+'</button>'; });
  $('ranks').innerHTML=rb;
  let g='<text class="front" x="12" y="14">FRONT</text>';
  rows.forEach((row,ri)=>{ const y=40+ri*DY+DY/2-10, x0=20+DX/2, x1=20+(row.length-0.5)*DX;
    g+='<line class="bar" style="opacity:'+(((d.cfg.rowoff||0)&(1<<ri))?.25:1)+'" x1="'+(x0-50)+'" y1="'+y+'" x2="'+(x1+50)+'" y2="'+y+'"/>';
    row.forEach((id,ci)=>{ const t=byId[id]; if(!t) return; const cx=20+DX/2+ci*DX, n=t.r.length;
      const blocked=t.s.filter(s=>s==2).length, dis=!t.en, dead=!t.on&&!dis;
      g+='<g class="twr'+(dis?' dis':'')+'" onclick="showTower('+id+')">';
      for(let i=0;i<n;i++){ const a0=-Math.PI/2+(i/n)*2*Math.PI+0.05, a1=-Math.PI/2+((i+1)/n)*2*Math.PI-0.05;
        g+='<path d="'+wedge(cx,y,38,54,a0,a1)+'" fill="'+(dead?'#2a2f36':SC[t.s[i]]??SC[4])+'"><title>Tower '+id+' run '+(i+1)+': '+t.r[i]+'/s</title></path>'; }
      g+='<circle class="hub'+(blocked&&d.act?' alarm':'')+(dead?' dead':'')+'" cx="'+cx+'" cy="'+y+'" r="34"/>';
      g+='<text x="'+cx+'" y="'+(y-7)+'" style="font-size:22px">'+id+'</text>';
      g+='<text class="sub" x="'+cx+'" y="'+(y+14)+'">'+(dis?'OFF':dead?'NO SIG':(blocked&&d.act?'BLOCKED':t.avg+'/s'))+'</text></g>'; });
  });
  const m=$('map'); m.setAttribute('viewBox','0 0 '+W+' '+H); m.innerHTML=g;
}
function render(d){
  alarm=!!d.alarm; mute=d.mute;
  const st=$('st');
  if(!d.aog&&!d.cfg.always){st.textContent='No AgOpenGPS';st.className='pill low';}
  else if(!d.mon){st.textContent='Section off';st.className='pill';}
  else if(!d.act){st.textContent='Starting '+d.wait+'s';st.className='pill low';}
  else if(d.alarm){st.textContent='BLOCKAGE';st.className='pill bad';}
  else {st.textContent='Monitoring';st.className='pill ok';}
  $('aog').textContent='AgOpenGPS '+(d.aog?(d.sec?'section ON':'section off'):'not seen');
  $('aog').className='pill'+(d.aog?(d.sec?' ok':''):' low');
  $('spd').textContent=d.spd+' km/h';
  $('mute').textContent=d.mute?('Muted '+d.mute+'s'):'Mute 2 min';
  $('mute').classList.toggle('on',d.mute>0);
  const bad=[], off=[];
  let h='';
  for(const t of d.t){
    if(!t.on&&t.en) off.push(t.id);
    h+='<div id="tw'+t.id+'" class="tw'+(!t.en?' dis':t.on?'':' off')+'"><div class="th"><span>Tower '+t.id+'</span><small>'+
       (!t.en?'RANK OFF':t.on?(t.tune?'TUNE MODE':'avg '+t.avg+'/s'):'NO SIGNAL')+'</small></div><div class="bars">';
    t.r.forEach((v,i)=>{ const s=t.s[i], pct=t.avg?Math.round(v*100/t.avg):0;
      if(s==2) bad.push('T'+t.id+' R'+(i+1));
      const hgt=Math.min(pct,150)/1.5;
      h+='<div class="run s'+s+'" onclick="ignore('+t.id+','+(i+1)+')" title="'+v+'/s"><div class="well"><div class="fill" style="height:'+
         (s==3?100:hgt)+'%"></div></div><div class="lbl">'+(i+1)+'</div></div>'; });
    h+='</div></div>';
  }
  $('grid').innerHTML=h;
  const b=$('banner'), parts=[];
  if(d.act&&bad.length) parts.push('BLOCKED: '+bad.join(', '));
  if(d.act&&off.length) parts.push('NO SIGNAL: tower '+off.join(', '));
  b.textContent=parts.join('  |  '); b.classList.toggle('show',parts.length>0);
  if(!cfgLoaded){ for(const k of ['towers','block','warn','secs','delay','min']) $(k).value=d.cfg[k];
    $('always').checked=!!d.cfg.always; $('layout').value=d.cfg.layout||''; cfgLoaded=true; }
  drawMap(d);
}
function poll(){ fetch('/data',{cache:'no-store'}).then(r=>r.json()).then(d=>{last=Date.now();render(d);})
  .catch(()=>{}).finally(()=>setTimeout(poll,500)); }
setInterval(()=>{ if(!last||Date.now()-last>3000){ $('st').textContent='Cab unit not responding';$('st').className='pill bad';
  alarm=true; mute=0; } },1000);
poll();
</script></body></html>)HTML";
