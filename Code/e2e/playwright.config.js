// @ts-check
const { defineConfig, devices } = require("@playwright/test");

function getModuleUrl() {
  const rawUrl = process.env.MODULE_URL || process.env.WIFIWHIRL_URL;
  if (!rawUrl) {
    throw new Error(
      "Set MODULE_URL before running tests, for example: MODULE_URL=http://192.168.178.42 npm test",
    );
  }

  const withProtocol = /^https?:\/\//i.test(rawUrl)
    ? rawUrl
    : `http://${rawUrl}`;

  const url = new URL(withProtocol);
  url.pathname = url.pathname.replace(/\/+$/, "");
  url.search = "";
  url.hash = "";
  return url.toString().replace(/\/$/, "");
}

const moduleUrl = getModuleUrl();

module.exports = defineConfig({
  testDir: "./tests",
  fullyParallel: false,
  workers: 1,
  timeout: 180 * 1000,
  expect: {
    timeout: 30 * 1000,
  },
  retries: process.env.CI ? 1 : 0,
  reporter: [["list"], ["html", { open: "never" }]],
  use: {
    baseURL: moduleUrl,
    actionTimeout: 30 * 1000,
    navigationTimeout: 60 * 1000,
    screenshot: "only-on-failure",
    trace: "on-first-retry",
    video: "retain-on-failure",
  },
  projects: [
    {
      name: "chromium",
      use: { ...devices["Desktop Chrome"] },
    },
  ],
});
