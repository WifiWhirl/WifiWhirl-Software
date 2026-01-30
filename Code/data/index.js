// the web socket connection
var connection;

// command mapping
const cmdMap = {
  setTarget: 0,
  setTargetSelector: 0,
  toggleUnit: 1,
  toggleBubbles: 2,
  toggleHeater: 3,
  togglePump: 4,
  //resetq: 5,
  restartEsp: 6,
  //gettarget: 7,
  resetTotals: 8,
  resetTimerChlorine: 9,
  resetTimerFilter: 10,
  toggleHydroJets: 11,
  setBrightness: 12,
  setBrightnessSelector: 12,
  setBeep: 13,
  setAmbient: 15,
  setAmbientSelector: 15,
  setAmbientF: 14,
  setAmbientC: 15,
  resetDaily: 16,
  toggleGodmode: 17,
  setFullpower: 18,
  printText: 19,
  setReady: 20,
  togglePWR: 22,
  toggleLCK: 23,
  resetTimerCleanFilter: 24,
  resetTimerWaterChange: 25,
  resetTimerPh: 27,
  setPhValue: 28,
  setClValue: 29,
  setCyaValue: 30,
  setAlkValue: 31,
};

// button element ID mapping
const btnMap = {
  toggleUnit: "UNT",
  toggleBubbles: "AIR",
  toggleHeater: "HTR",
  togglePump: "FLT",
  toggleHydroJets: "HJT",
  toggleGodmode: "GOD",
};

// to be used for setting the control values once after loading original values from the web socket
var initControlValues = true;

// display brightness multiplier. lower value results lower brightness levels (1-30)
const dspBrtMultiplier = 16;

// update states
updateTempState = false;
updateAmbState = false;

// water quality editing states and debounce timers
var wqEditingState = {
  ph: false,
  cl: false,
  cya: false,
  alk: false
};
var wqDebounceTimers = {
  ph: null,
  cl: null,
  cya: null,
  alk: null
};
updateBrtState = false;

// initial connect to the web socket
// connect();

function connect() {
  connection = new WebSocket("ws://" + location.hostname + ":81/", ["arduino"]);

  connection.onopen = function () {
    document.body.classList.add("connected");
    initControlValues = true;
  };

  connection.onerror = function (error) {
    console.log("WebSocket Error ", error);
    document.body.classList.add("error");
    connection.close();
  };

  connection.onclose = function () {
    console.log("WebSocket connection closed, reconnecting in 5 s");
    document.body.classList.add("error");
    setTimeout(function () {
      connect();
    }, 5000);
  };

  connection.onmessage = function (e) {
    handlemsg(e);
  };
}

String.prototype.pad = function (String, len) {
  var str = this;
  while (str.length < len) {
    str = String + str;
  }
  return str;
};

function tryParseJSONObject(jsonString) {
  try {
    var o = JSON.parse(jsonString);

    // Handle non-exception-throwing cases:
    // Neither JSON.parse(false) or JSON.parse(1234) throw errors, hence the type-checking,
    // but... JSON.parse(null) returns null, and typeof null === "object",
    // so we must check for that, too. Thankfully, null is falsey, so this suffices:
    if (o && typeof o === "object") {
      return o;
    }
  } catch (e) {}

  return false;
}

