import { copyFileSync, existsSync, readFileSync, writeFileSync } from "node:fs";
import path from "node:path";

import { expect, test, type Page } from "@playwright/test";

import { readState } from "./state";

const state = readState();
const fixture = state.fixturePath;

function prepareAsset(name: string): string {
  const destination = path.join(state.assetDir, name);
  copyFileSync(fixture, destination);
  return destination;
}

async function launch(page: Page): Promise<void> {
  await page.goto(state.url);
  await expect(page.locator(".status-online")).toBeVisible();
  await expect(page.locator(".phase")).toHaveText("E4");
}

async function openAsset(page: Page, file: string): Promise<void> {
  await page.locator(".path-field input").fill(file);
  await page.getByRole("button", { name: "Open", exact: true }).click();
  await expect(page.locator(".statusbar > span").first()).toContainText("opened");
}

test("loads the editor and opens an asset fixture", async ({ page }) => {
  await launch(page);
  await openAsset(page, prepareAsset("open.json"));

  await expect(page.locator(".tree")).toContainText("Gameplay");
  await expect(page.locator(".tree")).toContainText("Fire");
  await expect(page.locator(".tree")).toContainText("<Keyboard>/space");
  await expect(page.locator(".statusbar-right")).toContainText("saved");
  await expect(page.locator(".app")).toHaveAttribute("data-dirty", "false");

  const hostApi = await page.evaluate(() => {
    const host = (
      window as Window & {
        __insysEditorHost?: {
          state(): { dirty: boolean; rawDirty: boolean; autoSave: boolean };
          openAsset(path: string): void;
          save(): void;
        };
      }
    ).__insysEditorHost;
    return host
      ? {
          state: host.state(),
          openAsset: typeof host.openAsset,
          save: typeof host.save,
        }
      : null;
  });
  expect(hostApi).not.toBeNull();
  expect(hostApi?.state).toEqual({
    dirty: false,
    rawDirty: false,
    autoSave: false,
  });
  expect(hostApi?.openAsset).toBe("function");
  expect(hostApi?.save).toBe("function");

  const hostFile = prepareAsset("host-open.json");
  await page.evaluate((file) => {
    (
      window as Window & {
        __insysEditorHost?: { openAsset(path: string): void };
      }
    ).__insysEditorHost?.openAsset(file);
  }, hostFile);
  await expect(page.locator(".statusbar > span").first()).toContainText("opened");
});

test("creates map, action and binding, then persists them", async ({ page }) => {
  const file = prepareAsset("create.json");
  await launch(page);
  await openAsset(page, file);

  await page.getByRole("button", { name: "+ Map", exact: true }).click();
  await expect(page.locator(".tree")).toContainText("New action map");
  await expect(page.locator(".app")).toHaveAttribute("data-dirty", "true");
  const newMapRow = page.locator(".tree-row.map-row", {
    hasText: "New action map",
  });
  await newMapRow.getByTitle("Add action").click();
  const actionRow = page.locator(".tree-row.action-row", {
    hasText: "New action",
  });
  await expect(actionRow).toBeVisible();
  await actionRow.getByTitle("Add binding").click();
  await expect(page.locator(".tree")).toContainText("New binding");

  await page.getByRole("button", { name: "Save", exact: true }).click();
  await expect(page.locator(".statusbar > span").first()).toContainText("saved");
  await expect(page.locator(".app")).toHaveAttribute("data-dirty", "false");

  const saved = JSON.parse(readFileSync(file, "utf8")) as {
    maps: Array<{
      name: string;
      actions: Array<{ name: string }>;
      bindings: Array<{ action: string; path?: string }>;
    }>;
  };
  const map = saved.maps.find((candidate) => candidate.name === "New action map");
  expect(map).toBeDefined();
  expect(map?.actions.some((candidate) => candidate.name === "New action")).toBe(
    true,
  );
  expect(
    map?.bindings.find((candidate) => candidate.action === "New action")?.path,
  ).toBe("<Keyboard>/space");

  await page.getByRole("button", { name: "Reload", exact: true }).click();
  await expect(page.locator(".statusbar > span").first()).toContainText("reloaded");
  await expect(page.locator(".tree")).toContainText("New action map");
  await page.getByRole("button", { name: "Save", exact: true }).click();
  await expect(page.locator(".statusbar > span").first()).toContainText(
    "already up to date",
  );
});

