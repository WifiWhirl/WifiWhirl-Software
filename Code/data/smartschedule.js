/**
 * Smart Schedule
 * Handles UI interaction and communication with WifiWhirl backend
 */

let scheduleUpdateInterval = null;

/**
 * Load smart schedule on page load
 */
function loadSmartSchedule() {
  // Set default date to today
  const today = new Date();
  document.getElementById('targetDate').value = today.toISOString().split('T')[0];
  
  // Start updating schedule status
  updateScheduleStatus();
  scheduleUpdateInterval = setInterval(updateScheduleStatus, 5000); // Update every 5 seconds
}

/**
 * Update schedule status display
 */
function updateScheduleStatus() {
  const xhr = new XMLHttpRequest();
  xhr.open('POST', '/getsmartschedule/', true);
  xhr.setRequestHeader('Content-Type', 'application/json');
  
  xhr.onreadystatechange = function() {
    if (xhr.readyState === 4 && xhr.status === 200) {
      try {
        const data = JSON.parse(xhr.responseText);
        displayScheduleStatus(data);
      } catch (e) {
        console.error('Failed to parse schedule data:', e);
      }
    }
  };
  
  xhr.send('{}');
}

/**
 * Display schedule status in UI
 */
function displayScheduleStatus(data) {
  const isActive = data.ACTIVE || false;
  
  // Show/hide sections based on active state
  document.getElementById('statusActive').style.display = isActive ? 'block' : 'none';
  document.getElementById('statusInactive').style.display = isActive ? 'none' : 'block';
  
  if (isActive) {
    // Update status fields
    document.getElementById('statusTargetTemp').textContent = data.TARGETTEMP || '--';
    document.getElementById('statusCurrentTemp').textContent = data.CURRENTTEMP || '--';
    document.getElementById('statusAccurateTemp').textContent = (data.ACCURATETEMP && data.ACCURATETEMP > 0) ? data.ACCURATETEMP + ' °C' : 'Wird gemessen...';
    
    // Display heating estimate in HH:mm format
    if (data.ESTIMATE && data.ESTIMATE > 0) {
      if (data.ESTIMATE >= 999) {
        document.getElementById('statusEstimate').innerHTML = 
          '<span style="color: #ff9800;">Unmöglich zu heizen (Umgebung zu kalt)</span>';
      } else if (data.ESTIMATE_FMT && data.ESTIMATE_FMT.length > 0) {
        // Use pre-formatted HH:mm string from backend
        document.getElementById('statusEstimate').textContent = data.ESTIMATE_FMT + ' Stunden';
      } else {
        // Fallback: format locally
        const totalMinutes = Math.round(data.ESTIMATE * 60);
        const hours = Math.floor(totalMinutes / 60);
        const minutes = totalMinutes % 60;
        const formatted = String(hours).padStart(2, '0') + ':' + String(minutes).padStart(2, '0');
        document.getElementById('statusEstimate').textContent = formatted + ' Stunden';
      }
    } else {
      document.getElementById('statusEstimate').textContent = 'Wird berechnet...';
    }
    
    // Display heater status
    const heaterStatus = data.HEATER ? 
      '<span style="color: #4caf50; font-weight: bold;">🔥 EIN</span>' : 
      '<span style="color: #999;">AUS</span>';
    document.getElementById('statusHeater').innerHTML = heaterStatus;
    
    // Display pump status (if in temp reading mode)
    if (data.READING_STATE > 0) {
      const readingStates = ['', 'Pumpe läuft (20s)', 'Wartet auf Messung (20s)', 'Temperatur wird gelesen'];
      document.getElementById('statusReadingStateText').innerHTML = 
        '<span style="color: #2196f3; font-weight: bold;">⚙️ ' + readingStates[data.READING_STATE] + '</span>';
      document.getElementById('statusReadingState').style.display = 'table-row';
    } else {
      document.getElementById('statusReadingState').style.display = 'none';
    }
    
    // Format target time
    if (data.TARGETTIME) {
      const targetDate = new Date(data.TARGETTIME * 1000);
      document.getElementById('statusTargetTime').textContent = formatDateTime(targetDate);
    }
    
    // Format start time
    if (data.STARTTIME && data.STARTTIME > 0) {
      const startDate = new Date(data.STARTTIME * 1000);
      document.getElementById('statusStartTime').textContent = formatDateTime(startDate);
    } else {
      document.getElementById('statusStartTime').textContent = 'Wird berechnet...';
    }
    
    // Format next check time (or show "completed" status)
    if (data.CHECKCOMPLETED) {
      document.getElementById('statusNextCheck').innerHTML = 
        '<span style="color: #4caf50; font-weight: bold;">Prüfung abgeschlossen</span>';
    } else if (data.NEXTCHECK) {
      const nextCheckDate = new Date(data.NEXTCHECK * 1000);
      document.getElementById('statusNextCheck').textContent = formatDateTime(nextCheckDate);
    }
    
    // Calculate and display time remaining
    if (data.TIMEREMAINING) {
      document.getElementById('statusTimeRemaining').textContent = formatDuration(data.TIMEREMAINING);
    }
    
    // Calculate and display time until start
    if (data.TIMEUNTILSTART) {
      const timeUntilStart = data.TIMEUNTILSTART;
      if (timeUntilStart <= 0) {
        document.getElementById('statusTimeUntilStart').innerHTML = 
          '<span style="color: #4caf50; font-weight: bold;">Jetzt! (Heizung sollte laufen)</span>';
      } else {
        document.getElementById('statusTimeUntilStart').textContent = formatDuration(timeUntilStart);
      }
    }
  }
}