function handlemsg(e) {
  console.log(e.data);
  var msgobj = tryParseJSONObject(e.data);
  if (!msgobj) return;
  console.log(msgobj);

  if (msgobj.CONTENT == "OTHER") {
    // MQTT status
    mqtt_states = [
      "Zeitüberschreitung beim Verbindungsaufbau", // -4 / the server didn't respond within the keepalive time
      "Verbindung verloren", // -3 / the network connection was broken
      "Verbindung kann nicht hergestellt werden", // -2 / the network connection failed
      "Getrennt", // -1 / the client is disconnected cleanly
      "Verbunden", // 0 / the client is connected
      "Fehler: Bad Protocol", // 1 / the server doesn't support the requested version of MQTT
      "Fehler: Bad Client ID", // 2 / the server rejected the client identifier
      "Fehler: Server nicht verfügbar", // 3 / the server was unable to accept the connection
      "Fehler: Falsche Zugangsdaten", // 4 / the username/password were rejected
      "Fehler: WifiWhirl darf sich nicht verbinden", // 5 / the client was not authorized to connect
    ];

    try {
      if (msgobj.MQTT == -1) {
        document.getElementById("mqtt").innerHTML = "";
      } else {
        document.getElementById("mqtt").innerHTML =
          "MQTT: " + mqtt_states[msgobj.MQTT + 4];
      }
    } catch (error) {
      console.error(error);
    }

    document.getElementById("fw").innerHTML = "WifiWhirl " + msgobj.FW;

    // Set wifi symbol signal strenght
    if (msgobj.RSSI <= -80) {
      document.getElementById("rssi").className = "waveStrength-1";
    } else if (msgobj.RSSI <= -70) {
      document.getElementById("rssi").className = "waveStrength-2";
    } else if (msgobj.RSSI <= -67) {
      document.getElementById("rssi").className = "waveStrength-3";
    } else {
      document.getElementById("rssi").className = "waveStrength-4";
    }

    document
      .getElementById("rssi2")
      .setAttribute("data-text", "RSSI: " + msgobj.RSSI);

    // hydro jets available
    try {
      document.getElementById("jets").style.display = msgobj.HASJETS
        ? "table-cell"
        : "none";
      document.getElementById("jetsswitch").style.display = msgobj.HASJETS
        ? "table-cell"
        : "none";
      document.getElementById("jetstotals").style.display = msgobj.HASJETS
        ? ""
        : "none";
    } catch (error) {
      console.error(error);
    }

    const ambElement = document.getElementById("amb");
    ambElement.disabled = msgobj.WEATHER;
    ambElement.title = msgobj.WEATHER
      ? "Umgebungstemperatur wird per Wetterdatenabfrage gesetzt."
      : "";
  }
  try {
    if (msgobj.CONTENT == "STATES") {
      // temperature
      document.getElementById("atlabel").innerHTML = msgobj.TMP.toString();
      document.getElementById("ttlabel").innerHTML = msgobj.TGT.toString();

      // buttons
      document.getElementById("ONOFF").checked = msgobj.PWR;
      document.getElementById("LCK").checked = msgobj.LCK;

      document.getElementById("AIR").checked = msgobj.AIR;
      if (document.getElementById("UNT").checked != msgobj.UNT) {
        document.getElementById("UNT").checked = msgobj.UNT;
        initControlValues = true;
      }
      document.getElementById("FLT").checked = msgobj.FLT;
      document.getElementById("HJT").checked = msgobj.HJT;
      document.getElementById("HTR").checked = msgobj.RED || msgobj.GRN;

      // heater button color
      document.getElementById("htrspan").classList.remove("heateron");
      document.getElementById("htrspan").classList.remove("heateroff");
      if (msgobj.RED || msgobj.GRN) {
        document
          .getElementById("htrspan")
          .classList.add(
            msgobj.RED ? "heateron" : msgobj.GRN ? "heateroff" : "n-o-n-e"
          );
      }

      // display
      document.getElementById("display").innerHTML = String.fromCharCode(
        msgobj.CH1,
        msgobj.CH2,
        msgobj.CH3
      );
      document.getElementById("display").style.color = rgb(
        255 -
          dspBrtMultiplier * 8 +
          dspBrtMultiplier * (parseInt(msgobj.BRT) + 1),
        0,
        0
      );
      if (msgobj.CH1 != 101 && msgobj.CH3 != 32 && msgobj.CH1 != 54) {
        msgobj.UNT
          ? (document.getElementById("display").innerHTML += " °C")
          : (document.getElementById("display").innerHTML += " °F");
      } else {
        document.getElementById("display").innerHTML += " ";
      }

      // set control values (once)
      if (initControlValues) {
        var minTemp = msgobj.UNT ? 20 : 68;
        var maxTemp = msgobj.UNT ? 40 : 104;
        var minAmb = msgobj.UNT ? -40 : -40;
        var maxAmb = msgobj.UNT ? 60 : 140;
        document.getElementById("temp").min = minTemp;
        document.getElementById("temp").max = maxTemp;
        document.getElementById("selectorTemp").min = minTemp;
        document.getElementById("selectorTemp").max = maxTemp;
        document.getElementById("amb").min = minAmb;
        document.getElementById("amb").max = maxAmb;
        document.getElementById("selectorAmb").min = minAmb;
        document.getElementById("selectorAmb").max = maxAmb;

        document.getElementById("temp").value = msgobj.TGT;
        document.getElementById("amb").value = msgobj.AMB;
        document.getElementById("brt").value = msgobj.BRT;

        initControlValues = false;
      }

      document.getElementById("sliderTempVal").innerHTML = msgobj.TGT;
      document.getElementById("sliderAmbVal").innerHTML = msgobj.AMB;
      document.getElementById("sliderBrtVal").innerHTML = msgobj.BRT;

      // get selector elements
      var elemSelectorTemp = document.getElementById("selectorTemp");
      var elemSelectorAmb = document.getElementById("selectorAmb");
      var elemSelectorBrt = document.getElementById("selectorBrt");

      // change values only if element is not active (selected for input)
      // also change only if an update is not in progress
      if (document.activeElement !== elemSelectorTemp && !updateTempState) {
        elemSelectorTemp.value = msgobj.TGT;
        elemSelectorTemp.parentElement.querySelector(
          ".numDisplay"
        ).textContent = msgobj.TGT;
      }
      if (document.activeElement !== elemSelectorAmb && !updateAmbState) {
        elemSelectorAmb.value = msgobj.AMB;
        elemSelectorAmb.parentElement.querySelector(".numDisplay").textContent =
          msgobj.AMB;
      }
      if (document.activeElement !== elemSelectorBrt && !updateBrtState) {
        elemSelectorBrt.value = msgobj.BRT;
        elemSelectorBrt.parentElement.querySelector(".numDisplay").textContent =
          msgobj.BRT;
      }

      // reset update states when the set target matches the input
      if (elemSelectorTemp.value == msgobj.TGT) updateTempState = false;
      if (elemSelectorAmb.value == msgobj.AMB) updateAmbState = false;
      if (elemSelectorBrt.value == msgobj.BRT) updateBrtState = false;

      const unitsymbols = document.querySelectorAll("[id^=unitcf]");
      if (msgobj.UNT) {
        unitsymbols.forEach((unitsymbol) => {
          unitsymbol.innerHTML = "C";
        });
      } else {
        unitsymbols.forEach((unitsymbol) => {
          unitsymbol.innerHTML = "F";
        });
      }

      // get slider elements
      var sliderTemp = document.getElementById("temp");
      var sliderAmb = document.getElementById("amb");
      var sliderBrt = document.getElementById("brt");

      // Update slider positions as long as the user is not actively interacting
      if (document.activeElement !== sliderTemp && !updateTempState) {
        sliderTemp.value = msgobj.TGT;
      }
      if (document.activeElement !== sliderAmb && !updateAmbState) {
        sliderAmb.value = msgobj.AMB;
      }
      if (document.activeElement !== sliderBrt && !updateBrtState) {
        sliderBrt.value = msgobj.BRT;
      }
    }
  } catch (error) {
    console.error(error);
  }
  try {
    if (msgobj.CONTENT == "TIMES") {
      var date = new Date(msgobj.TIME * 1000);
      document.getElementById("time").innerHTML = date.toLocaleString();

      // chlorine add reset timer
      var clTimeSec = Math.floor(Date.now() / 1000 - msgobj.CLTIME);
      var clDays = clTimeSec / (24 * 3600);
      var clTimerEl = document.getElementById("cltimer");
      clTimerEl.innerHTML = getTimeSinceText(clTimeSec);
      clTimerEl.title = formatTimestamp(msgobj.CLTIME);
      document.getElementById("cltimerbtn").className =
        (clDays > msgobj.CLINT && clTimeSec <= TWENTY_YEARS_SEC) ? "button_red" : "button";

      // filter change reset timer
      var fTimeSec = Math.floor(Date.now() / 1000 - msgobj.FTIME);
      var fDays = fTimeSec / (24 * 3600);
      var fTimerEl = document.getElementById("ftimer");
      fTimerEl.innerHTML = getTimeSinceText(fTimeSec);
      fTimerEl.title = formatTimestamp(msgobj.FTIME);
      document.getElementById("ftimerbtn").className =
        (fDays > msgobj.FINT && fTimeSec <= TWENTY_YEARS_SEC) ? "button_red" : "button";

      // filter clean reset timer
      var fcTimeSec = Math.floor(Date.now() / 1000 - msgobj.FCTIME);
      var fcDays = fcTimeSec / (24 * 3600);
      var fcTimerEl = document.getElementById("fctimer");
      fcTimerEl.innerHTML = getTimeSinceText(fcTimeSec);
      fcTimerEl.title = formatTimestamp(msgobj.FCTIME);
      document.getElementById("fctimerbtn").className =
        (fcDays > msgobj.FCINT && fcTimeSec <= TWENTY_YEARS_SEC) ? "button_red" : "button";

      // water change reset timer
      var wcTimeSec = Math.floor(Date.now() / 1000 - msgobj.WCTIME);
      var wcDays = wcTimeSec / (24 * 3600);
      var wcTimerEl = document.getElementById("wctimer");
      wcTimerEl.innerHTML = getTimeSinceText(wcTimeSec);
      wcTimerEl.title = formatTimestamp(msgobj.WCTIME);
      document.getElementById("wctimerbtn").className =
        (wcDays > msgobj.WCINT && wcTimeSec <= TWENTY_YEARS_SEC) ? "button_red" : "button";

      // statistics
      document.getElementById("heatingtime").innerHTML = s2dhms(
        msgobj.HEATINGTIME
      );
      document.getElementById("uptime").innerHTML = s2dhms(msgobj.UPTIME);
      document.getElementById("airtime").innerHTML = s2dhms(msgobj.AIRTIME);
      document.getElementById("filtertime").innerHTML = s2dhms(msgobj.PUMPTIME);
      document.getElementById("jettime").innerHTML = s2dhms(msgobj.JETTIME);
      document.getElementById("cost").innerHTML = msgobj.COST.toFixed(2)
        .toString()
        .replace(".", ",");
      document.getElementById("t2r").innerHTML =
        s2dhms(msgobj.T2R * 3600) +
        " (" +
        (msgobj.T2R <= 0 ? "Zeit für ein Bad!" : "Nicht bereit") +
        ")";

      // energy monitoring
      document.getElementById("energy_power").innerHTML = msgobj.WATT || 0;
      document.getElementById("energy_today").innerHTML = (msgobj.KWHD || 0)
        .toFixed(2)
        .toString()
        .replace(".", ",");
      document.getElementById("energy_total").innerHTML = (msgobj.KWH || 0)
        .toFixed(2)
        .toString()
        .replace(".", ",");

      // water quality - pH value (only update if not editing)
      var phTimeSec = Math.floor(Date.now() / 1000 - msgobj.PHTIME);
      var phVal = (msgobj.PHVAL || 72) / 10;
      var phTimerEl = document.getElementById("phtimer");
      phTimerEl.innerHTML = getTimeSinceText(phTimeSec);
      phTimerEl.title = formatTimestamp(msgobj.PHTIME);
      if (!wqEditingState.ph) {
        document.getElementById("phinput").value = phVal.toFixed(1).replace(".", ",");
      }

      // water quality - Chlorine value (only update if not editing)
      var clvTimeSec = Math.floor(Date.now() / 1000 - msgobj.CLVTIME);
      var clVal = (msgobj.CLVAL || 10) / 10;
      var clvTimerEl = document.getElementById("cltimer2");
      clvTimerEl.innerHTML = getTimeSinceText(clvTimeSec);
      clvTimerEl.title = formatTimestamp(msgobj.CLVTIME);
      if (!wqEditingState.cl) {
        document.getElementById("clinput").value = clVal.toFixed(1).replace(".", ",");
      }

      // water quality - Cyanuric acid (only update if not editing)
      var cyaTimeSec = Math.floor(Date.now() / 1000 - (msgobj.CYATIME || 0));
      var cyaVal = (msgobj.CYAVAL || 0) / 10;
      var cyaTimerEl = document.getElementById("cyatimer");
      if (cyaTimerEl) {
        cyaTimerEl.innerHTML = getTimeSinceText(cyaTimeSec);
        cyaTimerEl.title = formatTimestamp(msgobj.CYATIME);
      }
      if (document.getElementById("cyainput") && !wqEditingState.cya) {
        document.getElementById("cyainput").value = cyaVal.toFixed(1).replace(".", ",");
      }

      // water quality - Alkalinity (only update if not editing)
      var alkTimeSec = Math.floor(Date.now() / 1000 - (msgobj.ALKTIME || 0));
      var alkVal = msgobj.ALKVAL || 0;
      var alkTimerEl = document.getElementById("alktimer");
      if (alkTimerEl) {
        alkTimerEl.innerHTML = getTimeSinceText(alkTimeSec);
        alkTimerEl.title = formatTimestamp(msgobj.ALKTIME);
      }
      if (document.getElementById("alkinput") && !wqEditingState.alk) {
        document.getElementById("alkinput").value = alkVal;
      }
    }
  } catch (error) {
    console.error(error);
  }
}

