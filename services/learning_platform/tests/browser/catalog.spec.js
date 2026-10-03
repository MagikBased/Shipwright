const AxeBuilder = require("@axe-core/playwright").default;
const { test, expect } = require("./fixtures");


test.describe("public game catalog", () => {
  test("shows the OoT chapter roadmap and reviewed pilot without an account", async ({ page }) => {
    await page.goto("/catalog");

    await expect(page.locator("#game-title")).toHaveText("Ocarina of Time");
    await expect(page.locator("#chapter-list > li")).toHaveCount(11);
    await expect(page.locator("#chapter-list > li").first()).toContainText("The Boy Without a Fairy");
    await expect(page.locator("#chapter-list > li").first()).toContainText("Pilot content");
    await expect(page.locator("#language-profile-title")).toHaveText("What level is this adventure?");
    await expect(page.locator("#jlpt-breakdown .level-row")).toHaveCount(6);
    await expect(page.locator("#language-method")).toContainText("4,158 unique words");
    await expect(page.locator("#coverage-signed-out")).toContainText("Sign in");
    await expect(page.locator("#vocabulary-list > li")).toHaveCount(40);

    await page.locator(".pilot-cards summary").click();
    await expect(page.locator(".pilot-grid article")).toHaveCount(6);
    await expect(page.locator(".pilot-grid article").first()).toContainText("森");
    await expect(page.locator(".pilot-grid article").first()).toContainText("ancient spirit");

    const violations = await new AxeBuilder({ page }).analyze();
    expect(violations.violations).toEqual([]);
  });

  test("links to the catalog from signed-out onboarding", async ({ page }) => {
    await page.goto("/");
    await page.getByRole("link", { name: "Browse the game catalog" }).click();
    await expect(page).toHaveURL(/\/catalog$/);
    await expect(page.locator("#game-title")).toHaveText("Ocarina of Time");
  });

  test("lets an account mark a catalog word known and updates coverage", async ({ page }) => {
    await page.goto("/");
    await page.locator("#register-form input[name=displayName]").fill("Catalog Learner");
    await page.locator("#register-form input[name=email]").fill("catalog-coverage@example.test");
    await page.locator("#register-form input[name=password]").fill("catalog coverage password");
    await page.locator("#register-form button[type=submit]").click();
    await expect(page.locator("#dashboard")).toBeVisible();
    await page.goto("/catalog");
    await expect(page.locator("#coverage-signed-in")).toBeVisible();
    const firstToggle = page.locator(".known-toggle").first();
    await expect(firstToggle).toHaveText("Mark known");
    await firstToggle.click();
    await expect(firstToggle).toHaveText("Known ✓");
    await expect(page.locator("#coverage-count")).toContainText("1 of 4,158");
  });
});
