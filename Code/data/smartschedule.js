/**
 * Smart Schedule
 * Handles UI interaction and communication with WifiWhirl backend
 */

let scheduleUpdateInterval = null;

/**
 * Load smart schedule on page load
 */
function loadSmartSchedule() {
  // Set default datetime to today at 19:00
  const today = new Date();
  const year = today.getFullYear();
  const month = String(today.getMonth() + 1).padStart(2, "0");
  const day = String(today.getDate()).padStart(2, "0");
  document.getElementById("targetDateTime").value =
    year + "-" + month + "-" + day + "T19:00";

  // Fetch current global target temperature and set as default
  fetchGlobalTargetTemp();

  // Start updating schedule status
  updateScheduleStatus();
  scheduleUpdateInterval = setInterval(updateScheduleStatus, 5000); // Update every 5 seconds
}

/**
 * Fetch global target temperature to pre-fill the form
 */
function fetchGlobalTargetTemp() {
  const xhr = new XMLHttpRequest();
  xhr.open("POST", "/getsmartschedule/", true);
  xhr.setRequestHeader("Content-Type", "application/json");

  xhr.onreadystatechange = function () {
    if (xhr.readyState === 4 && xhr.status === 200) {
      try {
        const data = JSON.parse(xhr.responseText);
        // Set global target temperature as default for new schedules
        if (data.GLOBALTARGET && data.GLOBALTARGET > 0) {
          document.getElementById("targetTemp").value = data.GLOBALTARGET;
        }
      } catch (e) {
        console.error("Failed to fetch global target:", e);
      }
    }
  };

  xhr.send("{}");
}

/**
 * Update schedule status display
 */
function updateScheduleStatus() {
  const xhr = new XMLHttpRequest();
  xhr.open("POST", "/getsmartschedule/", true);
  xhr.setRequestHeader("Content-Type", "application/json");

  xhr.onreadystatechange = function () {
    if (xhr.readyState === 4 && xhr.status === 200) {
      try {
        const data = JSON.parse(xhr.responseText);
        displayScheduleStatus(data);
      } catch (e) {
        console.error("Failed to parse schedule data:", e);
      }
    }
  };

  xhr.send("{}");
}

/**
 * Display schedule status in UI
 */
