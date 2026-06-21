function topNav() {
  var x = document.getElementById("topnav");
  if (x.className === "topnav") {
    x.className += " responsive";
  } else {
    x.className = "topnav";
  }
}

// Show a Logout link in the nav only when a global-auth session cookie is set
// (cookie is deliberately not HttpOnly so this check works client-side).
document.addEventListener("DOMContentLoaded", function () {
  if (document.cookie.indexOf("WHIRL_SESSION=") === -1) return;
  var nav = document.getElementById("topnav");
  if (!nav) return;
  var a = document.createElement("a");
  a.href = "/logout";
  a.textContent = "Abmelden";
  nav.appendChild(a);
});
function validatePassword(id) {
  var x = document.getElementById(id);
  if (x.value == "<Passwort eingeben>") {
    alert("Bitte Passwort eingeben...");
    return false;
  }
  return true;
}

// Function to update the displayed number
function updateNumber(parent) {
  var parentElement = parent.parentElement;
  var input = parentElement.querySelector(".selectorvalue");
  var numDisplay = parentElement.querySelector(".numDisplay");
  numDisplay.textContent = input.value;
}

function increaseNumber(id) {
  var x = document.getElementById(id);
  var val = Number(x.value);
  var max = Number(x.max);
  if (val < max) {
    val += 1;
    x.value = val;
  }
  updateNumber(x);
}

function decreaseNumber(id) {
  var x = document.getElementById(id);
  var val = Number(x.value);
  var min = Number(x.min);
  if (val > min) {
    val -= 1;
    x.value = val;
  }
  updateNumber(x);
}

function buttonConfirm(elem, text = "", timeout = 3, reset = true) {
  var originalText = elem.innerHTML;

  elem.innerHTML = text == "" ? "&check;" : text;
  elem.disabled = true;

  if (reset) {
    setTimeout(function () {
      elem.innerHTML = originalText;
      elem.disabled = false;
    }, timeout * 1000);
  }
}
