const { test, expect } = require("@playwright/test");
const os = require("node:os");
const path = require("node:path");

const sizes = [
  { name: "phone", width: 375, height: 812 },
  { name: "tablet", width: 768, height: 1024 },
  { name: "desktop", width: 1440, height: 1000 }
];

for (const size of sizes) {
  test(`${size.name}: renders without errors or overflow`, async ({ page }) => {
    const problems = [];
    page.on("console", message => {
      if (message.type() === "error") problems.push(`console: ${message.text()}`);
    });
    page.on("pageerror", error => problems.push(`pageerror: ${error.message}`));
    page.on("requestfailed", request => problems.push(`request: ${request.url()} ${request.failure()?.errorText || "failed"}`));
    page.on("response", response => {
      if (response.url().startsWith("http://127.0.0.1:4173/") && response.status() >= 400) {
        problems.push(`response: ${response.status()} ${response.url()}`);
      }
    });

    await page.setViewportSize({ width: size.width, height: size.height });
    const response = await page.goto("./", { waitUntil: "networkidle" });
    expect(response.status()).toBe(200);
    await expect(page.locator("h1")).toContainText("Level up your");
    await expect(page.locator("[data-viewer-fallback]")).toBeVisible();

    if (size.name === "phone" || size.name === "desktop") {
      await page.screenshot({ path: path.join(os.tmpdir(), `echodesk-${size.name}.png`), animations: "disabled" });
    }

    const overflow = await page.evaluate(() => document.documentElement.scrollWidth - window.innerWidth);
    expect(overflow).toBeLessThanOrEqual(1);
    expect(problems).toEqual([]);

    if (size.width <= 1100) {
      const toggle = page.locator("[data-menu-toggle]");
      await toggle.click();
      await expect(toggle).toHaveAttribute("aria-expanded", "true");
      await expect(page.locator("[data-mobile-menu]")).toBeVisible();
      await page.locator('[data-mobile-menu] a[href="#utilities"]').click();
      await expect(toggle).toHaveAttribute("aria-expanded", "false");
      await expect(page).toHaveURL(/#utilities$/);
    } else {
      await page.locator('.desktop-nav a[href="#features"]').click();
      await expect(page).toHaveURL(/#features$/);
    }

    const card = page.locator("[data-flip-card]").first();
    await card.scrollIntoViewIfNeeded();
    await card.click();
    await expect(card).toHaveAttribute("aria-pressed", "true");
    await card.press("Space");
    await expect(card).toHaveAttribute("aria-pressed", "false");

    const theme = page.locator("[data-theme-toggle]");
    await theme.click();
    await expect(page.locator("html")).toHaveAttribute("data-theme", /dark|light/);
  });
}

test("headline flips and product visual responds to keyboard", async ({ page }) => {
  await page.setViewportSize({ width: 1440, height: 1000 });
  await page.goto("./", { waitUntil: "networkidle" });
  const word = page.locator("[data-flip-word]");
  const before = await word.textContent();
  await expect(word).not.toHaveText(before, { timeout: 4500 });

  const orbit = page.locator("[data-product-orbit]");
  await orbit.focus();
  await orbit.press("ArrowRight");
  await page.waitForTimeout(250);
  await expect(orbit).toHaveCSS("transform", /matrix/);
});

test("every in-page navigation link has a target", async ({ page }) => {
  await page.goto("./", { waitUntil: "networkidle" });
  const missing = await page.evaluate(() => Array.from(document.querySelectorAll('a[href^="#"]'))
    .map(link => link.getAttribute("href"))
    .filter((href, index, all) => href && all.indexOf(href) === index)
    .filter(href => !document.querySelector(href)));
  expect(missing).toEqual([]);
});

test("setup step uses the USB icon asset", async ({ page }) => {
  await page.goto("./#setup", { waitUntil: "networkidle" });
  const icon = page.locator(".cable-visual img");
  await expect(icon).toBeVisible();
  await expect(icon).toHaveAttribute("src", "assets/icons/usb.svg");
  expect(await icon.evaluate(element => element.complete && element.naturalWidth > 0)).toBe(true);
  await expect(page.locator(".cable-visual i")).toHaveCount(0);
});

test("utility cards use native EchoDesk OS captures", async ({ page }) => {
  await page.goto("./#utilities", { waitUntil: "networkidle" });
  for (const name of ["codex", "clock", "now-playing"]) {
    const capture = page.locator(`[data-os-capture="${name}"]`);
    const image = capture.locator("img");
    await expect(image).toBeVisible();
    await expect(image).toHaveAttribute("src", `./assets/os/${name}.png`);
    expect(await image.evaluate(element => ({
      complete: element.complete,
      width: element.naturalWidth,
      height: element.naturalHeight
    }))).toEqual({ complete: true, width: 320, height: 240 });
  }
});

test("Professional card shows its concept image and elevated status", async ({ page }) => {
  await page.setViewportSize({ width: 920, height: 900 });
  await page.goto("./#professional", { waitUntil: "networkidle" });

  const card = page.locator("#professional");
  const image = card.locator(".pro-visual img");
  const status = card.locator(".pro-status");
  const note = card.locator(".product-note");
  const button = card.locator(".button-disabled");

  await expect(image).toBeVisible();
  expect(await image.evaluate(element => element.complete && element.naturalWidth > 0)).toBe(true);

  const imageBox = await image.boundingBox();
  const statusBox = await status.boundingBox();
  const noteBox = await note.boundingBox();
  const buttonBox = await button.boundingBox();
  expect(statusBox.y).toBeLessThan(imageBox.y + imageBox.height / 2);
  expect(buttonBox.y - (noteBox.y + noteBox.height)).toBeLessThanOrEqual(40);
});

test("core content remains visible without JavaScript", async ({ browser }) => {
  const context = await browser.newContext({
    javaScriptEnabled: false,
    viewport: { width: 1440, height: 1000 }
  });
  const page = await context.newPage();
  await page.goto("http://127.0.0.1:4173/docs/", { waitUntil: "networkidle" });
  await expect(page.locator("#features h2")).toBeVisible();
  await expect(page.locator("#utilities h2")).toBeVisible();
  await expect(page.locator(".reveal").first()).toHaveCSS("opacity", "1");
  await context.close();
});

test("reduced motion keeps content immediately readable", async ({ page }) => {
  await page.emulateMedia({ reducedMotion: "reduce" });
  await page.setViewportSize({ width: 375, height: 812 });
  await page.goto("./", { waitUntil: "networkidle" });
  await expect(page.locator("[data-flip-word]")).toHaveText("Desk");
  await expect(page.locator(".reveal").first()).toHaveCSS("opacity", "1");
  const card = page.locator("[data-flip-card]").first();
  await card.click();
  await expect(card).toHaveAttribute("aria-pressed", "true");
});

for (const size of sizes) {
  test(`${size.name}: download page is accessible and responsive`, async ({ page }) => {
    const problems = [];
    page.on("console", message => {
      if (message.type() === "error") problems.push(`console: ${message.text()}`);
    });
    page.on("pageerror", error => problems.push(`pageerror: ${error.message}`));
    page.on("requestfailed", request => problems.push(`request: ${request.url()} ${request.failure()?.errorText || "failed"}`));

    await page.setViewportSize({ width: size.width, height: size.height });
    const response = await page.goto("./download.html", { waitUntil: "networkidle" });
    expect(response.status()).toBe(200);
    await expect(page.locator("h1")).toContainText("ready on desktop");
    await expect(page.locator('.desktop-nav a[aria-current="page"]')).toHaveText("Download");
    await expect(page.locator(".download-primary")).toHaveAttribute("href", "https://github.com/andomatthew1234/EchoDesk/releases/download/v0.2.7/EchoDesk-Desktop-v0.2.7-win-x64-setup.exe");
    await expect(page.locator(".download-portable")).toHaveAttribute("href", "https://github.com/andomatthew1234/EchoDesk/releases/download/v0.2.7/EchoDesk-Desktop-v0.2.7-win-x64.zip");
    await expect(page.locator(".download-macos")).toHaveAttribute("href", "https://github.com/andomatthew1234/EchoDesk/releases/download/v0.2.7/EchoDesk-Desktop-v0.2.7-macos-universal.dmg");
    await expect(page.locator(".download-macos-zip")).toHaveAttribute("href", "https://github.com/andomatthew1234/EchoDesk/releases/download/v0.2.7/EchoDesk-Desktop-v0.2.7-macos-universal.zip");
    await expect(page.locator("[data-installer-sha256]")).toHaveText(/Pending final|^[A-F0-9]{64}$/);
    await expect(page.locator("[data-portable-sha256]")).toHaveText(/Pending final|^[A-F0-9]{64}$/);
    await expect(page.locator("[data-macos-dmg-sha256]")).toHaveText(/Pending final|^[A-F0-9]{64}$/);
    await expect(page.locator("[data-macos-zip-sha256]")).toHaveText(/Pending final|^[A-F0-9]{64}$/);

    const overflow = await page.evaluate(() => document.documentElement.scrollWidth - window.innerWidth);
    expect(overflow).toBeLessThanOrEqual(1);
    expect(problems).toEqual([]);

    if (size.width <= 1100) {
      await page.locator("[data-menu-toggle]").click();
      await expect(page.locator("[data-mobile-menu]")).toBeVisible();
    }
  });
}

test("home page exposes the Desktop download route", async ({ page }) => {
  await page.goto("./", { waitUntil: "networkidle" });
  await expect(page.locator('.desktop-nav a[href="./download.html"]')).toHaveText("Download Desktop App");
  await expect(page.locator('.software-card a[href="./download.html"]')).toContainText("Download for Windows");
});