// 20 years in seconds (threshold for "noch nie" / never)
const TWENTY_YEARS_SEC = 20 * 365 * 24 * 3600;

// Format Unix timestamp to German date format "DD.MM.YYYY HH:mm Uhr"
// Returns empty string if timestamp is invalid (> 20 years ago)
function formatTimestamp(unixTimestamp) {
  if (!unixTimestamp || unixTimestamp <= 0) return "";
  var timeSinceSec = Math.floor(Date.now() / 1000) - unixTimestamp;
  if (timeSinceSec > TWENTY_YEARS_SEC) return "";
  var date = new Date(unixTimestamp * 1000);
  var day = String(date.getDate()).padStart(2, '0');
  var month = String(date.getMonth() + 1).padStart(2, '0');
  var year = date.getFullYear();
  var hours = String(date.getHours()).padStart(2, '0');
  var minutes = String(date.getMinutes()).padStart(2, '0');
  return day + "." + month + "." + year + " " + hours + ":" + minutes + " Uhr";
}

// getTimeSinceText expects seconds (not days) for precise time display
function getTimeSinceText(seconds) {
  // If timestamp is more than 20 years in the past, show "noch nie"
  if (seconds > TWENTY_YEARS_SEC) return "noch nie";
  
  // "gerade eben" only for the last 60 seconds
  if (seconds < 60) return "gerade eben";
  
  // minutes (1-59 min)
  var minutes = Math.floor(seconds / 60);
  if (minutes < 60) {
    if (minutes == 1) return "vor einer Minute";
    return "vor " + minutes + " Minuten";
  }
  
  // hours (1-23 h)
  var hours = Math.floor(seconds / 3600);
  if (hours < 24) {
    if (hours == 1) return "vor einer Stunde";
    return "vor " + hours + " Stunden";
  }
  
  // days (1+)
  var days = Math.floor(seconds / 86400);
  if (days == 1) return "vor einem Tag";
  return "vor " + days + " Tagen";
}

