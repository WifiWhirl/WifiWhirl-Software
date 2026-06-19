const { test, expect } = require("@playwright/test");

const ESP_SETTLE_MS = Number(process.env.ESP_SETTLE_MS || 1500);
const ESP_HARDWARE_SETTLE_MS = Number(process.env.ESP_HARDWARE_SETTLE_MS || 10000);
const ESP_REQUEST_RETRIES = Number(process.env.ESP_REQUEST_RETRIES || 3);

async function wait(ms) {
  await new Promise((resolve) => setTimeout(resolve, ms));
}

async function postJson(request, path, body = {}) {
  const response = await request.post(path, { data: body });
  expect(response.ok(), `${path} returned ${response.status()}`).toBeTruthy();
  return response.json();
}

async function waitForEspToSettle() {
  await wait(ESP_SETTLE_MS);
}

async function waitForHardwareCommandToApply() {
  await wait(ESP_HARDWARE_SETTLE_MS);
}

function futureTimestamp(secondsFromNow = 7 * 24 * 60 * 60) {
  return Math.floor(Date.now() / 1000) + secondsFromNow;
}

function findCommandIndex(queue, predicate) {
  for (let i = 0; i < queue.LEN; i += 1) {
    const item = {
      cmd: queue.CMD?.[i],
      value: queue.VALUE?.[i],
      xtime: queue.XTIME?.[i],
      interval: queue.INTERVAL?.[i],
      text: queue.TXT?.[i] || "",
    };

    if (predicate(item)) {
      return i;
    }
  }

  return -1;
}

function webhookPath(payload) {
  return `/hook/?send=${encodeURIComponent(JSON.stringify(payload))}`;
}

async function getJsonWithRetry(request, path) {
  let lastError;

  for (let attempt = 1; attempt <= Math.max(ESP_REQUEST_RETRIES, 1); attempt += 1) {
    try {
      const response = await request.get(path, { timeout: 30 * 1000 });
      if (!response.ok()) {
        throw new Error(`${path} returned ${response.status()}`);
      }
      return await response.json();
    } catch (error) {
      lastError = error;
      if (attempt < ESP_REQUEST_RETRIES) {
        await waitForEspToSettle();
      }
    }
  }

  throw lastError;
}

async function getPollData(request) {
  return getJsonWithRetry(request, "/getpolldata/");
}

async function getPollStates(request) {
  const data = await getPollData(request);
  return data[0];
}

async function sendCommand(request, command) {
  const payload = {
    XTIME: 0,
    INTERVAL: 0,
    TXT: "",
    ...command,
  };
  const response = await request.post("/sendcommand/", { data: payload });
  if (response.status() === 409) {
    test.skip(true, "Command queue is full on the target module.");
  }
  expect(response.ok(), `/sendcommand/ returned ${response.status()}`).toBeTruthy();
  await waitForEspToSettle();
  return response.text();
}

async function getWebhookStates(request) {
  return getJsonWithRetry(request, "/getstates/");
}

async function waitForPolledValue(getter, expected, options = {}) {
  const timeout = options.timeout || 90 * 1000;
  const intervals = options.intervals || [3000, 5000, 7000];
  const deadline = Date.now() + timeout;
  let attempt = 0;
  let lastValue;
  let lastError;

  while (Date.now() < deadline) {
    try {
      lastValue = await getter();
      lastError = undefined;
      if (Object.is(lastValue, expected)) {
        return { matched: true, lastValue };
      }
    } catch (error) {
      lastError = error;
    }

    await wait(intervals[Math.min(attempt, intervals.length - 1)]);
    attempt += 1;
  }

  return { matched: false, lastValue, lastError };
}

async function expectPolledValue(getter, expected, message) {
  const result = await waitForPolledValue(getter, expected);
  const lastSeen = result.lastError
    ? `last error: ${result.lastError.message}`
    : `last value: ${JSON.stringify(result.lastValue)}`;
  expect(result.matched, `${message}; ${lastSeen}`).toBeTruthy();
}