function displayScheduleStatus(data) {
  const isActive = data.ACTIVE || false;

  // Show/hide sections based on active state
  document.getElementById("statusActive").style.display = isActive
    ? "block"
    : "none";
  document.getElementById("statusInactive").style.display = isActive
    ? "none"
    : "block";

  if (isActive) {
    // Update status fields
    document.getElementById("statusTargetTemp").textContent =
      data.TARGETTEMP || "--";
    document.getElementById("statusCurrentTemp").textContent =
      data.CURRENTTEMP || "--";
    document.getElementById("statusAccurateTemp").textContent =
      data.ACCURATETEMP && data.ACCURATETEMP > 0
        ? data.ACCURATETEMP + " °C"
        : "Wird gemessen...";

    // Display heating estimate (raw, without buffer)
    if (data.ESTIMATE >= 999) {
      document.getElementById("statusEstimate").innerHTML =
        '<span style="color: #ff9800;">Unmöglich zu heizen (Umgebung zu kalt)</span>';
      document.getElementById("statusBuffer").textContent = "--";
    } else if (data.ESTIMATE > 0) {
      var estimateSeconds = Math.round(data.ESTIMATE * 3600);
      document.getElementById("statusEstimate").textContent =
        formatDuration(estimateSeconds);
      // Display safety buffer
      var bufferSeconds = Math.round((data.BUFFER || 0) * 3600);
      document.getElementById("statusBuffer").textContent =
        bufferSeconds > 0 ? formatDuration(bufferSeconds) : "--";
    } else {
      document.getElementById("statusEstimate").textContent =
        "Bereits auf Temperatur";
      document.getElementById("statusBuffer").textContent = "--";
    }

    // Display remaining heating time (live countdown while heater is running)
    // Use the same dynamic calculation as the dashboard ("Bereit in" / T2R)
    var remainingRow = document.getElementById("statusRemainingRow");
    if (data.HEATER && data.REMAINING_HEATING_TIME !== undefined && data.REMAINING_HEATING_TIME >= 0) {
      // Backend calculates this dynamically using the same physics as dashboard T2R
      var remainingHours = data.REMAINING_HEATING_TIME;
      remainingRow.style.display = "table-row";
      if (remainingHours === 0) {
        document.getElementById("statusRemaining").innerHTML =
          '<span style="color: #4caf50;">Bereits auf Temperatur</span>';
      } else if (remainingHours >= 999) {
        document.getElementById("statusRemaining").innerHTML =
          '<span style="color: #ff9800;">Berechnung nicht möglich</span>';
      } else {
        var remainingSeconds = Math.round(remainingHours * 3600);
        document.getElementById("statusRemaining").textContent =
          formatDuration(remainingSeconds);
      }
    } else if (data.ESTIMATE >= 999) {
      remainingRow.style.display = "none";
    } else if (data.ESTIMATE <= 0) {
      remainingRow.style.display = "table-row";
      document.getElementById("statusRemaining").innerHTML =
        '<span style="color: #4caf50;">Bereits auf Temperatur</span>';
    } else {
      remainingRow.style.display = "none";
    }

    // Display heater status
    const heaterStatus = data.HEATER
      ? '<span style="color: #4caf50; font-weight: bold;">🔥 EIN</span>'
      : '<span style="color: #999;">AUS</span>';
    document.getElementById("statusHeater").innerHTML = heaterStatus;

    // Display status message (heating in progress or temp reading)
    if (data.HEATER) {
      // Heater is on - show heating message
      document.getElementById("statusReadingStateText").innerHTML =
        '<span style="color: #4caf50; font-weight: bold;">🔥 Dein Whirlpool heizt auf.</span>';
      document.getElementById("statusReadingState").style.display = "table-row";
    } else if (data.READING_STATE > 0) {
      // Not heating, but in temp reading mode
      const readingStates = [
        "",
        "Pumpe läuft (20s)",
        "Temperatur wird gelesen",
      ];
      document.getElementById("statusReadingStateText").innerHTML =
        '<span style="color: #2196f3; font-weight: bold;">⚙️ ' +
        readingStates[data.READING_STATE] +
        "</span>";
      document.getElementById("statusReadingState").style.display = "table-row";
    } else {
      document.getElementById("statusReadingState").style.display = "none";
    }

    // Format target time
    if (data.TARGETTIME) {
      const targetDate = new Date(data.TARGETTIME * 1000);
      document.getElementById("statusTargetTime").textContent =
        formatDateTime(targetDate);
    }

    // Format start time
    if (data.STARTTIME && data.STARTTIME > 0) {
      const startDate = new Date(data.STARTTIME * 1000);
      document.getElementById("statusStartTime").textContent =
        formatDateTime(startDate);
    } else {
      document.getElementById("statusStartTime").textContent =
        "Wird berechnet...";
    }

    // Format next check time (or show "completed" status)
    if (data.CHECKCOMPLETED) {
      document.getElementById("statusNextCheck").innerHTML =
        '<span style="color: #4caf50; font-weight: bold;">Prüfung abgeschlossen</span>';
    } else if (data.NEXTCHECK) {
      const nextCheckDate = new Date(data.NEXTCHECK * 1000);
      document.getElementById("statusNextCheck").textContent =
        formatDateTime(nextCheckDate);
    }

    // Calculate and display time remaining
    if (data.TIMEREMAINING) {
      document.getElementById("statusTimeRemaining").textContent =
        formatDuration(data.TIMEREMAINING);
    }

    // Calculate and display time until start
    if (data.STARTTIME && data.STARTTIME > 0) {
      // Only show time until start if a start time has been calculated
      const timeUntilStart = data.TIMEUNTILSTART;
      if (timeUntilStart <= 0 && data.HEATER) {
        // Show "now running" only if heater is actually on
        document.getElementById("statusTimeUntilStart").innerHTML =
          '<span style="color: #4caf50; font-weight: bold;">Jetzt! (Heizung läuft)</span>';
      } else if (timeUntilStart <= 0) {
        // Start time reached but heater not on yet (starting soon)
        document.getElementById("statusTimeUntilStart").innerHTML =
          '<span style="color: #ff9800;">Startet gleich...</span>';
      } else {
        document.getElementById("statusTimeUntilStart").textContent =
          formatDuration(timeUntilStart);
      }
    } else {
      // No start time calculated yet - still measuring
      document.getElementById("statusTimeUntilStart").innerHTML =
        '<span style="color: #2196f3;">Wird berechnet...</span>';
    }
  }
}