// Parse value with comma or dot as decimal separator
function parseWqValue(inputValue) {
  return parseFloat(inputValue.toString().replace(",", "."));
}

// Called on input change - starts editing state and debounce timer
function onPhInput() {
  wqEditingState.ph = true;
  if (wqDebounceTimers.ph) clearTimeout(wqDebounceTimers.ph);
  wqDebounceTimers.ph = setTimeout(function() {
    savePhValue();
    wqEditingState.ph = false;
  }, 1000);
}

function savePhValue() {
  var val = parseWqValue(document.getElementById("phinput").value);
  if (val >= 0 && val <= 14) {
    sendCommandWithValue("setPhValue", Math.round(val * 10));
    document.getElementById("phtimer").innerHTML = "gerade eben";
  }
}

// Called on input change - starts editing state and debounce timer
function onClInput() {
  wqEditingState.cl = true;
  if (wqDebounceTimers.cl) clearTimeout(wqDebounceTimers.cl);
  wqDebounceTimers.cl = setTimeout(function() {
    saveClValue();
    wqEditingState.cl = false;
  }, 1000);
}

function saveClValue() {
  var val = parseWqValue(document.getElementById("clinput").value);
  if (val >= 0 && val <= 10) {
    sendCommandWithValue("setClValue", Math.round(val * 10));
    document.getElementById("cltimer2").innerHTML = "gerade eben";
  }
}