test("auto-saves on blur when enabled", async ({ page }) => {
  const file = prepareAsset("autosave.json");
  await launch(page);
  await openAsset(page, file);

  await page.getByRole("button", { name: "+ Map", exact: true }).click();
  await expect(page.locator(".app")).toHaveAttribute("data-dirty", "true");

  await page.getByLabel("Auto save").check();
  await expect(page.locator(".app")).toHaveAttribute("data-auto-save", "true");
  await expect(page.locator(".statusbar > span").first()).toContainText("saved");
  await expect(page.locator(".app")).toHaveAttribute("data-dirty", "false");

  await page.getByRole("button", { name: "+ Map", exact: true }).click();
  await expect(page.locator(".app")).toHaveAttribute("data-dirty", "true");
  await page.evaluate(() => window.dispatchEvent(new Event("blur")));
  await expect(page.locator(".statusbar > span").first()).toContainText("saved");
  await expect(page.locator(".app")).toHaveAttribute("data-dirty", "false");

  const saved = JSON.parse(readFileSync(file, "utf8")) as {
    maps: Array<{ name: string }>;
  };
  expect(saved.maps.length).toBeGreaterThanOrEqual(2);
});

test("bakes and reopens a read-only binary asset", async ({ page }) => {
  const file = prepareAsset("bake-source.json");
  const binary = path.join(state.assetDir, "bake-source.insysb");
  await launch(page);
  await openAsset(page, file);

  page.once("dialog", (dialog) => {
    expect(dialog.type()).toBe("prompt");
    void dialog.accept(binary);
  });
  await page.getByRole("button", { name: "Bake", exact: true }).click();
  await expect(page.locator(".statusbar > span").first()).toContainText("baked");
  expect(existsSync(binary)).toBe(true);
  expect(readFileSync(binary).byteLength).toBeGreaterThan(32);

  await page.locator(".path-field input").fill(binary);
  await page.getByRole("button", { name: "Open", exact: true }).click();
  await expect(page.locator(".statusbar > span").first()).toContainText("imported");
  await expect(page.locator(".badge-readonly")).toHaveText("read-only");
  await expect(page.locator(".path-field span").first()).toHaveText("Save as");
  await expect(page.locator(".statusbar-right")).toContainText(
    "binary import · read-only",
  );

  await page.locator(".path-field input").fill(binary);
  await expect(
    page.getByRole("button", { name: "Save", exact: true }),
  ).toBeDisabled();
});

test("surfaces an external save conflict", async ({ page }) => {
  const file = prepareAsset("conflict.json");
  await launch(page);
  await openAsset(page, file);

  await page.getByRole("button", { name: "+ Map", exact: true }).click();
  const external = JSON.parse(readFileSync(file, "utf8")) as { name?: string };
  external.name = "External edit";
  writeFileSync(file, `${JSON.stringify(external, null, 2)}\n`);

  await page.getByRole("button", { name: "Save", exact: true }).click();
  await expect(page.locator(".statusbar > span").first()).toContainText(
    "save conflict",
  );
  await expect(page.locator(".statusbar-right")).toContainText("unsaved changes");
});

test("runs preview and forwards driven keyboard input", async ({ page }) => {
  await launch(page);
  await openAsset(page, prepareAsset("preview.json"));

  await page.getByRole("button", { name: "Preview", exact: true }).click();
  await expect(page.locator(".preview-status")).toContainText("running");
  await expect(page.locator(".action-state", { hasText: "Fire" })).toBeVisible();

  await page.getByLabel("Drive input").check();
  const events = page.locator(".event-list .event-row");
  const before = await events.count();
  await page.keyboard.down("Space");
  await expect.poll(() => events.count()).toBeGreaterThan(before);
  await page.keyboard.up("Space");
});
