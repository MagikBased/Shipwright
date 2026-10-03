const base = require("@playwright/test");

const test = base.test.extend({
  page: async ({ page }, use, testInfo) => {
    const consoleMessages = [];
    const responseFailures = [];
    page.on("console", message => consoleMessages.push(`[${message.type()}] ${message.text()}`));
    page.on("pageerror", error => consoleMessages.push(`[pageerror] ${error.stack || error.message}`));
    page.on("response", response => {
      if (response.status() >= 400) responseFailures.push(`${response.status()} ${response.request().method()} ${response.url()}`);
    });
    await use(page);
    if (testInfo.status !== testInfo.expectedStatus) {
      if (!page.isClosed()) await testInfo.attach("page.html", { body: await page.content(), contentType: "text/html" });
      await testInfo.attach("browser-console.txt", { body: consoleMessages.join("\n") || "No browser console messages", contentType: "text/plain" });
      await testInfo.attach("failed-responses.txt", { body: responseFailures.join("\n") || "No failed HTTP responses", contentType: "text/plain" });
    }
  },
});

module.exports = { test, expect: base.expect };
