const { defineConfig } = require("@playwright/test");
const os = require("node:os");
const path = require("node:path");

module.exports = defineConfig({
  testDir: __dirname,
  outputDir: path.join(os.tmpdir(), "echodesk-site-playwright"),
  timeout: 30000,
  retries: 0,
  workers: 1,
  reporter: "line",
  use: {
    baseURL: process.env.ECHODESK_SITE_BASE_URL || "http://127.0.0.1:4173/docs/",
    browserName: "chromium",
    launchOptions: {
      executablePath: "C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe"
    },
    screenshot: "only-on-failure",
    trace: "retain-on-failure"
  }
});