// Called on input change - starts editing state and debounce timer
function onCyaInput() {
  wqEditingState.cya = true;
  if (wqDebounceTimers.cya) clearTimeout(wqDebounceTimers.cya);
  wqDebounceTimers.cya = setTimeout(function() {
    saveCyaValue();
    wqEditingState.cya = false;
  }, 1000);
}

// Save cyanuric acid value (Cyanursäure) - range 0-100 mg/L
function saveCyaValue() {
  var val = parseWqValue(document.getElementById("cyainput").value);
  if (val >= 0 && val <= 100) {
    sendCommandWithValue("setCyaValue", Math.round(val * 10));
    document.getElementById("cyatimer").innerHTML = "gerade eben";
  }
}

// Called on input change - starts editing state and debounce timer
function onAlkInput() {
  wqEditingState.alk = true;
  if (wqDebounceTimers.alk) clearTimeout(wqDebounceTimers.alk);
  wqDebounceTimers.alk = setTimeout(function() {
    saveAlkValue();
    wqEditingState.alk = false;
  }, 1000);
}

// Save alkalinity value (Alkalinität) - range 0-300 ppm
function saveAlkValue() {
  var val = parseInt(document.getElementById("alkinput").value);
  if (val >= 0 && val <= 300) {
    sendCommandWithValue("setAlkValue", val);
    document.getElementById("alktimer").innerHTML = "gerade eben";
  }
}

