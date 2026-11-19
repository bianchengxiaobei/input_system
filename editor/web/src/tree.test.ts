import { describe, expect, it } from "vitest";

import type { AssetJson } from "./asset";
import {
  TREE_ROW_HEIGHT,
  buildTreeRows,
  visibleTreeRange,
} from "./TreePanel";

function largeAsset(bindingCount: number): AssetJson {
  return {
    version: 1,
    name: "Large",
    maps: [
      {
        name: "Gameplay",
        actions: [{ name: "Move", id: "a", type: "value" }],
        bindings: Array.from({ length: bindingCount }, (_, index) => ({
          action: "Move",
          id: `b${index}`,
          path: `<Keyboard>/key${index}`,
        })),
      },
    ],
    schemes: [],
  };
}

describe("virtual asset tree", () => {
  it("flattens 1000 bindings into a bounded visible window", () => {
    const rows = buildTreeRows(largeAsset(1000));
    expect(rows.length).toBe(1003);
    expect(rows.filter((row) => row.kind === "binding")).toHaveLength(1000);
    const range = visibleTreeRange(rows.length, 34 * 700, 480);
    expect(range.start).toBeGreaterThan(600);
    expect(range.end - range.start).toBeLessThan(40);
    expect(range.end).toBeLessThan(rows.length);
    expect(rows.length * TREE_ROW_HEIGHT).toBeGreaterThan(30000);
  });

  it("keeps empty actions visible as an explicit row", () => {
    const asset = largeAsset(0);
    const rows = buildTreeRows(asset);
    expect(rows.some((row) => row.kind === "empty-binding")).toBe(true);
  });
});