/**
 * Format Unix timestamp to readable date/time
 */
function formatDateTime(date) {
  const day = String(date.getDate()).padStart(2, "0");
  const month = String(date.getMonth() + 1).padStart(2, "0");
  const year = date.getFullYear();
  const hours = String(date.getHours()).padStart(2, "0");
  const minutes = String(date.getMinutes()).padStart(2, "0");

  return `${day}.${month}.${year} ${hours}:${minutes} Uhr`;
}

/**
 * Format duration in seconds to human readable format
 */
function formatDuration(seconds) {
  if (seconds < 0) {
    return "In der Vergangenheit";
  }

  const days = Math.floor(seconds / 86400);
  const hours = Math.floor((seconds % 86400) / 3600);
  const minutes = Math.floor((seconds % 3600) / 60);

  let parts = [];
  if (days > 0) parts.push(`${days} Tag${days > 1 ? "e" : ""}`);
  if (hours > 0) parts.push(`${hours} Std.`);
  if (minutes > 0 || parts.length === 0) parts.push(`${minutes} Min.`);

  return parts.join(", ");
}

/**
 * Set a new smart schedule
 */
function setSchedule() {
  // Get form values
  const dateTimeStr = document.getElementById("targetDateTime").value;
  const targetTemp = parseInt(document.getElementById("targetTemp").value);
  const keepHeaterOn = document.getElementById("keepHeaterOn").value === "true";

  // Validate inputs
  if (!dateTimeStr) {
    alert("Bitte gib Datum und Uhrzeit ein.");
    return;
  }

  if (targetTemp < 20 || targetTemp > 40) {
    alert("Die Zieltemperatur muss zwischen 20°C und 40°C liegen.");
    return;
  }

  // Convert datetime-local value to Unix timestamp
  const targetDate = new Date(dateTimeStr);
  const targetTime = Math.floor(targetDate.getTime() / 1000);

  // Check if time is in the future
  const now = Math.floor(Date.now() / 1000);
  if (targetTime <= now) {
    alert("Die Zielzeit muss in der Zukunft liegen.");
    return;
  }

  // Prepare data
  const data = {
    TARGETTIME: targetTime,
    TARGETTEMP: targetTemp,
    KEEPON: keepHeaterOn,
  };

  // Send to backend
  const xhr = new XMLHttpRequest();
  xhr.open("POST", "/setsmartschedule/", true);
  xhr.setRequestHeader("Content-Type", "application/json");

  xhr.onreadystatechange = function () {
    if (xhr.readyState === 4) {
      if (xhr.status === 200) {
        alert(
          "✅ Zeitplan aktiviert!\n\nDas System berechnet jetzt automatisch, wann deine Heizung starten muss.",
        );
        updateScheduleStatus();
      } else {
        alert(
          "❌ Fehler beim Aktivieren.\n\nMögliche Ursachen:\n- Uhrzeit nicht synchronisiert\n- Ungültige Parameter\n- Verbindungsfehler",
        );
      }
    }
  };

  xhr.send(JSON.stringify(data));
}

/**
 * Cancel active schedule
 */
function cancelSchedule() {
  if (!confirm("Möchtest du den Zeitplan wirklich abbrechen?")) {
    return;
  }

  const xhr = new XMLHttpRequest();
  xhr.open("POST", "/cancelsmartschedule/", true);
  xhr.setRequestHeader("Content-Type", "application/json");

  xhr.onreadystatechange = function () {
    if (xhr.readyState === 4) {
      if (xhr.status === 200) {
        alert("✅ Zeitplan erfolgreich abgebrochen.");
        updateScheduleStatus();
      } else {
        alert("❌ Fehler beim Abbrechen des Zeitplans.");
      }
    }
  };

  xhr.send("{}");
}

// Clean up interval when page unloads
window.addEventListener("beforeunload", function () {
  if (scheduleUpdateInterval) {
    clearInterval(scheduleUpdateInterval);
  }
});
