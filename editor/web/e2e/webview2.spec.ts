import { spawn, spawnSync, type ChildProcess } from "node:child_process";
import {
  copyFileSync,
  existsSync,
  mkdtempSync,
  readFileSync,
} from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";

import {
  chromium,
  expect,
  test,
  type Browser,
  type Page,
} from "@playwright/test";

import {
  getFreePort,
  PROJECT_DIR,
  readState,
  removeDirectoryWithRetry,
} from "./state";

const state = readState();
const skipReason =
  process.platform !== "win32"
    ? "WebView2 shell is only available on Windows"
    : !state.shellExe
      ? "insys_editor.exe is not built"
      : "";

function killTree(pid: number | undefined): void {
  if (!pid) {
    return;
  }
  spawnSync("taskkill", ["/PID", String(pid), "/T", "/F"], {
    windowsHide: true,
    stdio: "ignore",
  });
}

async function waitForFile(file: string, timeoutMs: number): Promise<string> {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (existsSync(file)) {
      const value = readFileSync(file, "utf8").trim();
      if (value) {
        return value;
      }
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw new Error(`timed out waiting for ${file}`);
}

async function waitForCdp(port: number): Promise<void> {
  const deadline = Date.now() + 15000;
  const endpoint = `http://127.0.0.1:${port}/json/version`;
  while (Date.now() < deadline) {
    try {
      const response = await fetch(endpoint);
      if (response.ok) {
        return;
      }
    } catch {
      // WebView2 has not opened the CDP endpoint yet.
    }
    await new Promise((resolve) => setTimeout(resolve, 200));
  }
  throw new Error(`timed out waiting for WebView2 CDP on port ${port}`);
}

test.describe("Windows WebView2 shell", () => {
  test.skip(Boolean(skipReason), skipReason);
  test.setTimeout(60_000);

  let shell: ChildProcess | undefined;
  let browser: Browser | undefined;
  let page: Page | undefined;
  let profileDir = "";
  let assetFile = "";

  test.beforeAll(async () => {
    profileDir = mkdtempSync(path.join(tmpdir(), "insys-webview2-"));
    const urlFile = path.join(profileDir, "url.txt");
    const logFile = path.join(profileDir, "shell.log");
    const cdpPort = await getFreePort();
    assetFile = path.join(state.assetDir, "webview2.json");
    copyFileSync(state.fixturePath, assetFile);

    shell = spawn(
      state.shellExe,
      [
        "--asset-root",
        state.assetDir,
        "--static",
        state.webDist,
        "--user-data-dir",
        profileDir,
        "--remote-debugging-port",
        String(cdpPort),
        "--url-file",
        urlFile,
        "--log",
        logFile,
      ],
      {
        cwd: PROJECT_DIR,
        windowsHide: true,
        stdio: "ignore",
      },
    );
    await waitForFile(urlFile, 20000);
    await waitForCdp(cdpPort);

    browser = await chromium.connectOverCDP(`http://127.0.0.1:${cdpPort}`);
    const context = browser.contexts()[0];
    if (!context) {
      throw new Error("WebView2 CDP exposed no browser context");
    }
    const deadline = Date.now() + 20000;
    while (Date.now() < deadline && !page) {
      page = context
        .pages()
        .find((candidate) => candidate.url().startsWith("http://127.0.0.1"));
      if (!page) {
        await new Promise((resolve) => setTimeout(resolve, 100));
      }
    }
    if (!page) {
      let shellLog = "";
      try {
        shellLog = readFileSync(logFile, "utf8");
      } catch {
        // Keep the timeout error if the log was not written.
      }
      throw new Error(`WebView2 CDP exposed no editor page\n${shellLog}`);
    }
  });

  test.afterAll(async () => {
    try {
      await browser?.close();
    } catch {
      // The WebView2 browser may already be closing with the shell.
    }
    killTree(shell?.pid);
    if (profileDir) {
      try {
        await removeDirectoryWithRetry(profileDir, 2000);
      } catch {
        // Leave the profile in the OS temp directory if WebView2 still holds it.
      }
    }
  });

  test("hosts the editor in WebView2 and saves an asset", async () => {
    if (!page) {
      throw new Error("WebView2 page is unavailable");
    }
    await page.waitForSelector(".toolbar", { timeout: 15000 });
    await expect(page.locator(".status-online")).toBeVisible();
    await expect(page.locator(".phase")).toHaveText("E4");

    await page.locator(".path-field input").fill(assetFile);
    await page.getByRole("button", { name: "Open", exact: true }).click();
    await expect(page.locator(".statusbar > span").first()).toContainText("opened");
    await expect(page.locator(".tree")).toContainText("Gameplay");

    await page.getByRole("button", { name: "+ Map", exact: true }).click();
    await expect(page.locator(".tree")).toContainText("New action map");
    const newMapRow = page.locator(".tree-row.map-row", {
      hasText: "New action map",
    });
    await newMapRow.getByTitle("Add action").click();
    await expect(page.locator(".tree")).toContainText("New action");

    await page.getByRole("button", { name: "Save", exact: true }).click();
    await expect(page.locator(".statusbar > span").first()).toContainText("saved");

    const saved = JSON.parse(readFileSync(assetFile, "utf8")) as {
      maps: Array<{ name: string; actions: Array<{ name: string }> }>;
    };
    const created = saved.maps.find((map) => map.name === "New action map");
    expect(created?.actions.some((action) => action.name === "New action")).toBe(
      true,
    );

    const hostFile = path.join(state.assetDir, "opened-via-host.json");
    copyFileSync(state.fixturePath, hostFile);
    await page.evaluate((targetPath) => {
      const host = (
        window as unknown as {
          chrome: {
            webview: { dispatchEvent(event: MessageEvent<string>): void };
          };
        }
      ).chrome.webview;
      host.dispatchEvent(new MessageEvent("message", { data: targetPath }));
    }, hostFile);
    await expect(page.locator(".statusbar > span").first()).toContainText(
      "opened",
    );
    await expect(page.locator(".path-field input")).toHaveValue(hostFile);
  });
});