function sendCommandWithValue(cmd, value) {
  if (typeof cmdMap[cmd] == "undefined") {
    console.log("invalid command");
    return;
  }
  var obj = {};
  obj["CMD"] = cmdMap[cmd];
  obj["VALUE"] = value;
  obj["XTIME"] = Math.floor(Date.now() / 1000);
  obj["INTERVAL"] = 0;
  obj["TXT"] = "";
  var json = JSON.stringify(obj);
  connection.send(json);
  console.log(json);
}

function s2dhms(val) {
  // Handle special states from backend (sent as seconds: -2*3600, -1*3600)
  if (val == -7200) {
    // Ready state (-2 * 3600)
    return "00:00:00";
  } else if (val == -3600) {
    // Never ready / calculation not possible (-1 * 3600)
    return "Berechnung nicht möglich";
  }
  var day = 3600 * 24;
  var hour = 3600;
  var minute = 60;
  var rem;
  var days = Math.floor(val / day);
  rem = val % day;
  var hours = Math.floor(rem / hour);
  rem = val % hour;
  var minutes = Math.floor(rem / minute);
  rem = val % minute;
  var seconds = Math.floor(rem);
  return (
    (days >= 1 ? days + " Tag" + (days != 1 ? "e" : "") + " " : "") +
    hours.toString().pad("0", 2) +
    ":" +
    minutes.toString().pad("0", 2) +
    ":" +
    seconds.toString().pad("0", 2)
  );
}

