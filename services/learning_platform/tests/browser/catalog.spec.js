const AxeBuilder = require("@axe-core/playwright").default;
const { test, expect } = require("./fixtures");


test.describe("public game catalog", () => {
  test("shows the OoT chapter roadmap and reviewed pilot without an account", async ({ page }) => {
    await page.goto("/catalog");

    await expect(page.locator("#game-title")).toHaveText("Ocarina of Time");
    await expect(page.locator("#chapter-list > li")).toHaveCount(11);
    await expect(page.locator("#chapter-list > li").first()).toContainText("The Boy Without a Fairy");
    await expect(page.locator("#chapter-list > li").first()).toContainText("Pilot content");

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
});
