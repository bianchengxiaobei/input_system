import { spawnSync } from "node:child_process";
import { existsSync, readFileSync } from "node:fs";

import {
  STATE_FILE,
  TMP_DIR,
  removeDirectoryWithRetry,
  type E2EState,
} from "./state";

export default async function globalTeardown(): Promise<void> {
  if (existsSync(STATE_FILE)) {
    try {
      const state = JSON.parse(readFileSync(STATE_FILE, "utf8")) as E2EState;
      if (state.serverPid) {
        spawnSync(
          "taskkill",
          ["/PID", String(state.serverPid), "/T", "/F"],
          { windowsHide: true, stdio: "ignore" },
        );
      }
    } catch {
      // The test runner may have already removed the state.
    }
  }
  try {
    await removeDirectoryWithRetry(TMP_DIR);
  } catch (error) {
    console.warn(`could not remove ${TMP_DIR}: ${String(error)}`);
  }
}
