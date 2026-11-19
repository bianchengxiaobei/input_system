import { describe, expect, it } from "vitest";

import type { AssetJson } from "./asset";
import type { EditorCatalog } from "./catalog";
import {
  compositePartControlType,
  derivedBindings,
  validateControlPath,
} from "./preview";

const catalog: EditorCatalog = {
  layouts: [
    {
      name: "Keyboard",
      displayName: "Keyboard",
      controls: [
        {
          name: "space",
          path: "<Keyboard>/space",
          layout: "Key",
          displayName: "space",
          usages: ["Submit"],
          aliases: [],
        },
      ],
    },
    {
      name: "Gamepad",
      displayName: "Gamepad",
      controls: [
        {
          name: "leftStick",
          path: "<Gamepad>/leftStick",
          layout: "Stick",
          displayName: "leftStick",
          usages: ["Stick"],
          aliases: [],
        },
        {
          name: "buttonSouth",
          path: "<Gamepad>/buttonSouth",
          layout: "Button",
          displayName: "buttonSouth",
          usages: [],
          aliases: ["a"],
        },
      ],
    },
  ],
  devices: ["Keyboard", "Gamepad"],
  controlTypes: ["Button", "Axis", "Stick"],
  composites: [],
  interactions: [],
  processors: [],
};

const asset: AssetJson = {
  version: 1,
  maps: [
    {
      name: "Player",
      actions: [
        { name: "Jump", type: "button", control: "Button" },
        { name: "Move", type: "value", control: "Stick" },
      ],
      bindings: [
        { action: "Jump", path: "<Keyboard>/space" },
        { action: "Move", path: "<Gamepad>/leftStick" },
      ],
    },
    {
      name: "UI",
      actions: [{ name: "Submit", type: "button", control: "Button" }],
      bindings: [{ action: "Submit", path: "<Keyboard>/space" }],
    },
  ],
  schemes: [],
};

describe("preview path validation", () => {
  it("accepts catalog controls and compatible expected types", () => {
    expect(validateControlPath(catalog, "<Keyboard>/space", "Button")).toBeNull();
    expect(validateControlPath(catalog, "<Gamepad>/leftStick", "Stick")).toBeNull();
  });

  it("reports unknown layouts, controls and control type mismatches", () => {
    expect(validateControlPath(catalog, "<Touch>/position", "Vector2")).toContain("Unknown");
    expect(validateControlPath(catalog, "<Keyboard>/missing", "Button")).toContain(
      "No catalog control",
    );
    expect(validateControlPath(catalog, "<Keyboard>/space", "Stick")).toContain(
      "not compatible",
    );
  });

  it("lists derived bindings for the same control path", () => {
    const derived = derivedBindings(asset, 0, 0);
    expect(derived).toHaveLength(1);
    expect(derived[0].map).toBe("UI");
    expect(derived[0].action).toBe("Submit");
  });

  it("chooses capture control types for composite parts", () => {
    expect(compositePartControlType("Vector2")).toBe("");
    expect(compositePartControlType("OneModifier")).toBe("Button");
  });
});
