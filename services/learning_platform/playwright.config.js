const { defineConfig } = require("@playwright/test");
const { existsSync } = require("node:fs");
const { resolve } = require("node:path");

const repositoryPython = resolve(__dirname, "../../venv/learning-platform/bin/python");
const python = process.env.JP_ASSIST_TEST_PYTHON || (existsSync(repositoryPython) ? repositoryPython : "python3");
const chromium = process.env.JP_ASSIST_TEST_CHROMIUM;

module.exports = defineConfig({
  testDir: "./tests/browser",
  fullyParallel: false,
  workers: 1,
  retries: 0,
  timeout: 30_000,
  outputDir: "var/playwright-results",
  reporter: [["line"], ["html", { outputFolder: "var/playwright-report", open: "never" }]],
  use: {
    baseURL: "http://127.0.0.1:18766",
    browserName: "chromium",
    viewport: { width: 1440, height: 900 },
    launchOptions: chromium ? { executablePath: chromium } : {},
    screenshot: "only-on-failure",
    trace: "retain-on-failure",
  },
  webServer: {
    command: `${python} scripts/browser_test_server.py --database var/browser-test.sqlite3 --port 18766`,
    url: "http://127.0.0.1:18766/readyz",
    reuseExistingServer: false,
    timeout: 30_000,
    env: { PYTHONPATH: "." },
  },
});
