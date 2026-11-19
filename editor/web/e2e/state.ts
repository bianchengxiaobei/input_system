import { existsSync, readFileSync, rmSync } from "node:fs";
import net from "node:net";
import path from "node:path";
import { fileURLToPath } from "node:url";

export interface E2EState {
  url: string;
  token: string;
  serverPid: number;
  assetDir: string;
  fixturePath: string;
  webDist: string;
  shellExe: string;
  serverExe: string;
}

export const E2E_DIR = path.dirname(fileURLToPath(import.meta.url));
export const WEB_DIR = path.resolve(E2E_DIR, "..");
export const PROJECT_DIR = path.resolve(WEB_DIR, "..", "..");
export const TMP_DIR = path.join(E2E_DIR, ".tmp");
export const STATE_FILE = path.join(TMP_DIR, "state.json");

export function readState(): E2EState {
  if (!existsSync(STATE_FILE)) {
    throw new Error(`E2E state not found at ${STATE_FILE}; run through Playwright`);
  }
  return JSON.parse(readFileSync(STATE_FILE, "utf8")) as E2EState;
}

export async function removeDirectoryWithRetry(
  target: string,
  timeoutMs = 10000,
): Promise<void> {
  const deadline = Date.now() + timeoutMs;
  let lastError: unknown;
  while (Date.now() < deadline) {
    try {
      rmSync(target, { recursive: true, force: true });
      return;
    } catch (error) {
      lastError = error;
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
  }
  if (lastError) {
    throw lastError;
  }
}

export function getFreePort(): Promise<number> {
  return new Promise((resolve, reject) => {
    const server = net.createServer();
    server.once("error", reject);
    server.listen(0, "127.0.0.1", () => {
      const address = server.address();
      if (address === null || typeof address === "string") {
        server.close();
        reject(new Error("could not allocate a local TCP port"));
        return;
      }
      const { port } = address;
      server.close((error) => {
        if (error) {
          reject(error);
        } else {
          resolve(port);
        }
      });
    });
  });
}
