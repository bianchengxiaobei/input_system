import { spawn, spawnSync, type ChildProcess } from "node:child_process";
import {
  copyFileSync,
  existsSync,
  mkdirSync,
  readFileSync,
  writeFileSync,
} from "node:fs";
import path from "node:path";
import { createInterface } from "node:readline";

import {
  E2E_DIR,
  PROJECT_DIR,
  STATE_FILE,
  TMP_DIR,
  removeDirectoryWithRetry,
  type E2EState,
} from "./state";

const TOKEN = "playwright-token";

function killTree(pid: number | undefined): void {
  if (!pid) {
    return;
  }
  spawnSync("taskkill", ["/PID", String(pid), "/T", "/F"], {
    windowsHide: true,
    stdio: "ignore",
  });
}

function killPreviousServer(): void {
  if (!existsSync(STATE_FILE)) {
    return;
  }
  try {
    const previous = JSON.parse(readFileSync(STATE_FILE, "utf8")) as E2EState;
    killTree(previous.serverPid);
  } catch {
    // Ignore a corrupt state file; it will be replaced.
  }
}

function findExecutable(envName: string, relative: string): string {
  const configured = process.env[envName];
  const candidate = configured
    ? path.resolve(configured)
    : path.join(PROJECT_DIR, relative);
  if (!existsSync(candidate)) {
    throw new Error(
      `${envName} not found at ${candidate}. Build the editor targets first.`,
    );
  }
  return candidate;
}

function waitForUrl(server: ChildProcess): Promise<string> {
  return new Promise((resolve, reject) => {
    if (!server.stdout) {
      reject(new Error("editor server stdout is not piped"));
      return;
    }
    let stderr = "";
    server.stderr?.on("data", (chunk: Buffer) => {
      stderr += chunk.toString();
    });
    const lines = createInterface({ input: server.stdout });
    const timer = setTimeout(() => {
      lines.close();
      reject(
        new Error(
          `timed out waiting for editor server URL${stderr ? `\n${stderr}` : ""}`,
        ),
      );
    }, 15000);
    const finish = (error: Error | null, url?: string) => {
      clearTimeout(timer);
      lines.close();
      if (error) {
        reject(error);
      } else {
        resolve(url!);
      }
    };
    lines.on("line", (line) => {
      const match = line.match(/listening on (http:\/\/\S+)/);
      if (match) {
        finish(null, match[1]);
      }
    });
    server.once("error", (error) => finish(error));
    server.once("exit", (code) => {
      finish(
        new Error(
          `editor server exited before publishing its URL (code ${code})` +
            (stderr ? `\n${stderr}` : ""),
        ),
      );
    });
  });
}

async function waitForHealth(url: string): Promise<void> {
  const health = new URL(url);
  health.pathname = "/healthz";
  health.search = "";
  const deadline = Date.now() + 10000;
  while (Date.now() < deadline) {
    try {
      const response = await fetch(health);
      if (response.ok) {
        return;
      }
    } catch {
      // The server may have published the URL just before its listen loop
      // starts accepting HTTP requests.
    }
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw new Error(`editor server health check failed at ${health}`);
}

export default async function globalSetup(): Promise<void> {
  killPreviousServer();
  await removeDirectoryWithRetry(TMP_DIR);
  mkdirSync(TMP_DIR, { recursive: true });

  const serverExe = findExecutable(
    "INSYS_E2E_SERVER_EXE",
    "build-editor/Debug/insys_editor_server.exe",
  );
  const shellExe = path.join(PROJECT_DIR, "build-editor", "Debug", "insys_editor.exe");
  const webDist = path.join(E2E_DIR, "..", "dist");
  if (!existsSync(path.join(webDist, "index.html"))) {
    throw new Error(`editor/web/dist is missing; run npm run build first`);
  }

  const assetDir = path.join(TMP_DIR, "assets");
  mkdirSync(assetDir, { recursive: true });
  const fixtureSource = path.join(E2E_DIR, "fixtures", "player.json");
  const fixturePath = path.join(assetDir, "player.json");
  copyFileSync(fixtureSource, fixturePath);

  const server = spawn(
    serverExe,
    [
      "--port",
      "0",
      "--token",
      TOKEN,
      "--static",
      webDist,
      "--asset-root",
      assetDir,
    ],
    {
      cwd: PROJECT_DIR,
      detached: true,
      windowsHide: true,
      stdio: ["ignore", "pipe", "pipe"],
    },
  );

  try {
    const url = await waitForUrl(server);
    await waitForHealth(url);
    const state: E2EState = {
      url,
      token: TOKEN,
      serverPid: server.pid ?? 0,
      assetDir,
      fixturePath,
      webDist,
      shellExe: existsSync(shellExe) ? shellExe : "",
      serverExe,
    };
    writeFileSync(STATE_FILE, `${JSON.stringify(state, null, 2)}\n`);
    server.unref();
    process.stdout.write(`insys E2E server: ${url}\n`);
  } catch (error) {
    killTree(server.pid);
    await removeDirectoryWithRetry(TMP_DIR);
    throw error;
  }
}