async function expectAlertFromEvaluate(page, callback, expectedText) {
  const dialogPromise = page.waitForEvent("dialog");
  const evaluatePromise = page.evaluate(callback);
  const dialog = await dialogPromise;
  expect(dialog.message()).toContain(expectedText);
  await dialog.accept();
  await evaluatePromise;
}

test.describe("WifiWhirl real module smoke tests", () => {
  test("read-only backend endpoints respond with expected shapes", async ({ request }) => {
    const statesResponse = await request.get("/gettemps/");
    expect(
      statesResponse.ok(),
      `/gettemps/ returned ${statesResponse.status()}`,
    ).toBeTruthy();
    const temps = await statesResponse.json();
    expect(temps).toEqual(
      expect.objectContaining({
        currentC: expect.any(Number),
        targetC: expect.any(Number),
        ambientC: expect.any(Number),
        unit: expect.stringMatching(/^[CF]$/),
      }),
    );

    const config = await postJson(request, "/getconfig/");
    expect(config).toEqual(
      expect.objectContaining({
        PRICE: expect.any(Number),
        POOLCAP: expect.any(Number),
      }),
    );

    const schedule = await postJson(request, "/getsmartschedule/");
    expect(schedule).toEqual(
      expect.objectContaining({
        CONTENT: "SMARTSCHEDULE",
        ACTIVE: expect.any(Boolean),
        KEEPON: expect.any(Boolean),
        ESTIMATED_KWH: expect.any(Number),
        ESTIMATED_COST: expect.any(Number),
      }),
    );

    const queue = await postJson(request, "/getcommands/");
    expect(queue).toEqual(
      expect.objectContaining({
        LEN: expect.any(Number),
      }),
    );
    expect(queue.LEN).toBeGreaterThanOrEqual(0);
  });

  test("read-only configuration endpoints expose valid JSON", async ({ request }) => {
    const endpoints = [
      { path: "/getwebconfig/", keys: ["SST", "SSD", "UCS"] },
      { path: "/getmqtt/", keys: ["enableMqtt", "mqttServer", "mqttPort"] },
      { path: "/getwifi/", keys: ["enableAp", "enableWM", "wmApName"] },
      { path: "/gethardware/", keys: ["cio", "dsp", "pcb"] },
    ];

    for (const endpoint of endpoints) {
      const payload = await postJson(request, endpoint.path);
      for (const key of endpoint.keys) {
        expect(payload, `${endpoint.path} missing ${key}`).toHaveProperty(key);
      }
    }
  });

  test("temperature endpoint supports field filtering", async ({ request }) => {
    const response = await request.get("/gettemps/?currentC&targetC");
    expect(response.ok(), `/gettemps/ filter returned ${response.status()}`).toBeTruthy();

    const payload = await response.json();
    expect(payload).toEqual(
      expect.objectContaining({
        currentC: expect.any(Number),
        targetC: expect.any(Number),
      }),
    );
    expect(payload).not.toHaveProperty("ambientC");
    expect(payload).not.toHaveProperty("unit");
  });

  test("polling fallback returns states, times, and other info", async ({ request }) => {
    const payload = await getPollData(request);
    expect(Array.isArray(payload)).toBe(true);
    expect(payload).toHaveLength(3);
    expect(payload[0]).toEqual(
      expect.objectContaining({
        CONTENT: "STATES",
        TGT: expect.any(Number),
        BRT: expect.any(Number),
        LCK: expect.any(Number),
      }),
    );
    expect(payload[1]).toEqual(
      expect.objectContaining({
        CONTENT: "TIMES",
        UPTIME: expect.any(Number),
      }),
    );
    expect(payload[2]).toEqual(
      expect.objectContaining({
        CONTENT: "OTHER",
        FW: expect.any(String),
        IP: expect.any(String),
      }),
    );
  });

  test("webhook status endpoints expose safe read-only state", async ({ request }) => {
    const statesResponse = await request.get("/getstates/");
    expect(
      statesResponse.ok(),
      `/getstates/ returned ${statesResponse.status()}`,
    ).toBeTruthy();
    const states = await statesResponse.json();
    expect(states).toEqual(
      expect.objectContaining({
        pump: expect.any(Boolean),
        heater: expect.any(Boolean),
        bubbles: expect.any(Boolean),
        jets: expect.any(Boolean),
        power: expect.any(Boolean),
        lock: expect.any(Boolean),
      }),
    );

    const tempsResponse = await request.get("/gettemps/");
    expect(
      tempsResponse.ok(),
      `/gettemps/ returned ${tempsResponse.status()}`,
    ).toBeTruthy();
    const temps = await tempsResponse.json();
    expect(temps).toEqual(
      expect.objectContaining({
        currentC: expect.any(Number),
        currentF: expect.any(Number),
        targetC: expect.any(Number),
        targetF: expect.any(Number),
        ambientC: expect.any(Number),
        ambientF: expect.any(Number),
        unit: expect.stringMatching(/^[CF]$/),
      }),
    );
  });

  test("webhook endpoints enforce GET and reject invalid hook payloads", async ({ request }) => {
    for (const path of ["/hook/", "/getstates/", "/gettemps/"]) {
      const response = await request.post(path, { data: {} });
      expect(response.status(), `${path} should reject POST`).toBe(405);
      await expect(response.text()).resolves.toContain("Method not allowed");
      await waitForEspToSettle();
    }

    const missingSend = await request.get("/hook/");
    expect(missingSend.status()).toBe(400);
    await expect(missingSend.text()).resolves.toContain("Error deserializing message");

    const invalidJson = await request.get("/hook/?send=not-json");
    expect(invalidJson.status()).toBe(400);
    await expect(invalidJson.text()).resolves.toContain("Error deserializing message");
    await waitForEspToSettle();
  });

  test("webhook can schedule and clean up a safe future text command", async ({ request }) => {
    const uniqueText = `wh${Date.now().toString(36).slice(-8)}`;
    let activeText = uniqueText;
    const xtime = futureTimestamp(9 * 24 * 60 * 60);

    const hookResponse = await request.get(
      webhookPath({
        CMD: 19,
        VALUE: 1,
        XTIME: xtime,
        INTERVAL: 0,
        TXT: uniqueText,
      }),
    );

    if (hookResponse.status() === 409) {
      test.skip(true, "Command queue is full on the target module.");
    }
    expect(hookResponse.ok(), `/hook/ returned ${hookResponse.status()}`).toBeTruthy();
    await expect(hookResponse.text()).resolves.toContain(`19 1 ${xtime}`);
    await waitForEspToSettle();

    try {
      const queue = await postJson(request, "/getcommands/");
      const index = findCommandIndex(
        queue,
        (item) =>
          item.cmd === 19 &&
          item.value === 1 &&
          item.xtime === xtime &&
          item.text === uniqueText,
      );
      expect(index, "webhook-created text command should be present").toBeGreaterThanOrEqual(0);

      const deleteResponse = await request.post("/delcommand/", {
        data: { IDX: index },
      });
      expect(deleteResponse.ok(), `/delcommand/ returned ${deleteResponse.status()}`).toBeTruthy();
      activeText = "";
      await waitForEspToSettle();

      const queueAfterDelete = await postJson(request, "/getcommands/");
      expect(
        findCommandIndex(
          queueAfterDelete,
          (item) => item.cmd === 19 && item.xtime === xtime && item.text === uniqueText,
        ),
      ).toBe(-1);
    } finally {
      if (activeText) {
        const queue = await postJson(request, "/getcommands/");
        const index = findCommandIndex(
          queue,
          (item) => item.cmd === 19 && item.xtime === xtime && item.text === activeText,
        );
        if (index >= 0) {
          await request.post("/delcommand/", { data: { IDX: index } });
          await waitForEspToSettle();
        }
      }
    }
  });

  test("sendcommand fallback can schedule and clean up a safe future text command", async ({ request }) => {
    const uniqueText = `sc${Date.now().toString(36).slice(-8)}`;
    let activeText = uniqueText;
    const xtime = futureTimestamp(10 * 24 * 60 * 60);

    const responseText = await sendCommand(request, {
      CMD: 19,
      VALUE: 1,
      XTIME: xtime,
      INTERVAL: 0,
      TXT: uniqueText,
    });
    expect(responseText).toContain(`19 1 ${xtime}`);

    try {
      const queue = await postJson(request, "/getcommands/");
      const index = findCommandIndex(
        queue,
        (item) =>
          item.cmd === 19 &&
          item.value === 1 &&
          item.xtime === xtime &&
          item.text === uniqueText,
      );
      expect(index, "sendcommand-created text command should be present").toBeGreaterThanOrEqual(0);

      const deleteResponse = await request.post("/delcommand/", {
        data: { IDX: index },
      });
      expect(deleteResponse.ok(), `/delcommand/ returned ${deleteResponse.status()}`).toBeTruthy();
      activeText = "";
      await waitForEspToSettle();
    } finally {
      if (activeText) {
        const queue = await postJson(request, "/getcommands/");
        const index = findCommandIndex(
          queue,
          (item) => item.cmd === 19 && item.xtime === xtime && item.text === activeText,
        );
        if (index >= 0) {
          await request.post("/delcommand/", { data: { IDX: index } });
          await waitForEspToSettle();
        }
      }
    }
  });

  test("command queue can download and rejects invalid restore payload", async ({ request }) => {
    const downloaded = await postJson(request, "/cmdq_file/", { ACT: "download" });
    expect(downloaded).toEqual(expect.any(Object));

    const invalidUpload = await request.post("/cmdq_file/?action=upload", {
      headers: { "Content-Type": "application/json" },
      data: "not-json",
    });
    expect(invalidUpload.status()).toBe(400);
    await expect(invalidUpload.text()).resolves.toMatch(
      /Ungültiges JSON-Format|Datei muss ein JSON-Objekt enthalten/,
    );
    await waitForEspToSettle();
  });

  test("command queue restore rejects malformed structures", async ({ request }) => {
    const invalidPayloads = [
      {
        name: "missing LEN",
        payload: { CMD: [19], VALUE: [1], XTIME: [futureTimestamp()], INTERVAL: [0], TXT: ["bad"] },
        expected: "Datei fehlt erforderliche Felder",
      },
      {
        name: "array length mismatch",
        payload: { LEN: 2, CMD: [19], VALUE: [1, 1], XTIME: [futureTimestamp(), futureTimestamp()], INTERVAL: [0, 0], TXT: ["bad", "bad2"] },
        expected: "Array-Längen stimmen nicht mit LEN überein",
      },
      {
        name: "too many commands",
        payload: {
          LEN: 21,
          CMD: Array(21).fill(19),
          VALUE: Array(21).fill(1),
          XTIME: Array(21).fill(futureTimestamp()),
          INTERVAL: Array(21).fill(0),
          TXT: Array(21).fill("bad"),
        },
        expected: "Warteschlange enthält zu viele Befehle",
      },
      {
        name: "invalid TXT shape",
        payload: { LEN: 1, CMD: [19], VALUE: [1], XTIME: [futureTimestamp()], INTERVAL: [0], TXT: "bad" },
        expected: "TXT muss ein Array sein",
      },
    ];

    for (const item of invalidPayloads) {
      const response = await request.post("/cmdq_file/?action=upload", {
        headers: { "Content-Type": "application/json" },
        data: JSON.stringify(item.payload),
      });
      expect(response.status(), `${item.name} should be rejected`).toBe(400);
      await expect(response.text()).resolves.toContain(item.expected);
      await waitForEspToSettle();
    }
  });

  test("smart schedule API rejects invalid updates without creating schedules", async ({ request }) => {
    const pastSchedule = await request.post("/setsmartschedule/", {
      data: {
        TARGETTIME: Math.floor(Date.now() / 1000) - 3600,
        TARGETTEMP: 37,
        KEEPON: false,
      },
    });
    expect(pastSchedule.status()).toBe(400);
    await waitForEspToSettle();

    const invalidTemp = await request.post("/setsmartschedule/", {
      data: {
        TARGETTIME: futureTimestamp(),
        TARGETTEMP: 99,
        KEEPON: false,
      },
    });
    expect(invalidTemp.status()).toBe(400);
    await waitForEspToSettle();

    const missingKeepOn = await request.post("/updatesmartschedule/", {
      data: {},
    });
    expect(missingKeepOn.status()).toBe(400);
    await expect(missingKeepOn.text()).resolves.toContain("Missing KEEPON");
    await waitForEspToSettle();
  });

  test("target temperature and brightness can be changed and restored", async ({ request }) => {
    const originalStates = await getPollStates(request);
    const originalTarget = originalStates.TGT;
    const originalBrightness = originalStates.BRT;
    const newTarget = originalStates.UNT
      ? originalTarget < 40 ? originalTarget + 1 : originalTarget - 1
      : originalTarget < 104 ? originalTarget + 1 : originalTarget - 1;
    const newBrightness = originalBrightness === 7 ? 8 : 7;
    let targetChanged = false;
    let brightnessChanged = false;

    try {
      await sendCommand(request, { CMD: 0, VALUE: newTarget });
      await waitForHardwareCommandToApply();
      targetChanged = true;
      const targetResult = await waitForPolledValue(
        async () => (await getPollStates(request)).TGT,
        newTarget,
      );
      expect(
        targetResult.matched,
        `Target temperature command was accepted, but the module still reports ${JSON.stringify(targetResult.lastValue)}.`,
      ).toBeTruthy();

      await sendCommand(request, { CMD: 12, VALUE: newBrightness });
      await waitForHardwareCommandToApply();
      brightnessChanged = true;
      const brightnessResult = await waitForPolledValue(
        async () => (await getPollStates(request)).BRT,
        newBrightness,
      );
      expect(
        brightnessResult.matched,
        `Brightness command was accepted, but the module still reports ${JSON.stringify(brightnessResult.lastValue)}.`,
      ).toBeTruthy();
    } finally {
      if (targetChanged) {
        await sendCommand(request, { CMD: 0, VALUE: originalTarget });
        await waitForHardwareCommandToApply();
        await expectPolledValue(
          async () => (await getPollStates(request)).TGT,
          originalTarget,
          "target temperature should be restored",
        );
      }

      if (brightnessChanged) {
        await sendCommand(request, { CMD: 12, VALUE: originalBrightness });
        await waitForHardwareCommandToApply();
        await expectPolledValue(
          async () => (await getPollStates(request)).BRT,
          originalBrightness,
          "brightness should be restored",
        );
      }
    }
  });

  test("lock can be toggled and restored", async ({ request }) => {
    const originalLock = Boolean((await getWebhookStates(request)).lock);
    let lockChanged = false;

    try {
      await sendCommand(request, { CMD: 23, VALUE: 1 });
      await waitForHardwareCommandToApply();
      lockChanged = true;
      const lockResult = await waitForPolledValue(
        async () => Boolean((await getWebhookStates(request)).lock),
        !originalLock,
      );
      expect(
        lockResult.matched,
        `Lock command was accepted, but the module still reports ${JSON.stringify(lockResult.lastValue)}.`,
      ).toBeTruthy();
    } finally {
      const currentLock = Boolean((await getWebhookStates(request)).lock);
      if (currentLock !== originalLock) {
        await sendCommand(request, { CMD: 23, VALUE: 1 });
        await waitForHardwareCommandToApply();
      }
      if (lockChanged || currentLock !== originalLock) {
        await expectPolledValue(
          async () => Boolean((await getWebhookStates(request)).lock),
          originalLock,
          "lock should be restored",
        );
      }
    }
  });

  test("safe future text command can be added, edited, and deleted", async ({ request }) => {
    const uniqueText = `e2e${Date.now().toString(36).slice(-8)}`;
    const editedText = `${uniqueText}x`;
    let activeText = uniqueText;

    const addResponse = await request.post("/addcommand/", {
      data: {
        CMD: 19,
        VALUE: 1,
        XTIME: futureTimestamp(),
        INTERVAL: 0,
        TXT: uniqueText,
      },
    });

    if (addResponse.status() === 409) {
      test.skip(true, "Command queue is full on the target module.");
    }
    expect(addResponse.ok(), `/addcommand/ returned ${addResponse.status()}`).toBeTruthy();
    await waitForEspToSettle();

    try {
      let queue = await postJson(request, "/getcommands/");
      let index = findCommandIndex(
        queue,
        (item) => item.cmd === 19 && item.text === uniqueText,
      );
      expect(index, "added text command should be present").toBeGreaterThanOrEqual(0);

      const editResponse = await request.post("/editcommand/", {
        data: {
          IDX: index,
          CMD: 19,
          VALUE: 1,
          XTIME: futureTimestamp(8 * 24 * 60 * 60),
          INTERVAL: 0,
          TXT: editedText,
        },
      });
      expect(editResponse.ok(), `/editcommand/ returned ${editResponse.status()}`).toBeTruthy();
      activeText = editedText;
      await waitForEspToSettle();

      queue = await postJson(request, "/getcommands/");
      index = findCommandIndex(
        queue,
        (item) => item.cmd === 19 && item.text === editedText,
      );
      expect(index, "edited text command should be present").toBeGreaterThanOrEqual(0);

      const deleteResponse = await request.post("/delcommand/", {
        data: { IDX: index },
      });
      expect(deleteResponse.ok(), `/delcommand/ returned ${deleteResponse.status()}`).toBeTruthy();
      activeText = "";
      await waitForEspToSettle();

      queue = await postJson(request, "/getcommands/");
      expect(
        findCommandIndex(queue, (item) => item.cmd === 19 && item.text === editedText),
      ).toBe(-1);
    } finally {
      if (activeText) {
        const queue = await postJson(request, "/getcommands/");
        const index = findCommandIndex(
          queue,
          (item) => item.cmd === 19 && item.text === activeText,
        );
        if (index >= 0) {
          await request.post("/delcommand/", { data: { IDX: index } });
          await waitForEspToSettle();
        }
      }
    }
  });

  test("dashboard and navigation pages load without unsafe control actions", async ({ page }) => {
    const pages = [
      { path: "/", title: "WifiWhirl" },
      { path: "/config.html", title: "SPA-Konfiguration" },
      { path: "/automation.html", title: "Automatisierung" },
      { path: "/smartschedule.html", title: "Smart Schedule" },
      { path: "/webconfig.html", title: "Web-Konfiguration" },
      { path: "/wifi.html", title: "Netzwerkkonfiguration" },
      { path: "/hwconfig.html", title: "Pumpenkonfiguration" },
      { path: "/info.html", title: "Softwareinfo" },
    ];

    for (const item of pages) {
      await page.goto(item.path, { waitUntil: "domcontentloaded" });
      await expect(page.locator("#site")).toBeVisible();
      await expect(page.locator("#header")).toContainText(item.title);
    }
  });

  test("smart schedule exposes cost estimate and editable heater target behavior", async ({ page }) => {
    await page.goto("/smartschedule.html", { waitUntil: "domcontentloaded" });

    await expect(page.locator("#statusCost")).toBeAttached();
    await expect(page.locator("#activeKeepHeaterOn")).toBeAttached();
    await expect(page.locator("#keepHeaterOn")).toBeVisible();

    await expect(page.locator("#keepHeaterOn option")).toHaveText([
      "Heizung ausschalten",
      "Heizung eingeschaltet lassen",
    ]);
  });

  test("smart schedule client-side validation blocks unsafe submissions", async ({ page }) => {
    await page.goto("/smartschedule.html", { waitUntil: "domcontentloaded" });

    await page.locator("#targetDateTime").fill("");
    await expectAlertFromEvaluate(
      page,
      () => window.setSchedule(),
      "Datum und Uhrzeit",
    );

    await page.locator("#targetDateTime").fill("2099-01-01T19:00");
    await page.locator("#targetTemp").fill("99");
    await expectAlertFromEvaluate(
      page,
      () => window.setSchedule(),
      "zwischen 20°C und 40°C",
    );

    await page.locator("#targetDateTime").fill("2000-01-01T19:00");
    await page.locator("#targetTemp").fill("37");
    await expectAlertFromEvaluate(
      page,
      () => window.setSchedule(),
      "Zielzeit muss in der Zukunft",
    );
  });

  test("automation UI uses the safe Bedientasten sperren wording", async ({ page }) => {
    await page.goto("/automation.html", { waitUntil: "domcontentloaded" });

    const commandSelect = page.locator("#commands");
    await expect(commandSelect).toBeVisible();
    await expect(commandSelect.locator('option[value="26"]')).toHaveText(
      "Bedientasten sperren",
    );

    await commandSelect.selectOption("26");
    await expect(page.locator("#vallabel")).toHaveText("Bedientasten:");
    await expect(page.locator("#onoffinfo")).toHaveText("sperren");
    await expect(page.locator("#offinfo")).toHaveText("entsperren");
  });

  test("automation form serializes safe commands without hardware command IDs", async ({ page }) => {
    await page.goto("/automation.html", { waitUntil: "domcontentloaded" });

    await page.locator("#commands").selectOption("26");
    const lockButtonsPayload = await page.evaluate(() => window.getCommandJson());
    expect(lockButtonsPayload).toEqual(
      expect.objectContaining({
        CMD: 26,
        VALUE: 0,
      }),
    );

    await page.locator("#valoff").check();
    const unlockButtonsPayload = await page.evaluate(() => window.getCommandJson());
    expect(unlockButtonsPayload).toEqual(
      expect.objectContaining({
        CMD: 26,
        VALUE: 1,
      }),
    );

    await page.locator("#commands").selectOption("19");
    await page.locator("#txt").fill("e2etest");
    const textPayload = await page.evaluate(() => window.getCommandJson());
    expect(textPayload).toEqual(
      expect.objectContaining({
        CMD: 19,
        TXT: "e2etest",
      }),
    );
  });

  test("dark mode toggle persists locally without module writes", async ({ page }) => {
    await page.goto("/", { waitUntil: "domcontentloaded" });

    const toggle = page.locator("#darkModeToggle");
    await expect(toggle).toBeAttached();
    await toggle.evaluate((el) => el.click());
    await expect(page.locator("html")).toHaveClass(/darkmode/);

    await page.reload({ waitUntil: "domcontentloaded" });
    await expect(page.locator("html")).toHaveClass(/darkmode/);

    await page.locator("#darkModeToggle").evaluate((el) => el.click());
    await expect(page.locator("html")).not.toHaveClass(/darkmode/);
  });

  test("web config localStorage controls dashboard section visibility without module writes", async ({ page }) => {
    await page.route("**/getwebconfig/", async (route) => {
      await route.fulfill({
        status: 200,
        contentType: "application/json",
        body: JSON.stringify({
          SST: false,
          SSD: true,
          SSC: true,
          SSB: true,
          SSTIM: true,
          SSTOT: true,
          SSEN: true,
          SSWQ: true,
          SWQCYA: false,
          SWQALK: false,
          UCS: true,
        }),
      });
    });
    await page.route("**/setwebconfig/", async (route) => {
      await route.fulfill({ status: 200, contentType: "text/plain", body: "" });
    });

    await page.goto("/webconfig.html", { waitUntil: "domcontentloaded" });
    await page.locator("#showSectionTemperature").setChecked(false);
    await page.evaluate(() => window.saveWebConfig());
    await expect
      .poll(() => page.evaluate(() => localStorage.getItem("showSectionTemperature")))
      .toBe("false");

    await page.goto("/", { waitUntil: "domcontentloaded" });
    await expect(page.locator("#sectionTemperature")).toBeHidden();

    await page.evaluate(() => {
      localStorage.removeItem("showSectionTemperature");
    });
  });

  test("mobile navigation menu opens and closes", async ({ page }) => {
    await page.setViewportSize({ width: 390, height: 844 });
    await page.goto("/", { waitUntil: "domcontentloaded" });

    const nav = page.locator("#topnav");
    await expect(nav).toHaveClass("topnav");

    await page.locator(".topnavicon").click();
    await expect(nav).toHaveClass(/responsive/);

    await page.locator(".topnavicon").click();
    await expect(nav).toHaveClass("topnav");
  });

  test("core pages do not overflow on desktop or mobile viewports", async ({ page }) => {
    const pages = ["/", "/config.html", "/automation.html", "/smartschedule.html", "/webconfig.html", "/wifi.html"];
    const viewports = [
      { width: 390, height: 844 },
      { width: 1280, height: 900 },
    ];

    for (const viewport of viewports) {
      await page.setViewportSize(viewport);
      for (const path of pages) {
        await page.goto(path, { waitUntil: "domcontentloaded" });
        const overflow = await page.evaluate(() => {
          const root = document.documentElement;
          return root.scrollWidth - root.clientWidth;
        });
        expect(overflow, `${path} overflows at ${viewport.width}px`).toBeLessThanOrEqual(2);
      }
    }
  });

  test("static assets and manifest are served", async ({ request }) => {
    const manifest = await request.get("/manifest.json");
    expect(manifest.ok(), `/manifest.json returned ${manifest.status()}`).toBeTruthy();
    const manifestBody = await manifest.json();
    expect(manifestBody).toEqual(
      expect.objectContaining({
        name: expect.any(String),
      }),
    );

    for (const asset of ["/main.css", "/function.js", "/logo.png"]) {
      const response = await request.get(asset);
      expect(response.ok(), `${asset} returned ${response.status()}`).toBeTruthy();
      expect(Number(response.headers()["content-length"] || 1)).toBeGreaterThan(0);
    }
  });

  test("system info assets and prometheus metrics are served", async ({ request }) => {
    const infoPage = await request.get("/info.html");
    expect(infoPage.ok(), `/info.html returned ${infoPage.status()}`).toBeTruthy();
    await expect(infoPage.text()).resolves.toContain("Softwareinfo");

    for (const asset of ["/favicon.ico", "/Roboto-Regular.woff", "/Roboto-Regular.eot", "/furelise.mel", "/popcorn.mel"]) {
      const response = await request.get(asset);
      expect(response.ok(), `${asset} returned ${response.status()}`).toBeTruthy();
      expect(Number(response.headers()["content-length"] || 1)).toBeGreaterThan(0);
    }

    const metrics = await request.get("/metrics");
    expect(metrics.ok(), `/metrics returned ${metrics.status()}`).toBeTruthy();
    const metricsText = await metrics.text();
    expect(metricsText).toContain("# HELP");
    expect(metricsText).toContain("_info");
    expect(metricsText).toContain("_temperature_celcius");
  });

  test("configuration pages use alternating section styling", async ({ page }) => {
    const pages = [
      "/config.html",
      "/automation.html",
      "/smartschedule.html",
      "/webconfig.html",
      "/wifi.html",
      "/hwconfig.html",
    ];

    for (const path of pages) {
      await page.goto(path, { waitUntil: "domcontentloaded" });
      await expect(page.locator("section.odd-section").first()).toBeVisible();
    }
  });
});