function sendCommand(cmd) {
  console.log(cmd);
  console.log(typeof cmdMap[cmd]);
  // check command
  if (typeof cmdMap[cmd] == "undefined") {
    console.log("invalid command");
    return;
  }

  // get the current unit (true=C, false=F)
  var unit = document.getElementById("UNT").checked;

  // get and set value
  var value = 0;
  if (cmd == "setTarget" || cmd == "setTargetSelector") {
    value = parseInt(
      document.getElementById(cmd == "setTarget" ? "temp" : "selectorTemp")
        .value
    );
    value = getProperValue(value, unit ? 20 : 68, unit ? 40 : 104);
    document.getElementById("sliderTempVal").innerHTML = value.toString();
    document.getElementById("selectorTemp").value = value.toString();
    document
      .getElementById("selectorTemp")
      .setAttribute("value", value.toString());
    updateTempState = true;
  } else if (cmd == "togglePWR" || cmd == "toggleLCK") {
    value = 0;
  } else if (cmd == "setAmbient" || cmd == "setAmbientSelector") {
    value = parseInt(
      document.getElementById(cmd == "setAmbient" ? "amb" : "selectorAmb").value
    );
    value = getProperValue(value, unit ? -40 : -40, unit ? 60 : 140);
    document.getElementById("sliderAmbVal").innerHTML = value.toString();
    document.getElementById("selectorAmb").value = value.toString();
    cmd = "setAmbient" + (unit ? "C" : "F");
    updateAmbState = true;
  } else if (cmd == "setBrightness" || cmd == "setBrightnessSelector") {
    var brtElement = document.getElementById(
      cmd == "setBrightness" ? "brt" : "selectorBrt"
    );
    value = parseInt(brtElement.value);
    value = getProperValue(
      value,
      Number(brtElement.min),
      Number(brtElement.max)
    );
    document.getElementById("sliderBrtVal").innerHTML = value.toString();
    document.getElementById("selectorBrt").value = value.toString();
    document.getElementById("display").style.color = rgb(
      255 - dspBrtMultiplier * 8 + dspBrtMultiplier * (value + 1),
      0,
      0
    );
    updateBrtState = true;
  } else if (
    btnMap[cmd] &&
    (cmd == "toggleUnit" ||
      cmd == "toggleBubbles" ||
      cmd == "toggleHeater" ||
      cmd == "togglePump" ||
      cmd == "toggleHydroJets" ||
      cmd == "toggleGodmode")
  ) {
    value = document.getElementById(btnMap[cmd]).checked;
    initControlValues = true;
  }

  var obj = {};
  obj["CMD"] = cmdMap[cmd];
  obj["VALUE"] = value;
  obj["XTIME"] = Math.floor(Date.now() / 1000);
  obj["INTERVAL"] = 0;
  obj["TXT"] = "";
  var json = JSON.stringify(obj);
  connection.send(json);
  console.log(json);
}

function getProperValue(val, min, max) {
  return val < min ? min : val > max ? max : val;
}

function rgb(r, g, b) {
  // Clamp values to valid RGB range (0-255)
  r = Math.max(0, Math.min(255, Math.floor(r)));
  g = Math.max(0, Math.min(255, Math.floor(g)));
  b = Math.max(0, Math.min(255, Math.floor(b)));

  return `rgb(${r}, ${g}, ${b})`;
}
