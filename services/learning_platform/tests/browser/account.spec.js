const { readFile } = require("node:fs/promises");
const AxeBuilder = require("@axe-core/playwright").default;
const { test, expect } = require("./fixtures");

const EMAIL = "learner@example.test";
const PASSWORD = "correct horse battery";

async function login(page, password = PASSWORD) {
  await page.goto("/");
  await page.locator("#login-form input[name=email]").fill(EMAIL);
  await page.locator("#login-form input[name=password]").fill(password);
  await page.locator("#login-form button[type=submit]").click();
  await expect(page.locator("#dashboard")).toBeVisible();
}

async function openView(page, name) {
  await page.locator(`#tabs [data-view=${name}]`).click();
  await expect(page.locator(`#view-${name}`)).toBeVisible();
}

async function downloadedJson(download) {
  return JSON.parse(await readFile(await download.path(), "utf8"));
}

test.describe.serial("learning account", () => {
  test("registers and deletes an isolated account", async ({ page }) => {
    await page.goto("/");
    const form = page.locator("#register-form");
    await form.locator("input[name=displayName]").fill("Disposable Learner");
    await form.locator("input[name=email]").fill("disposable@example.test");
    await form.locator("input[name=password]").fill("disposable safe password");
    await form.locator("button[type=submit]").click();
    await expect(page.locator("#dashboard")).toBeVisible();
    await openView(page, "account");
    await page.locator("#delete-account-form input[name=password]").fill("disposable safe password");
    await page.locator("#delete-account-form button[type=submit]").click();
    await expect(page.locator("#confirm-dialog")).toBeVisible();
    await page.keyboard.press("Escape");
    await expect(page.locator("#confirm-dialog")).toBeHidden();
    await expect(page.locator("#dashboard")).toBeVisible();
    await page.locator("#delete-account-form button[type=submit]").click();
    await page.locator("#confirm-accept").click();
    await expect(page.locator("#auth")).toBeVisible();
  });

  test("keeps the signed-in shell visible when account data temporarily fails", async ({ page }) => {
    await page.goto("/");
    await page.route("**/v1/me/activity", route => route.abort("connectionrefused"));
    await page.locator("#login-form input[name=email]").fill(EMAIL);
    await page.locator("#login-form input[name=password]").fill(PASSWORD);
    await page.locator("#login-form button[type=submit]").click();
    await expect(page.locator("#dashboard")).toBeVisible();
    await expect(page.locator("#auth")).toBeHidden();
    await expect(page.locator("#notice")).toContainText("signed in, but some data could not be loaded");
    await expect(page.locator("#connection-status")).toContainText("Some account data could not be loaded");
    await page.unroute("**/v1/me/activity");
    await page.locator("#connection-status button").click();
    await expect(page.locator("#connection-status")).toBeHidden();
    await expect(page.locator("#stats .stat")).toHaveCount(6);
  });

  test("renders overview accessibly and at supported widths", async ({ page, browser }) => {
    await login(page);
    await expect(page.locator("#stats .stat")).toHaveCount(6);
    await expect(page.locator("#activity-chart .activity-day")).toHaveCount(30);
    await expect(page.locator("#games")).toContainText("test-adventure-a");
    const accessibility = await new AxeBuilder({ page }).analyze();
    expect(accessibility.violations.filter(item => ["critical", "serious"].includes(item.impact))).toEqual([]);

    for (const viewport of [{ width: 390, height: 844 }, { width: 768, height: 1024 }, { width: 1920, height: 1080 }, { width: 2560, height: 1080 }]) {
      const context = await browser.newContext({ viewport });
      const responsivePage = await context.newPage();
      await login(responsivePage);
      const overflow = await responsivePage.evaluate(() => document.documentElement.scrollWidth > document.documentElement.clientWidth);
      expect(overflow, `horizontal overflow at ${viewport.width}x${viewport.height}`).toBe(false);
      await context.close();
    }
  });

  test("keeps every primary view free of serious accessibility violations", async ({ page }) => {
    await login(page);
    const firstTab = page.locator("#tabs button").first();
    await firstTab.focus();
    await page.keyboard.press("Tab");
    await expect(page.locator("#tabs [data-view=vocabulary]")).toBeFocused();
    for (const view of ["overview", "vocabulary", "review", "export", "connections", "account"]) {
      await openView(page, view);
      const accessibility = await new AxeBuilder({ page }).analyze();
      expect(accessibility.violations.filter(item => ["critical", "serious"].includes(item.impact)), view).toEqual([]);
    }
  });

  test("matches the primary-view visual baselines", async ({ page }) => {
    await login(page);
    const masks = [page.locator("#sessions"), page.locator("#devices small")];
    for (const view of ["overview", "vocabulary", "review", "export", "connections", "account"]) {
      await openView(page, view);
      await expect(page).toHaveScreenshot(`${view}.png`, {
        animations: "disabled",
        caret: "hide",
        fullPage: false,
        mask: masks,
        maskColor: "#101b24",
      });
    }
  });

  test("updates daily goals", async ({ page }) => {
    await login(page);
    await page.locator("#goals-form input[name=dailyNewWords]").fill("9");
    await page.locator("#goals-form input[name=dailyReviews]").fill("21");
    await page.locator("#goals-form button[type=submit]").click();
    await expect(page.locator("#notice")).toContainText("Goals saved");
    await page.reload();
    await expect(page.locator("#goals-form input[name=dailyNewWords]")).toHaveValue("9");
    await expect(page.locator("#goals-form input[name=dailyReviews]")).toHaveValue("21");
  });

  test("searches and annotates vocabulary", async ({ page }) => {
    await login(page); await openView(page, "vocabulary");
    await expect(page.locator(".word-card")).toHaveCount(50);
    await expect(page.locator("#load-more-words")).toBeVisible();
    await page.locator("#load-more-words").click();
    await expect(page.locator(".word-card")).toHaveCount(68);
    await expect(page.locator("#load-more-words")).toBeHidden();
    await page.locator("#word-search").fill("forest");
    await expect(page.locator(".word-card")).toHaveCount(1);
    await expect(page.locator(".word-card")).toContainText("森");
    await page.locator(".word-card").click();
    const editor = page.locator("#word-editor");
    await expect(editor.locator("#editor-details")).toContainText("Hand-authored test fixture");
    await expect(editor.locator("#editor-details")).toContainText("test-adventure-a");
    await editor.locator("select[name=learningState]").selectOption("known");
    await editor.locator("input[name=tags]").fill("nature, tested");
    await editor.locator("textarea[name=note]").fill("Browser acceptance note");
    await editor.locator("button[type=submit]").click();
    await page.locator("#word-state").selectOption("known");
    await expect(page.locator(".word-card")).toContainText("tested");
  });

  test("completes a due review", async ({ page }) => {
    await login(page); await openView(page, "review");
    await page.locator("#review-card").click();
    await expect(page.locator("#review-actions")).toBeVisible();
    await expect(page.locator("#review-actions [data-rating='3'] small")).toHaveText(/m|h|d|mo|y/);
    const buriedMeaning = await page.locator("#review-card .meaning").textContent();
    await page.locator("#bury-review").click();
    await page.locator("#review-card").click();
    await expect(page.locator("#review-card .meaning")).not.toHaveText(buriedMeaning);
    const firstMeaning = await page.locator("#review-card .meaning").textContent();
    await page.locator("#review-actions [data-rating='3']").click();
    await page.locator("#review-card").click();
    await expect(page.locator("#review-card .meaning")).not.toHaveText(firstMeaning);
  });

  test("downloads content-neutral manifests and account data", async ({ page }) => {
    await login(page); await openView(page, "export");
    const manifestDownload = page.waitForEvent("download");
    await page.locator("#download-manifest").click();
    const manifest = await downloadedJson(await manifestDownload);
    expect(manifest.savedTokenIds).toContain("森|もり");
    expect(JSON.stringify(manifest)).not.toContain("japanese");
    expect(JSON.stringify(manifest)).not.toContain("english");

    const accountDownload = page.waitForEvent("download");
    await page.locator("#download-account").click();
    const account = await downloadedJson(await accountDownload);
    expect(account.user.email).toBe(EMAIL);
    expect(account.annotations.some(item => item.note === "Browser acceptance note")).toBe(true);
    expect(JSON.stringify(account).toLowerCase()).not.toContain("password_hash");
  });

  test("preflights and idempotently synchronizes stable Anki identities", async ({ page }) => {
    const actions = [];
    const notes = [];
    let modelExists = false;
    let modelFields = ["Word ID", "Word", "Reading", "Meaning", "Part of Speech", "Game"];
    let nextNoteId = 100;
    await page.route("http://127.0.0.1:8765/**", async route => {
      if (route.request().method() === "OPTIONS") {
        await route.fulfill({ status: 204, headers: { "Access-Control-Allow-Origin": "*", "Access-Control-Allow-Headers": "content-type" } });
        return;
      }
      const request = route.request().postDataJSON(); actions.push(request);
      let result = null;
      if (request.action === "modelNames") result = modelExists ? ["JP Assist Vocabulary"] : [];
      else if (request.action === "modelFieldNames") result = modelFields;
      else if (request.action === "findNotes") result = notes.map(note => note.noteId);
      else if (request.action === "notesInfo") result = notes.filter(note => request.params.notes.includes(note.noteId));
      else if (request.action === "createDeck") result = 1;
      else if (request.action === "createModel") { modelExists = true; modelFields = [...request.params.inOrderFields]; result = 1; }
      else if (request.action === "modelFieldAdd") { modelFields.push(request.params.fieldName); }
      else if (request.action === "addNotes") {
        result = request.params.notes.map(note => {
          const noteId = nextNoteId++;
          notes.push({ noteId, modelName: note.modelName, tags: note.tags, fields: Object.fromEntries(Object.entries(note.fields).map(([name, value], order) => [name, { value, order }])) });
          return noteId;
        });
      } else if (request.action === "updateNote") {
        const existing = notes.find(note => note.noteId === request.params.note.id);
        existing.tags = request.params.note.tags;
        for (const [name, value] of Object.entries(request.params.note.fields)) existing.fields[name].value = value;
      }
      await route.fulfill({ json: { result, error: null }, headers: { "Access-Control-Allow-Origin": "*" } });
    });
    await login(page); await openView(page, "export");
    await page.locator("#anki-import").click();
    await expect(page.locator("#anki-preflight")).toContainText("Add");
    await expect(page.locator("#anki-preflight")).toContainText("6");
    await page.locator("#anki-apply").click();
    await expect(page.locator("#notice")).toContainText("6 added, 0 updated, 0 unchanged");
    await expect(page.locator("#anki-preflight")).toContainText("Unchanged");
    await expect(page.locator("#anki-apply")).toBeHidden();
    const addNotes = actions.find(action => action.action === "addNotes");
    const homographs = addNotes.params.notes.filter(note => note.fields.Word === "生");
    expect(homographs).toHaveLength(2);
    expect(new Set(homographs.map(note => note.fields["Word ID"])).size).toBe(2);
    const multipleSenses = addNotes.params.notes.filter(note => note.fields.Word === "橋");
    expect(multipleSenses).toHaveLength(2);
    expect(new Set(multipleSenses.map(note => note.fields["Word ID"])).size).toBe(2);

    notes[0].fields.Meaning.value = "stale definition";
    modelFields = modelFields.filter(field => field !== "Game");
    await page.locator("#anki-import").click();
    await expect(page.locator("#anki-preflight")).toContainText("Update");
    await expect(page.locator("#anki-preflight")).toContainText("Field migrations");
    await page.locator("#anki-apply").click();
    await expect(page.locator("#notice")).toContainText("0 added, 1 updated, 5 unchanged");
    expect(actions.some(action => action.action === "updateNote")).toBe(true);
    expect(actions.some(action => action.action === "modelFieldAdd" && action.params.fieldName === "Game")).toBe(true);

    await page.locator("#anki-owner").selectOption("anki");
    await expect(page.locator("#confirm-dialog")).toBeVisible();
    await page.keyboard.press("Escape");
    await expect(page.locator("#anki-owner")).toHaveValue("jp_assist");
    await page.locator("#anki-owner").selectOption("anki");
    await page.locator("#confirm-accept").click();
    await expect(page.locator("#notice")).toContainText("Anki now owns scheduling");
    await openView(page, "review");
    await expect(page.locator("#review-owner-note")).toContainText("Anki owns scheduling");
    await expect(page.locator("#review-card")).toContainText("Anki owns this review queue");
    await openView(page, "export");
    await page.locator("#anki-owner").selectOption("jp_assist");
    await page.locator("#confirm-accept").click();
    await expect(page.locator("#notice")).toContainText("JP Assist now owns scheduling");
  });

  test("reports Anki identity conflicts, malformed responses, and unavailability", async ({ page }) => {
    let scenario = "duplicates";
    await page.route("http://127.0.0.1:8765/**", async route => {
      if (route.request().method() === "OPTIONS") {
        await route.fulfill({ status: 204, headers: { "Access-Control-Allow-Origin": "*", "Access-Control-Allow-Headers": "content-type" } });
        return;
      }
      if (scenario === "malformed") {
        await route.fulfill({ body: "not-json", headers: { "Access-Control-Allow-Origin": "*", "Content-Type": "application/json" } });
        return;
      }
      if (scenario === "unavailable") {
        await route.abort("connectionrefused");
        return;
      }
      const request = route.request().postDataJSON();
      const fields = Object.fromEntries(["Word ID", "Word", "Reading", "Meaning", "Part of Speech", "Game"].map((name, order) => [name, { value: name === "Word ID" ? "森|もり|sense:forest" : "", order }]));
      const defaults = {
        modelNames: ["JP Assist Vocabulary"],
        modelFieldNames: Object.keys(fields),
        findNotes: [1, 2],
        notesInfo: [{ noteId: 1, fields, tags: ["jp-assist"] }, { noteId: 2, fields, tags: ["jp-assist"] }],
      };
      const result = defaults[request.action];
      await route.fulfill({ json: { result, error: null }, headers: { "Access-Control-Allow-Origin": "*" } });
    });
    await login(page); await openView(page, "export");
    await page.locator("#anki-import").click();
    await expect(page.locator("#anki-preflight")).toContainText("multiple Anki notes");
    await expect(page.locator("#anki-apply")).toBeHidden();
    scenario = "malformed";
    await page.locator("#anki-import").click();
    await expect(page.locator("#notice")).toContainText(/AnkiConnect:.*JSON/i);
    scenario = "unavailable";
    await page.locator("#anki-import").click();
    await expect(page.locator("#notice")).toContainText("AnkiConnect: Failed to fetch");
  });

  test("pairs and revokes account access", async ({ page, request }) => {
    await login(page);
    const pairing = await request.post("/v1/device-pairings", { data: { deviceName: "Browser paired mod", adapterId: "browser-adapter", gameId: "browser-game" } });
    const pairingBody = await pairing.json();
    await openView(page, "connections");
    await page.locator("#pair-form input[name=userCode]").fill(pairingBody.userCode);
    await page.locator("#pair-form button[type=submit]").click();
    await expect(page.locator("#notice")).toContainText("Browser paired mod is approved");
    const claim = await request.post("/v1/device-pairings/token", { data: { deviceCode: pairingBody.deviceCode } });
    expect(claim.ok()).toBe(true);
    await page.reload(); await openView(page, "connections");
    const pairedDevice = page.locator("#devices .card-row").filter({ hasText: "Browser paired mod" });
    await expect(pairedDevice).toBeVisible();
    await pairedDevice.locator("button").click();
    await expect(page.locator("#confirm-dialog")).toBeVisible();
    await page.locator("#confirm-accept").click();
    await expect(page.locator("#notice")).toContainText("Device access revoked");
    const sessionRows = page.locator("#sessions .card-row");
    expect(await sessionRows.count()).toBeGreaterThan(1);
    await sessionRows.filter({ hasText: "Website session" }).first().locator("button").click();
    await page.locator("#confirm-accept").click();
    await expect(page.locator("#notice")).toContainText("Session revoked");
  });

  test("changes password, clears one game, and deletes the account", async ({ page }) => {
    await login(page); await openView(page, "account");
    const passwordForm = page.locator("#password-form");
    await passwordForm.locator("input[name=currentPassword]").fill(PASSWORD);
    await passwordForm.locator("input[name=newPassword]").fill("changed browser password");
    await passwordForm.locator("button[type=submit]").click();
    await expect(page.locator("#notice")).toContainText("Password changed");
    await page.locator("#logout").click();
    await expect(page.locator("#auth")).toBeVisible();
    await login(page, "changed browser password");
    await openView(page, "account");

    await page.locator("#clear-game").selectOption("test-adventure-b");
    await page.locator("#clear-game-form button[type=submit]").click();
    await expect(page.locator("#confirm-message")).toContainText("test-adventure-b");
    await page.locator("#confirm-accept").click();
    await expect(page.locator("#notice")).toContainText("progress cleared");

    await openView(page, "account");
    await page.locator("#delete-account-form input[name=password]").fill("changed browser password");
    await page.locator("#delete-account-form button[type=submit]").click();
    await page.locator("#confirm-accept").click();
    await expect(page.locator("#auth")).toBeVisible();
  });
});
