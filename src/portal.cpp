#include "portal.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

#include "display.h"
#include "mesh.h"

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

namespace portal {

namespace {

const char kPage[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>UAV-BOS Mesh-Repeater</title>
<style>
body{font-family:system-ui,sans-serif;margin:0;background:#f2f2f2;color:#222}
header{background:#b71c1c;color:#fff;padding:12px 16px;font-size:1.2em;font-weight:600}
main{max-width:520px;margin:auto;padding:12px}
section{background:#fff;border-radius:8px;padding:12px 16px;margin-bottom:12px;box-shadow:0 1px 3px #0002}
h2{font-size:1em;margin:0 0 8px}
label{display:block;margin-top:10px;font-size:.9em;color:#555}
input,select{width:100%;box-sizing:border-box;padding:8px;font-size:1em;border:1px solid #bbb;border-radius:4px}
button{margin-top:14px;padding:10px 14px;font-size:1em;border:0;border-radius:4px;background:#b71c1c;color:#fff;cursor:pointer}
button.sec{background:#666}
.row{display:flex;gap:8px}.row>*{flex:1}
table{width:100%;border-collapse:collapse;font-size:.9em}td{padding:3px 0}td:first-child{color:#666;width:45%}
.ok{color:#2e7d32;font-weight:600}.err{color:#c62828;font-weight:600}
small,p.hint{color:#777}p.hint{font-size:.8em;line-height:1.35;margin:2px 0 0}
a{color:#b71c1c}code{word-break:break-all}
.row button{margin-top:4px}
.nt td,.nt td:first-child{width:auto;color:#222;padding:3px 4px 3px 0}.nt tr:first-child td{color:#666;font-weight:600}
</style></head><body>
<header>UAV-BOS Mesh-Repeater</header>
<main>
<section><h2>Steuerung</h2>
<label>Betriebsart (sofort, ohne Neustart)</label>
<div class="row" id="modes"><button type="button" class="sec" data-m="0" onclick="setMode(0)">Repeater + WLAN</button>
<button type="button" class="sec" data-m="1" onclick="setMode(1)">Repeater ohne WLAN</button></div>
<p class="hint">Beide Arten leiten Funkpakete weiter. "Repeater + WLAN" haelt nur diesen Config-Zugang offen und verbindet sich mit keinem Router. "Repeater ohne WLAN" schaltet das WLAN aus. Zurueck geht es mit einem kurzen Druck auf PRG.</p>
<label>Display</label>
<div class="row"><button type="button" class="sec" id="blbtn" onclick="toggleBl()">-</button></div>
<p class="hint">Schaltet nur das OLED. Der Repeater funkt weiter. Die Taste 3 s halten macht dasselbe.</p>
<small id="ctlmsg"></small></section>
<section><h2>Status</h2><table id="st"><tr><td>Lade...</td></tr></table></section>
<section id="meshsec" style="display:none"><h2>LoRa-Mesh</h2><table id="mt"></table>
<table id="nodes" class="nt" style="margin-top:10px"></table></section>
<section><h2>Einstellungen</h2>
<form method="POST" action="/save" onsubmit="return chk()">
<label>Betriebsart</label>
<select name="mode" id="mode"><option value="0">Repeater + WLAN (nur Config-AP)</option>
<option value="1">Repeater ohne WLAN</option></select>
<p class="hint">Gilt nach dem Speichern und bleibt nach einem Neustart erhalten. Ein kurzer Tastendruck schaltet im Betrieb dasselbe um.</p>
<h2 style="margin-top:16px">LoRa / Meshtastic</h2>
<small>Alle Tracker und die Meshtastic-Knoten, die weiterleiten sollen, brauchen dasselbe Profil und denselben Slot.
Standard ist Meshtastic EU_868 LongFast.</small>
<label>Modemprofil <a href="/hilfe/profil">Profile erklaeren</a></label>
<select name="preset" id="preset" onchange="slotVis()"><option value="0">ShortFast (SF7, 250 kHz)</option>
<option value="1">ShortSlow (SF8, 250 kHz)</option><option value="2">MediumFast (SF9, 250 kHz)</option>
<option value="3">MediumSlow (SF10, 250 kHz)</option><option value="4">LongFast (SF11, 250 kHz, Standard)</option>
<option value="5">LongModerate (SF11, 125 kHz)</option><option value="6">LongSlow (SF12, 125 kHz)</option></select>
<div id="slotrow"><label>Frequenz-Slot</label>
<select name="slot" id="slot"><option value="0">Meshtastic-Standard</option><option value="1">1 (869,4625 MHz)</option>
<option value="2">2 (869,5875 MHz)</option></select>
<p class="hint">Nur bei LongModerate und LongSlow. Profile mit 250 kHz senden immer auf 869,525 MHz. "Standard" waehlt den Slot wie Meshtastic aus dem Profilnamen.</p></div>
<label>Hop-Limit eigener Pakete (1-7)</label>
<input name="hops" id="hops" type="number" min="1" max="7" required>
<p class="hint">Wird gespeichert wie beim Tracker. Das Hello dieses Repeaters geht mit Hop 0 raus und wird nicht weitergeflutet. Weitergeleitete Pakete behalten das Hop-Limit des Absenders, minus eins.</p>
<label>Sendeleistung (dBm, 2-20)</label><input name="txpower" id="txpower" type="number" min="2" max="20" required>
<p class="hint">20 dBm ist der Hochleistungsmodus des SX1276. 18 und 19 dBm kann der Chip nicht, die werden als 17 gespeichert. Der Tracker mit SX1262 darf bis 22 dBm.</p>
<label>Weiterleitung</label>
<select name="relay" id="relay" onchange="relayVis()"><option value="0">Alle Pakete (wie Meshtastic "ALL")</option>
<option value="1">Nur Pakete der UAV-BOS-Tracker</option><option value="2">Keine</option></select>
<p class="hint">"Alle" reicht auch fremde Meshtastic-Pakete weiter und vergroessert deren Netz. "Nur Tracker" bleibt auf dem UAV-BOS-Kanal. "Keine" leitet nichts weiter.</p>
<div id="fairrow"><label>Sendezeit fuer fremde Pakete (% pro Stunde, 0-10)</label>
<input name="fairtime" id="fairtime" type="number" min="0" max="10" required>
<p class="hint">Fremde Meshtastic-Pakete werden nur weitergeleitet, solange die gesamte Sendezeit unter diesem Wert liegt. 0 schaltet das aus, 10 geht bis zur gesetzlichen Grenze. Tracker-Pakete werden immer weitergeleitet.</p></div>
<label>Mesh-Schluessel <small id="keyhint"></small></label>
<div class="row"><input name="meshkey" id="meshkey" type="password" maxlength="64" autocomplete="off" spellcheck="false">
<button type="button" class="sec" style="margin-top:0;flex:0 0 auto" onclick="showKey()">Anzeigen</button>
<button type="button" class="sec" style="margin-top:0;flex:0 0 auto" onclick="genKey()">Neu</button></div>
<p class="hint">Base64, 16 oder 32 Byte. Derselbe Schluessel wie auf den Trackern der Organisation, sonst erkennt der Repeater den UAV-BOS-Kanal nicht. "Neu" nur verwenden, wenn danach jedes Geraet denselben Schluessel bekommt.</p>
<label>Passwort fuer Config-AP, Benutzer "admin" <small id="aphint">(min. 8 Zeichen, leer = kein Passwort)</small></label>
<input name="appass" id="appass" type="password" maxlength="63">
<p class="hint">Schuetzt diesen Zugang. Mindestens 8 Zeichen. Ein leeres Feld laesst ein gesetztes Passwort stehen.</p>
<label id="apclrrow" style="display:none"><input type="checkbox" name="apclear" value="1" style="width:auto"> Passwort entfernen</label>
<button type="submit">Speichern &amp; Neustart</button>
</form></section>
<section><h2>Wartung</h2>
<form method="POST" action="/reset" onsubmit="return confirm('Alle Einstellungen loeschen?')">
<button class="sec" type="submit">Werkseinstellungen</button></form>
<p class="hint">Loescht Mesh-Schluessel, LoRa-Einstellungen und die Betriebsart. Der Repeater startet neu und oeffnet den Config-AP.</p>
<small id="fw"></small></section>
</main>
<script>
const $=id=>document.getElementById(id);
function esc(s){return String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]))}
function chk(){const a=$('appass').value;if(a&&a.length<8){alert('AP-Passwort min. 8 Zeichen');return false}
 const k=$('meshkey').value.trim();if(k&&!keyOk(k)){alert('Mesh-Schluessel ungueltig (Base64, 16 oder 32 Byte)');return false}
 return true}
function keyOk(k){try{const n=atob(k).length;return n==16||n==32}catch(e){return false}}
function showKey(){$('meshkey').type='text'}
function genKey(){
 if($('meshkey').value&&!confirm('Neuen Schluessel erzeugen? Alle anderen Geraete brauchen danach denselben Schluessel.'))return;
 const b=new Uint8Array(32);crypto.getRandomValues(b);$('meshkey').value=btoa(String.fromCharCode(...b));showKey();
}
async function load(){
 const s=await (await fetch('/settings')).json();
 $('mode').value=s.mode;
 $('preset').value=s.preset;$('slot').value=s.slot;$('hops').value=s.hops;$('txpower').value=s.txpower;
 $('relay').value=s.relay;$('fairtime').value=s.fairtime;slotVis();relayVis();
 if(s.hasApPass){$('aphint').textContent='(gesetzt, leer = unveraendert)';$('apclrrow').style.display='block'}
 $('meshkey').value=s.meshKey;
 if(!s.meshKeySet)$('keyhint').innerHTML='<span class="err">Standard-Schluessel aktiv, bitte setzen</span>';
 else if(!s.meshKey)$('keyhint').textContent='(gesetzt, nur am Config-AP sichtbar, leer = unveraendert)';
 $('fw').textContent='Firmware '+s.fw+' | '+s.mac;
}
function slotVis(){$('slotrow').style.display=$('preset').value>=5?'block':'none'}
function relayVis(){$('fairrow').style.display=$('relay').value=='0'?'block':'none'}
async function status(){
 try{const s=await (await fetch('/status')).json();
 const m=s.mesh;
 let r=[['Betriebsart',esc(s.mode)],['WLAN',esc(s.wifi)],['IP',esc(s.ip)]];
 if(m.error)r.push(['LoRa','<span class="err">'+esc(m.error)+'</span>']);
 $('st').innerHTML=rows(r);
 showCtl(s.modeId,s.display);
 $('meshsec').style.display=m.active?'block':'none';
 if(m.active){
  let t=[['Eigene Knoten-ID',m.node+(m.placeholder?' <span class="err">Standard-Schluessel!</span>':'')],
  ['Funk',esc(m.preset)+', '+m.freq.toFixed(4)+' MHz, SF'+m.sf+', '+m.bw+' kHz, CR 4/'+m.cr+', '+m.txp+' dBm, '+m.hops+' Hops'],
  ['Verbundene Tracker<br><small>in den letzten 5 min gehoert</small>','<b>'+m.act+'</b> ('+m.direct+' direkt, '+(m.act-m.direct)+' ueber Weiterleitung)'],
  ['Tracker mit Positionsdaten<br><small>letzte Stunde</small>','<b>'+m.pos1h+'</b>'],
  ['Eigene LoRa-Sendungen',m.tx+(m.lastTx>=0?' (Hello vor '+age(m.lastTx)+')':'')],
  ['Weitergeleitete Pakete',m.relay+' ('+m.relayOwn+' Tracker, '+m.relayForeign+' fremde)'+
   (m.fdrop?', <span class="err">'+m.fdrop+' fremde verworfen</span>':'')],
  ['Empfangen ueber Meshtastic<br><small>letzter Weiterleiter kein Tracker</small>',m.viaForeign],
  ['Airtime letzte Stunde',m.air.toFixed(1)+' % von 10 % ('+m.airTracker.toFixed(1)+' % Tracker)'+(m.blocked?', <span class="err">'+m.blocked+' blockiert</span>':'')]];
  if(m.rx)t.push(['Letzter Empfang','RSSI '+m.rssi+' dBm, SNR '+m.snr.toFixed(1)+' dB']);
  $('mt').innerHTML=rows(t);
  $('nodes').innerHTML=m.nodes.length?'<tr><td>Tracker</td><td>zuletzt</td><td>Position</td><td>Weg</td><td>Signal</td></tr>'+
   m.nodes.map(n=>'<tr><td>'+n.id+'</td><td>vor '+age(n.ago)+'</td><td>'+(n.pos<0?'-':'vor '+age(n.pos)+' ('+n.cnt+')')+
   '</td><td>'+(n.hops?n.hops+' Hop'+(n.hops>1?'s':'')+(n.mt?' (Meshtastic)':''):'direkt')+'</td><td>'+n.rssi+' dBm / '+n.snr.toFixed(1)+' dB</td></tr>').join('')
   :'<tr><td>Noch keine anderen Tracker gehoert</td></tr>';
 }
 }catch(e){}
}
function rows(r){return r.map(x=>'<tr><td>'+x[0]+'</td><td>'+x[1]+'</td></tr>').join('')}
function age(s){return s<120?s+' s':s<7200?Math.round(s/60)+' min':Math.round(s/3600)+' h'}
let curMode=-1,dispOn=true;
function showCtl(mode,disp){
 curMode=mode;dispOn=disp;
 document.querySelectorAll('#modes button').forEach(b=>b.className=b.dataset.m==mode?'':'sec');
 $('blbtn').textContent=disp?'Display ist an - ausschalten':'Display ist aus - einschalten';
}
async function post(url,body){
 const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body||''});
 const t=await r.text();if(!r.ok)throw new Error(t);return t;
}
async function setMode(m){
 const old=curMode;
 if(m==old)return;
 if(m==1&&!confirm('"Repeater ohne WLAN" schaltet das WLAN aus. Diese Seite ist danach nur wieder erreichbar, wenn PRG kurz gedrueckt wird. Fortfahren?'))return;
 try{await post('/mode','mode='+m)}catch(e){alert(e.message);return}
 $('mode').value=m;showCtl(m,dispOn);
 $('ctlmsg').textContent=m==1?'WLAN wird ausgeschaltet, die Verbindung bricht ab.':'Umgeschaltet.';
}
async function toggleBl(){
 try{const r=JSON.parse(await post('/display'));showCtl(curMode,r.on)}catch(e){alert(e.message)}
}
load();status();setInterval(status,2000);
</script></body></html>)HTML";

const char kPresetHelpPage[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Modemprofile</title>
<style>
body{font-family:system-ui,sans-serif;margin:0;background:#f2f2f2;color:#222}
header{background:#b71c1c;color:#fff;padding:12px 16px;font-size:1.2em;font-weight:600}
main{max-width:520px;margin:auto;padding:12px}
section{background:#fff;border-radius:8px;padding:12px 16px;margin-bottom:12px;box-shadow:0 1px 3px #0002}
h2{font-size:1em;margin:16px 0 8px}h2:first-child{margin-top:0}
p,li{font-size:.9em;line-height:1.4}
table{width:100%;border-collapse:collapse;font-size:.8em;margin-top:8px}
th,td{text-align:left;padding:4px 6px 4px 0;border-bottom:1px solid #eee;vertical-align:top}
th{color:#666;font-weight:600}
a{color:#b71c1c}
</style></head><body>
<header>Modemprofile</header>
<main><section>
<p><a href="/">Zurueck zu den Einstellungen</a></p>
<h2>Was ein Profil ist</h2>
<p>Das Modemprofil legt fest, wie der Repeater funkt: Frequenz, Bandbreite, Spreizfaktor (SF) und
Fehlerkorrektur. Zwei Geraete hoeren sich nur, wenn Profil und Frequenz-Slot gleich sind.
Das gilt fuer die Tracker und fuer Meshtastic-Knoten, die Pakete weiterleiten sollen.</p>
<p>Das Profil ist nicht der Meshtastic-Kanal. Der Kanal (Name und Schluessel) verschluesselt den Inhalt.
Der Repeater sendet auf dem Kanal UAV-BOS. Den Schluessel stellt "Mesh-Schluessel" ein.
Meshtastic-Geraete koennen die Pakete weiterleiten, ohne sie zu lesen.</p>
<h2>Reichweite und Sendezeit</h2>
<p>Ein hoeherer Spreizfaktor reicht weiter und braucht laenger fuer dasselbe Paket.
Im Band 869,4 bis 869,65 MHz sind 10&nbsp;% Sendezeit pro Stunde erlaubt.
Lange Profile verbrauchen das Budget schnell, dann fallen Weiterleitungen aus.</p>
<table>
<tr><th>Profil</th><th>Funk</th><th>Position</th><th>Wann</th></tr>
<tr><td>ShortFast</td><td>SF7, 250 kHz</td><td>0,05 s</td><td>Kurze Strecke, viele Fahrzeuge</td></tr>
<tr><td>ShortSlow</td><td>SF8, 250 kHz</td><td>0,09 s</td><td>Etwas weiter als ShortFast</td></tr>
<tr><td>MediumFast</td><td>SF9, 250 kHz</td><td>0,16 s</td><td>Mittlere Reichweite</td></tr>
<tr><td>MediumSlow</td><td>SF10, 250 kHz</td><td>0,3 s</td><td>Mittlere Reichweite, robuster</td></tr>
<tr><td>LongFast</td><td>SF11, 250 kHz</td><td>0,56 s</td><td>Standard. Die meisten Meshtastic-Netze in der EU nutzen das</td></tr>
<tr><td>LongModerate</td><td>SF11, 125 kHz, CR 4/8</td><td>1,8 s</td><td>Weiter als LongFast, deutlich mehr Sendezeit</td></tr>
<tr><td>LongSlow</td><td>SF12, 125 kHz, CR 4/8</td><td>3,3 s</td><td>Weiteste Reichweite, hoechste Sendezeit</td></tr>
</table>
<p>Die Zeiten gelten fuer eine Positionsmeldung. Weiterleitungen kommen dazu.</p>
<h2>Frequenz</h2>
<ul>
<li>250 kHz (ShortFast bis LongFast): genau ein Platz, immer 869,525 MHz. Der Frequenz-Slot wird ignoriert.</li>
<li>125 kHz (LongModerate, LongSlow): zwei Plaetze. Slot 1 ist 869,4625 MHz, Slot 2 ist 869,5875 MHz.</li>
<li>"Meshtastic-Standard" waehlt den Slot aus dem Profilnamen, so wie Meshtastic es bei einem Kanal ohne
eigenen Namen tut. Hat das Netz vor Ort einen eigenen Kanalnamen, den Slot von Hand auf denselben Wert setzen.</li>
</ul>
<h2>Empfehlung</h2>
<p>Dasselbe Profil wie die Tracker verwenden. LongFast lassen, solange das Meshtastic-Netz vor Ort nichts anderes nutzt.</p>
</section></main></body></html>)HTML";

const char kSavedPage[] PROGMEM = R"HTML(<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Gespeichert</title></head>
<body style="font-family:sans-serif;text-align:center;padding-top:40px">
<h2>%MSG%</h2><p>Der Repeater startet neu.</p></body></html>)HTML";

WebServer server(80);
DNSServer dns;
RepeaterConfig *cfgRef = nullptr;
bool running = false;
bool reboot = false;
uint32_t activityMs = 0;
bool modeRequested = false;
RepeaterMode requestedMode = RepeaterMode::WithWifi;

String jsonEscape(const String &s) {
  String out;
  out.reserve(s.length() + 8);
  for (char c : s) {
    switch (c) {
    case '"': out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\n': out += "\\n"; break;
    case '\r': out += "\\r"; break;
    case '\t': out += "\\t"; break;
    default:
      if ((uint8_t)c < 0x20) {
        char buf[8];
        snprintf(buf, sizeof(buf), "\\u%04x", c);
        out += buf;
      } else {
        out += c;
      }
    }
  }
  return out;
}

void touch() { activityMs = millis(); }

void sendSaved(const char *msg) {
  String page = FPSTR(kSavedPage);
  page.replace("%MSG%", msg);
  server.send(200, "text/html", page);
}

void handleRoot() {
  touch();
  server.send_P(200, "text/html", kPage);
}

void handlePresetHelp() {
  touch();
  server.send_P(200, "text/html", kPresetHelpPage);
}

void handleSettings() {
  touch();
  String json = "{";
  json += "\"hasApPass\":" + String(cfgRef->apPass.length() ? "true" : "false") + ",";
  json += "\"mode\":" + String((int)cfgRef->mode) + ",";
  const MeshSettings &ms = cfgRef->mesh;
  json += "\"preset\":" + String((int)ms.preset) + ",\"slot\":" + String(ms.slot) + ",\"hops\":" +
          String(ms.hopLimit) + ",\"txpower\":" + String(ms.txPowerDbm) + ",\"relay\":" + String((int)ms.relayMode) +
          ",\"fairtime\":" + String(ms.foreignAirtimePct) + ",";
  bool keySet = cfgRef->meshKey.length() && cfgRef->meshKey != config::kPlaceholderMeshKey;
  json += "\"meshKey\":\"" + (keySet ? jsonEscape(cfgRef->meshKey) : String("")) + "\",";
  json += "\"meshKeySet\":" + String(keySet ? "true" : "false") + ",";
  json += "\"fw\":\"" FW_VERSION "\",";
  json += "\"mac\":\"" + WiFi.macAddress() + "\"}";
  server.send(200, "application/json", json);
}

void handleStatus() {
  touch();
  MeshStats m = mesh::stats();
  char meshJson[768];
  snprintf(meshJson, sizeof(meshJson),
           "{\"ok\":%s,\"active\":%s,\"placeholder\":%s,\"node\":\"!%08lx\",\"heard\":%u,\"act\":%u,\"direct\":%u,"
           "\"pos1h\":%u,\"rx\":%lu,\"relay\":%lu,\"relayOwn\":%lu,\"relayForeign\":%lu,\"fdrop\":%lu,"
           "\"viaForeign\":%lu,\"tx\":%lu,\"blocked\":%lu,\"lastTx\":%ld,\"air\":%.2f,"
           "\"airTracker\":%.2f,\"rssi\":%d,"
           "\"snr\":%.1f,\"preset\":\"%s\",\"freq\":%.4f,\"sf\":%u,\"bw\":%.0f,\"cr\":%u,\"txp\":%d,\"hops\":%u,"
           "\"error\":\"%s\",\"nodes\":[",
           m.ok ? "true" : "false", m.active ? "true" : "false", m.placeholderKey ? "true" : "false",
           (unsigned long)m.nodeNum, m.heardNodes, m.activeNodes, m.directNodes, m.positionNodes1h,
           (unsigned long)m.rxCount, (unsigned long)m.relayCount, (unsigned long)m.ownRelayCount,
           (unsigned long)m.foreignRelayCount, (unsigned long)m.foreignDropped, (unsigned long)m.viaForeignCount,
           (unsigned long)m.txCount, (unsigned long)m.txBlocked,
           m.lastTxMs ? (long)((millis() - m.lastTxMs) / 1000) : -1L, m.airtimePercent, m.trackerAirtimePercent,
           m.lastRssi, m.lastSnr, m.presetName, m.freqMhz, m.spreadingFactor, m.bandwidthKhz, m.codingRate,
           m.txPowerDbm, m.hopLimit, m.error);

  String nodesJson;
  MeshNodeInfo nodes[32];
  size_t count = mesh::nodes(nodes, 32);
  for (size_t i = 0; i < count; i++) {
    char buf[160];
    snprintf(buf, sizeof(buf),
             "%s{\"id\":\"!%08lx\",\"ago\":%lu,\"pos\":%ld,\"cnt\":%lu,\"hops\":%u,\"mt\":%s,\"rssi\":%d,\"snr\":%.1f}",
             i ? "," : "", (unsigned long)nodes[i].node, (unsigned long)nodes[i].lastAgoSec,
             (long)nodes[i].lastPosAgoSec, (unsigned long)nodes[i].positions, nodes[i].hops,
             nodes[i].viaForeign ? "true" : "false", nodes[i].rssi, nodes[i].snr);
    nodesJson += buf;
  }

  String json = "{\"mode\":\"" + String(config::modeName(cfgRef->mode)) + "\",\"modeId\":" + String((int)cfgRef->mode) +
                ",\"display\":" + (display::on() ? "true" : "false") + ",\"wifi\":\"Config-AP\",\"ip\":\"" +
                WiFi.softAPIP().toString() + "\",\"mesh\":" + String(meshJson) + nodesJson + "]}}";
  server.send(200, "application/json", json);
}

void handleSave() {
  touch();
  RepeaterConfig next = *cfgRef;

  long mode = server.hasArg("mode") ? server.arg("mode").toInt() : (long)cfgRef->mode;
  next.mode = (RepeaterMode)constrain(mode, (long)RepeaterMode::WithWifi, (long)RepeaterMode::RadioOnly);

  MeshSettings &ms = next.mesh;
  auto argOr = [](const char *name, long fallback) {
    return server.hasArg(name) && server.arg(name).length() ? server.arg(name).toInt() : fallback;
  };
  long preset = argOr("preset", (long)ms.preset);
  if (preset < 0 || preset >= (long)LoraPreset::Count) {
    server.send(400, "text/plain", "Ungueltiges Modemprofil");
    return;
  }
  ms.preset = (LoraPreset)preset;
  ms.slot = constrain(argOr("slot", ms.slot), 0L, (long)config::kMaxSlot);
  ms.hopLimit = constrain(argOr("hops", ms.hopLimit), (long)config::kMinHopLimit, (long)config::kMaxHopLimit);
  ms.txPowerDbm =
      constrain(argOr("txpower", ms.txPowerDbm), (long)config::kMinTxPowerDbm, (long)config::kMaxTxPowerDbm);
  ms.relayMode = (RelayMode)constrain(argOr("relay", (long)ms.relayMode), (long)RelayMode::All, (long)RelayMode::None);
  ms.foreignAirtimePct = constrain(argOr("fairtime", ms.foreignAirtimePct), 0L, (long)config::kMaxForeignAirtimePct);

  String meshKey = server.arg("meshkey");
  meshKey.trim();
  if (meshKey.length()) {
    if (!config::validMeshKey(meshKey)) {
      server.send(400, "text/plain", "Mesh-Schluessel ungueltig (Base64, 16 oder 32 Byte)");
      return;
    }
    next.meshKey = meshKey;
  }

  String apPass = server.arg("appass");
  if (apPass.length() && apPass.length() < 8) {
    server.send(400, "text/plain", "AP-Passwort min. 8 Zeichen");
    return;
  }
  if (server.hasArg("apclear")) next.apPass = "";
  else if (apPass.length()) next.apPass = apPass;

  config::save(next);
  *cfgRef = next;
  sendSaved("Einstellungen gespeichert");
  reboot = true;
}

void handleMode() {
  touch();
  long m = server.hasArg("mode") ? server.arg("mode").toInt() : -1;
  if (m < (long)RepeaterMode::WithWifi || m > (long)RepeaterMode::RadioOnly) {
    server.send(400, "text/plain", "Ungueltige Betriebsart");
    return;
  }
  requestedMode = (RepeaterMode)m;
  modeRequested = true;
  server.send(200, "text/plain", "OK");
}

void handleDisplay() {
  touch();
  if (server.hasArg("on")) display::setOn(server.arg("on") == "1");
  else display::toggle();
  server.send(200, "application/json", String("{\"on\":") + (display::on() ? "true" : "false") + "}");
}

void handleReset() {
  touch();
  config::clear();
  sendSaved("Werkseinstellungen wiederhergestellt");
  reboot = true;
}

void handleNotFound() {
  server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
  server.send(302, "text/plain", "");
}

std::function<void()> guarded(void (*handler)()) {
  return [handler]() {
    if (cfgRef->apPass.length() >= 8 && !server.authenticate("admin", cfgRef->apPass.c_str())) {
      server.requestAuthentication(BASIC_AUTH, "UAV-BOS Repeater");
      return;
    }
    handler();
  };
}

void setupRoutes() {
  static bool registered = false;
  if (registered) return;
  registered = true;
  server.on("/", HTTP_GET, guarded(handleRoot));
  server.on("/hilfe/profil", HTTP_GET, guarded(handlePresetHelp));
  server.on("/settings", HTTP_GET, guarded(handleSettings));
  server.on("/status", HTTP_GET, guarded(handleStatus));
  server.on("/save", HTTP_POST, guarded(handleSave));
  server.on("/reset", HTTP_POST, guarded(handleReset));
  server.on("/mode", HTTP_POST, guarded(handleMode));
  server.on("/display", HTTP_POST, guarded(handleDisplay));
  server.onNotFound(handleNotFound);
}

} // namespace

void startAp(RepeaterConfig &cfg, const String &apSsid) {
  stop();
  cfgRef = &cfg;

  WiFi.mode(WIFI_AP);
  const char *pass = cfg.apPass.length() >= 8 ? cfg.apPass.c_str() : nullptr;
  WiFi.softAP(apSsid.c_str(), pass);
  delay(100);

  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());

  setupRoutes();
  server.begin();
  running = true;
  touch();
  Serial.printf("[portal] AP %s started on %s\n", apSsid.c_str(), WiFi.softAPIP().toString().c_str());
}

void stop() {
  if (!running) return;
  server.stop();
  dns.stop();
  WiFi.softAPdisconnect(true);
  running = false;
}

void loop() {
  if (!running) return;
  dns.processNextRequest();
  if (WiFi.softAPgetStationNum() > 0) touch();
  server.handleClient();
}

bool rebootRequested() { return reboot; }
uint32_t lastActivityMs() { return activityMs; }
uint8_t apClients() { return running ? WiFi.softAPgetStationNum() : 0; }

bool takeModeRequest(RepeaterMode &mode) {
  if (!modeRequested) return false;
  modeRequested = false;
  mode = requestedMode;
  return true;
}

} // namespace portal
