/**
 * wm_strings_de.h
 * german strings for
 * WiFiManager, a library for the ESP8266/Arduino platform
 * for configuration of WiFi credentials using a Captive Portal
 *
 * @author WifiWhirl
 * @version 0.0.0
 * @license MIT
 */

 #ifndef _WM_STRINGS_DE_H_
 #define _WM_STRINGS_DE_H_
 
 // strings files must include a consts file!
 #include "wm_consts_en.h" // include constants, tokens, routes
 
 const char WM_LANGUAGE[] PROGMEM = "de-DE"; // i18n lang code
 
 const char HTTP_HEAD_START[] PROGMEM = "<!DOCTYPE html>"
										"<html lang='de'><head>"
										"<meta name='format-detection' content='telephone=no'>"
										"<meta charset='UTF-8'>"
										"<meta  name='viewport' content='width=device-width,initial-scale=1,user-scalable=no'/>"
										"<title>{v} Netzwerkverbindung</title>";
 
 const char HTTP_SCRIPT[] PROGMEM = "<script>function c(l){"
									"document.getElementById('s').value=l.getAttribute('data-ssid')||l.innerText||l.textContent;"
									"p = l.nextElementSibling.classList.contains('l');"
									"document.getElementById('p').disabled = !p;"
									"if(p)document.getElementById('p').focus();};"
									"function f() {var x = document.getElementById('p');x.type==='password'?x.type='text':x.type='password';}"
									"</script>"; // @todo add button states, disable on click , show ack , spinner etc
 
 const char HTTP_HEAD_END[] PROGMEM = "</head><body class='{c}'><div class='wrap'>"; // {c} = _bodyclass
 const char HTTP_ROOT_MAIN[] PROGMEM = "<h1>{t} Netzwerkverbindung</h1><h3>{v}</h3>";
 
 const char *const HTTP_PORTAL_MENU[] PROGMEM = {
	 "<form action='/wifi'    method='get'><button>WLAN konfigurieren</button></form><br/>\n",		  // MENU_WIFI x
	 "<form action='/0wifi'   method='get'><button>WLAN manuell konfigurieren</button></form><br/>\n", // MENU_WIFINOSCAN
	 "<form action='/info'    method='get'><button>Info</button></form><br/>\n",						  // MENU_INFO x
	 "<form action='/param'   method='get'><button>Einrichten</button></form><br/>\n",				  // MENU_PARAM
	 "<form action='/close'   method='get'><button>Schließen</button></form><br/>\n",				  // MENU_CLOSE
	 "<form action='/restart' method='get'><button>Neustarten</button></form><br/>\n",				  // MENU_RESTART
	 "<form action='/exit'    method='get'><button>Beenden</button></form><br/>\n",					  // MENU_EXIT x
	 "<form action='/erase'   method='get'><button class='D'>Löschen</button></form><br/>\n",		  // MENU_ERASE
	 "<!-- <form action='/update'  method='get'><button>Update</button></form><br/> -->\n",			  // MENU_UPDATE
	 "<hr><br/>"																						  // MENU_SEP
 };
 
 const char HTTP_PORTAL_OPTIONS[] PROGMEM = "";
 const char HTTP_ITEM_QI[] PROGMEM = "<div role='img' aria-label='{r}%' title='{r}%' class='q q-{q} {i} {h}'></div>"; // rssi icons
 const char HTTP_ITEM_QP[] PROGMEM = "<div class='q {h}'>{r}%</div>";												 // rssi percentage {h} = hidden showperc pref
 const char HTTP_ITEM[] PROGMEM = "<div><a href='#p' onclick='c(this)' data-ssid='{V}'>{v}</a>{qi}{qp}</div>";		 // {q} = HTTP_ITEM_QI, {r} = HTTP_ITEM_QP
 
 const char HTTP_FORM_START[] PROGMEM = "<form method='POST' action='{v}'>";
 const char HTTP_FORM_WIFI[] PROGMEM = "<label for='s'>SSID</label><input id='s' name='s' maxlength='32' autocorrect='off' autocapitalize='none' placeholder='{v}'><br/><label for='p'>Passwort</label><input id='p' name='p' maxlength='64' type='password' placeholder='{p}'><input type='checkbox' id='showpass' onclick='f()'> <label for='showpass'>Passwort anzeigen</label><br/>";
 const char HTTP_FORM_WIFI_END[] PROGMEM = "";
 const char HTTP_FORM_STATIC_HEAD[] PROGMEM = "<hr><br/>";
 const char HTTP_FORM_END[] PROGMEM = "<br/><br/><button type='submit'>Speichern</button></form>";
 const char HTTP_FORM_LABEL[] PROGMEM = "<label for='{i}'>{t}</label>";
 const char HTTP_FORM_PARAM_HEAD[] PROGMEM = "<hr><br/>";
 const char HTTP_FORM_PARAM[] PROGMEM = "<br/><input id='{i}' name='{n}' maxlength='{l}' value='{v}' {c}>\n"; // do not remove newline!
 
 const char HTTP_SCAN_LINK[] PROGMEM = "<br/><form action='/wifi?refresh=1' method='POST'><button name='refresh' value='1'>Aktualisieren</button></form>";
 const char HTTP_SAVED[] PROGMEM = "<div class='msg'>Die Zugangsdaten wurden gespeichert.<br/>Ein Verbindungsversuch wird durchgeführt.<br />Falls dies fehlschlägt, verbinde dich neu und versuche es erneut.</div>";
 const char HTTP_PARAMSAVED[] PROGMEM = "<div class='msg S'>Gespeichert<br/></div>";
 const char HTTP_END[] PROGMEM = "</div></body></html>";
 const char HTTP_ERASEBTN[] PROGMEM = "<br/><form action='/erase' method='get'><button class='D'>WLAN Konfiguration löschen</button></form>";
 const char HTTP_UPDATEBTN[] PROGMEM = "<br/><form action='/update' method='get'><button>Update</button></form>";
 const char HTTP_BACKBTN[] PROGMEM = "<hr><br/><form action='/' method='get'><button>Zurück</button></form>";
 
 const char HTTP_STATUS_ON[] PROGMEM = "<div class='msg S'><strong>Verbunden</strong> mit {v}<br/><em><small>und IP-Adresse {i}</small></em></div>";
 const char HTTP_STATUS_OFF[] PROGMEM = "<div class='msg {c}'><strong>Nicht verbunden</strong> mit {v}{r}</div>"; // {c=class} {v=ssid} {r=status_off}
 const char HTTP_STATUS_OFFPW[] PROGMEM = "<br/>Zugangsdaten fehlerhaft";										 // STATION_WRONG_PASSWORD,  no eps32
 const char HTTP_STATUS_OFFNOAP[] PROGMEM = "<br/>WLAN nicht gefunden";											 // WL_NO_SSID_AVAIL
 const char HTTP_STATUS_OFFFAIL[] PROGMEM = "<br/>Verbindung konnte nicht hergestellt werden";					 // WL_CONNECT_FAILED
 const char HTTP_STATUS_NONE[] PROGMEM = "<div class='msg'>Nicht verbunden</div>";
 const char HTTP_BR[] PROGMEM = "<br/>";
 
 const char HTTP_STYLE[] PROGMEM = "<style>"
								   ".c,body{text-align:center;font-family:verdana}div,input,select{padding:5px;font-size:1em;margin:5px 0;box-sizing:border-box}"
								   "input,button,select,.msg{border-radius:.3rem;width: 100%}input[type=radio],input[type=checkbox]{width:auto}"
								   "button,input[type='button'],input[type='submit']{cursor:pointer;border:0;background-color:#1fa3ec;color:#fff;line-height:2.4rem;font-size:1.2rem;width:100%}"
								   "input[type='file']{border:1px solid #1fa3ec}"
								   ".wrap {text-align:left;display:inline-block;min-width:260px;max-width:500px}"
								   // links
								   "a{color:#000;font-weight:700;text-decoration:none}a:hover{color:#1fa3ec;text-decoration:underline}"
								   // quality icons
								   ".q{height:16px;margin:0;padding:0 5px;text-align:right;min-width:38px;float:right}.q.q-0:after{background-position-x:0}.q.q-1:after{background-position-x:-16px}.q.q-2:after{background-position-x:-32px}.q.q-3:after{background-position-x:-48px}.q.q-4:after{background-position-x:-64px}.q.l:before{background-position-x:-80px;padding-right:5px}.ql .q{float:left}.q:after,.q:before{content:'';width:16px;height:16px;display:inline-block;background-repeat:no-repeat;background-position: 16px 0;"
								   "background-image:url('data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAGAAAAAQCAMAAADeZIrLAAAAJFBMVEX///8AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAADHJj5lAAAAC3RSTlMAIjN3iJmqu8zd7vF8pzcAAABsSURBVHja7Y1BCsAwCASNSVo3/v+/BUEiXnIoXkoX5jAQMxTHzK9cVSnvDxwD8bFx8PhZ9q8FmghXBhqA1faxk92PsxvRc2CCCFdhQCbRkLoAQ3q/wWUBqG35ZxtVzW4Ed6LngPyBU2CobdIDQ5oPWI5nCUwAAAAASUVORK5CYII=');}"
								   // icons @2x media query (32px rescaled)
								   "@media (-webkit-min-device-pixel-ratio: 2),(min-resolution: 192dpi){.q:before,.q:after {"
								   "background-image:url('data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAALwAAAAgCAMAAACfM+KhAAAALVBMVEX///8AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAADAOrOgAAAADnRSTlMAESIzRGZ3iJmqu8zd7gKjCLQAAACmSURBVHgB7dDBCoMwEEXRmKlVY3L//3NLhyzqIqSUggy8uxnhCR5Mo8xLt+14aZ7wwgsvvPA/ofv9+44334UXXngvb6XsFhO/VoC2RsSv9J7x8BnYLW+AjT56ud/uePMdb7IP8Bsc/e7h8Cfk912ghsNXWPpDC4hvN+D1560A1QPORyh84VKLjjdvfPFm++i9EWq0348XXnjhhT+4dIbCW+WjZim9AKk4UZMnnCEuAAAAAElFTkSuQmCC');"
								   "background-size: 95px 16px;}}"
								   // msg callouts
								   ".msg{padding:20px;margin:20px 0;border:1px solid #eee;border-left-width:5px;border-left-color:#777}.msg h4{margin-top:0;margin-bottom:5px}.msg.P{border-left-color:#1fa3ec}.msg.P h4{color:#1fa3ec}.msg.D{border-left-color:#dc3630}.msg.D h4{color:#dc3630}.msg.S{border-left-color: #5cb85c}.msg.S h4{color: #5cb85c}"
								   // lists
								   "dt{font-weight:bold}dd{margin:0;padding:0 0 0.5em 0;min-height:12px}"
								   "td{vertical-align: top;}"
								   ".h{display:none}"
								   "button{transition: 0s opacity;transition-delay: 3s;transition-duration: 0s;cursor: pointer}"
								   "button.D{background-color:#dc3630}"
								   "button:active{opacity:50% !important;cursor:wait;transition-delay: 0s}"
								   // invert
								   "body.invert,body.invert a,body.invert h1 {background-color:#060606;color:#fff;}"
								   "body.invert .msg{color:#fff;background-color:#282828;border-top:1px solid #555;border-right:1px solid #555;border-bottom:1px solid #555;}"
								   "body.invert .q[role=img]{-webkit-filter:invert(1);filter:invert(1);}"
								   ":disabled {opacity: 0.5;}"
								   "</style>";
 
 #ifndef WM_NOHELP
 const char HTTP_HELP[] PROGMEM = "<p>WifiWhirl <a href='https://wifiwhirl.de'>https://wifiwhirl.de</a>";
 #else
 const char HTTP_HELP[] PROGMEM = "";
 #endif
 
 const char HTTP_UPDATE[] PROGMEM = "Upload new firmware<br/><form method='POST' action='u' enctype='multipart/form-data' onchange=\"(function(el){document.getElementById('uploadbin').style.display = el.value=='' ? 'none' : 'initial';})(this)\"><input type='file' name='update' accept='.bin,application/octet-stream'><button id='uploadbin' type='submit' class='h D'>Update</button></form><small><a href='http://192.168.4.1/update' target='_blank'>* May not function inside captive portal, open in browser http://192.168.4.1</a><small>";
 const char HTTP_UPDATE_FAIL[] PROGMEM = "<div class='msg D'><strong>Update failed!</strong><Br/>Reboot device and try again</div>";
 const char HTTP_UPDATE_SUCCESS[] PROGMEM = "<div class='msg S'><strong>Update successful.  </strong> <br/> Device rebooting now...</div>";
 
 #ifdef WM_JSTEST
 const char HTTP_JS[] PROGMEM =
	 "<script>function postAjax(url, data, success) {"
	 "    var params = typeof data == 'string' ? data : Object.keys(data).map("
	 "            function(k){ return encodeURIComponent(k) + '=' + encodeURIComponent(data[k]) }"
	 "        ).join('&');"
	 "    var xhr = window.XMLHttpRequest ? new XMLHttpRequest() : new ActiveXObject(\"Microsoft.XMLHTTP\");"
	 "    xhr.open('POST', url);"
	 "    xhr.onreadystatechange = function() {"
	 "        if (xhr.readyState>3 && xhr.status==200) { success(xhr.responseText); }"
	 "    };"
	 "    xhr.setRequestHeader('X-Requested-With', 'XMLHttpRequest');"
	 "    xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');"
	 "    xhr.send(params);"
	 "    return xhr;}"
	 "postAjax('/status', 'p1=1&p2=Hello+World', function(data){ console.log(data); });"
	 "postAjax('/status', { p1: 1, p2: 'Hello World' }, function(data){ console.log(data); });"
	 "</script>";
 #endif
 
 // Info html
 const char HTTP_INFO_esphead[] PROGMEM = "<h3>WifiWhirl-Modul</h3><hr><dl>";
 const char HTTP_INFO_fchipid[] PROGMEM = "<dt>Flash Chip ID</dt><dd>{1}</dd>";
 const char HTTP_INFO_corever[] PROGMEM = "<dt>Core Version</dt><dd>{1}</dd>";
 const char HTTP_INFO_bootver[] PROGMEM = "<dt>Boot Version</dt><dd>{1}</dd>";
 const char HTTP_INFO_lastreset[] PROGMEM = "<dt>Grund für letzten Neustart</dt><dd>{1}</dd>";
 const char HTTP_INFO_flashsize[] PROGMEM = "<dt>Tatsächliche Flash Größe</dt><dd>{1} bytes</dd>";
 
 const char HTTP_INFO_memsmeter[] PROGMEM = "<br/><progress value='{1}' max='{2}'></progress></dd>";
 const char HTTP_INFO_memsketch[] PROGMEM = "<dt>Arbeitsspeicher - Sketch Größe</dt><dd>Genutzt / Gesamt bytes<br/>{1} / {2}";
 const char HTTP_INFO_freeheap[] PROGMEM = "<dt>Arbeitsspeicher - Freier Heap Speicher</dt><dd>{1} bytes verfügbar</dd>";
 const char HTTP_INFO_wifihead[] PROGMEM = "<br/><h3>WLAN</h3><hr>";
 const char HTTP_INFO_uptime[] PROGMEM = "<dt>Laufzeit</dt><dd>{1} mins {2} secs</dd>";
 const char HTTP_INFO_chipid[] PROGMEM = "<dt>Chip ID</dt><dd>{1}</dd>";
 const char HTTP_INFO_idesize[] PROGMEM = "<dt>Flash Größe</dt><dd>{1} bytes</dd>";
 const char HTTP_INFO_sdkver[] PROGMEM = "<dt>SDK Version</dt><dd>{1}</dd>";
 const char HTTP_INFO_cpufreq[] PROGMEM = "<dt>CPU Frequenz</dt><dd>{1}MHz</dd>";
 const char HTTP_INFO_apip[] PROGMEM = "<dt>Access Point IP</dt><dd>{1}</dd>";
 const char HTTP_INFO_apmac[] PROGMEM = "<dt>Access Point MAC</dt><dd>{1}</dd>";
 const char HTTP_INFO_apssid[] PROGMEM = "<dt>Access Point SSID</dt><dd>{1}</dd>";
 const char HTTP_INFO_apbssid[] PROGMEM = "<dt>BSSID</dt><dd>{1}</dd>";
 const char HTTP_INFO_stassid[] PROGMEM = "<dt>Station SSID</dt><dd>{1}</dd>";
 const char HTTP_INFO_staip[] PROGMEM = "<dt>Station IP</dt><dd>{1}</dd>";
 const char HTTP_INFO_stagw[] PROGMEM = "<dt>Station Gateway</dt><dd>{1}</dd>";
 const char HTTP_INFO_stasub[] PROGMEM = "<dt>Station Subnetzmaske</dt><dd>{1}</dd>";
 const char HTTP_INFO_dnss[] PROGMEM = "<dt>DNS Server</dt><dd>{1}</dd>";
 const char HTTP_INFO_host[] PROGMEM = "<dt>Hostname</dt><dd>{1}</dd>";
 const char HTTP_INFO_stamac[] PROGMEM = "<dt>Station MAC</dt><dd>{1}</dd>";
 const char HTTP_INFO_conx[] PROGMEM = "<dt>Verbunden</dt><dd>{1}</dd>";
 const char HTTP_INFO_autoconx[] PROGMEM = "<dt>Automatisch verbinden</dt><dd>{1}</dd>";
 
 const char HTTP_INFO_aboutver[] PROGMEM = "<dt>WiFiManager</dt><dd>{1}</dd>";
 const char HTTP_INFO_aboutarduino[] PROGMEM = "<!-- <dt>Arduino</dt><dd>{1}</dd> -->";
 const char HTTP_INFO_aboutsdk[] PROGMEM = "<dt>ESP-SDK/IDF</dt><dd>{1}</dd>";
 const char HTTP_INFO_aboutdate[] PROGMEM = "<dt>Build Datum</dt><dd>{1}</dd>";
 
 const char S_brand[] PROGMEM = "WifiWhirl";
 const char S_debugPrefix[] PROGMEM = "*wm: ";
 const char S_y[] PROGMEM = "Ja";
 const char S_n[] PROGMEM = "Nein";
 const char S_enable[] PROGMEM = "Ein";
 const char S_disable[] PROGMEM = "Aus";
 const char S_GET[] PROGMEM = "GET";
 const char S_POST[] PROGMEM = "POST";
 const char S_NA[] PROGMEM = "Unbekannt";
 const char S_passph[] PROGMEM = "********";
 const char S_titlewifisaved[] PROGMEM = "Zugangsdaten gespeichert";
 const char S_titlewifisettings[] PROGMEM = "Einstellungen gespeichert";
 const char S_titlewifi[] PROGMEM = "C";
 const char S_titleinfo[] PROGMEM = "Info";
 const char S_titleparam[] PROGMEM = "Einrichtung";
 const char S_titleparamsaved[] PROGMEM = "Daten gespeichert";
 const char S_titleexit[] PROGMEM = "Neustart";
 const char S_titlereset[] PROGMEM = "Zurücksetzen";
 const char S_titleerase[] PROGMEM = "Löschen";
 const char S_titleclose[] PROGMEM = "Schließen";
 const char S_options[] PROGMEM = "Optionen";
 const char S_nonetworks[] PROGMEM = "Es wurden keine Netzwerke gefunden.";
 const char S_staticip[] PROGMEM = "Statische IP-Adresse";
 const char S_staticgw[] PROGMEM = "Statisches Gateway";
 const char S_staticdns[] PROGMEM = "Statischer DNS";
 const char S_subnet[] PROGMEM = "Subnetzmaske";
 const char S_exiting[] PROGMEM = "WifiWhirl startet neu";
 const char S_resetting[] PROGMEM = "WifiWhirl startet neu";
 const char S_closing[] PROGMEM = "Du kannst diese Seite nun schließen";
 const char S_error[] PROGMEM = "Es ist ein Fehler aufgetreten";
 const char S_notfound[] PROGMEM = "Die Datei konnte nicht gefunden werden\n\n";
 const char S_uri[] PROGMEM = "URI: ";
 const char S_method[] PROGMEM = "\nMethode: ";
 const char S_args[] PROGMEM = "\nArgumente: ";
 const char S_parampre[] PROGMEM = "param_";
 
 // debug strings
 const char D_HR[] PROGMEM = "--------------------";
 
 // softap ssid default prefix
 const char S_ssidpre[] PROGMEM = "wifiwhirl";
 
 // END WIFI_MANAGER_OVERRIDE_STRINGS
 #endif