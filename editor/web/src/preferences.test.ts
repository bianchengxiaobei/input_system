import { describe, expect, it } from "vitest";

import {
  DEFAULT_DIAGNOSTICS_WIDTH,
  DEFAULT_PREVIEW_INTERVAL_MS,
  DEFAULT_TREE_WIDTH,
  addRecentPath,
  clampDiagnosticsWidth,
  clampTreeWidth,
  loadPreferences,
  savePreferences,
  suggestedBinaryPath,
  suggestedJsonPath,
  type PreferenceStorage,
} from "./preferences";

function memoryStorage(initial?: string): PreferenceStorage & { value: string | null } {
  return {
    value: initial ?? null,
    getItem() {
      return this.value;
    },
    setItem(_key, value) {
      this.value = value;
    },
  };
}

describe("editor preferences", () => {
  it("round trips persisted layout and recent files", () => {
    const storage = memoryStorage();
    savePreferences(
      {
        recentPaths: ["assets/player.json"],
        view: "preview",
        treeWidth: 412,
        diagnosticsWidth: 288,
        previewIntervalMs: 100,
        autoSave: true,
      },
      storage,
    );
    expect(loadPreferences(storage)).toEqual({
      recentPaths: ["assets/player.json"],
      view: "preview",
      treeWidth: 412,
      diagnosticsWidth: 288,
      previewIntervalMs: 100,
      autoSave: true,
    });
  });

  it("falls back safely for corrupt or out-of-range values", () => {
    expect(loadPreferences(memoryStorage("{bad json"))).toEqual({
      recentPaths: [],
      view: "inspector",
      treeWidth: DEFAULT_TREE_WIDTH,
      diagnosticsWidth: DEFAULT_DIAGNOSTICS_WIDTH,
      previewIntervalMs: DEFAULT_PREVIEW_INTERVAL_MS,
      autoSave: false,
    });
    expect(clampTreeWidth(10)).toBe(200);
    expect(clampTreeWidth(900)).toBe(520);
    expect(clampDiagnosticsWidth(10)).toBe(220);
    expect(clampDiagnosticsWidth(900)).toBe(560);
  });

  it("deduplicates recent files and keeps the newest first", () => {
    expect(addRecentPath("a.json", ["b.json", "a.json"])).toEqual([
      "a.json",
      "b.json",
    ]);
    expect(addRecentPath("  ", ["a.json"])).toEqual(["a.json"]);
    const many = Array.from({ length: 12 }, (_, index) => `${index}.json`);
    expect(addRecentPath("new.json", many)).toHaveLength(10);
  });

  it("suggests a JSON destination for binary imports", () => {
    expect(suggestedJsonPath("assets/player.insysb")).toBe("assets/player.json");
    expect(suggestedJsonPath("C:\\assets\\player.insysb")).toBe(
      "C:\\assets\\player.json",
    );
    expect(suggestedJsonPath("player")).toBe("player.json");
    expect(suggestedBinaryPath("assets/player.json")).toBe(
      "assets/player.insysb",
    );
  });
});