/**
 * Format Unix timestamp to readable date/time
 */
function formatDateTime(date) {
  const day = String(date.getDate()).padStart(2, '0');
  const month = String(date.getMonth() + 1).padStart(2, '0');
  const year = date.getFullYear();
  const hours = String(date.getHours()).padStart(2, '0');
  const minutes = String(date.getMinutes()).padStart(2, '0');
  
  return `${day}.${month}.${year} ${hours}:${minutes} Uhr`;
}

/**
 * Format duration in seconds to human readable format
 */
function formatDuration(seconds) {
  if (seconds < 0) {
    return 'In der Vergangenheit';
  }
  
  const days = Math.floor(seconds / 86400);
  const hours = Math.floor((seconds % 86400) / 3600);
  const minutes = Math.floor((seconds % 3600) / 60);
  
  let parts = [];
  if (days > 0) parts.push(`${days} Tag${days > 1 ? 'e' : ''}`);
  if (hours > 0) parts.push(`${hours} Std.`);
  if (minutes > 0 || parts.length === 0) parts.push(`${minutes} Min.`);
  
  return parts.join(', ');
}

/**
 * Set a new smart schedule
 */
function setSchedule() {
  // Get form values
  const dateStr = document.getElementById('targetDate').value;
  const timeStr = document.getElementById('targetTime').value;
  const targetTemp = parseInt(document.getElementById('targetTemp').value);
  const keepHeaterOn = document.getElementById('keepHeaterOn').value === 'true';
  
  // Validate inputs
  if (!dateStr || !timeStr) {
    alert('Bitte gib das Datum und die Uhrzeit ein.');
    return;
  }
  
  if (targetTemp < 20 || targetTemp > 40) {
    alert('Die Zieltemperatur muss zwischen 20°C und 40°C liegen.');
    return;
  }
  
  // Combine date and time into Unix timestamp
  const dateTimeStr = `${dateStr}T${timeStr}:00`;
  const targetDate = new Date(dateTimeStr);
  const targetTime = Math.floor(targetDate.getTime() / 1000);
  
  // Check if time is in the future
  const now = Math.floor(Date.now() / 1000);
  if (targetTime <= now) {
    alert('Die Zielzeit muss in der Zukunft liegen.');
    return;
  }
  
  // Prepare data
  const data = {
    TARGETTIME: targetTime,
    TARGETTEMP: targetTemp,
    KEEPON: keepHeaterOn
  };
  
  // Send to backend
  const xhr = new XMLHttpRequest();
  xhr.open('POST', '/setsmartschedule/', true);
  xhr.setRequestHeader('Content-Type', 'application/json');
  
  xhr.onreadystatechange = function() {
    if (xhr.readyState === 4) {
      if (xhr.status === 200) {
        alert('✅ Zeitplan aktiviert!\n\nDas System berechnet jetzt automatisch, wann deine Heizung starten muss.');
        updateScheduleStatus();
      } else {
        alert('❌ Fehler beim Aktivieren.\n\nMögliche Ursachen:\n- Uhrzeit nicht synchronisiert\n- Ungültige Parameter\n- Verbindungsfehler');
      }
    }
  };
  
  xhr.send(JSON.stringify(data));
}

/**
 * Cancel active schedule
 */
function cancelSchedule() {
  if (!confirm('Möchtest du den Zeitplan wirklich abbrechen?')) {
    return;
  }
  
  const xhr = new XMLHttpRequest();
  xhr.open('POST', '/cancelsmartschedule/', true);
  xhr.setRequestHeader('Content-Type', 'application/json');
  
  xhr.onreadystatechange = function() {
    if (xhr.readyState === 4) {
      if (xhr.status === 200) {
        alert('✅ Zeitplan erfolgreich abgebrochen.');
        updateScheduleStatus();
      } else {
        alert('❌ Fehler beim Abbrechen des Zeitplans.');
      }
    }
  };
  
  xhr.send('{}');
}

// Clean up interval when page unloads
window.addEventListener('beforeunload', function() {
  if (scheduleUpdateInterval) {
    clearInterval(scheduleUpdateInterval);
  }
});

