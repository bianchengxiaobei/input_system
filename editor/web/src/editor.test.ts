import { describe, expect, it } from "vitest";

import {
  formatDescriptor,
  parseAsset,
  parseDescriptor,
  serializeAsset,
  uniqueName,
} from "./asset";
import type { EditorCatalog } from "./catalog";
import {
  addAction,
  addActionMap,
  addBinding,
  addCompositeBinding,
  addControlScheme,
  changeCompositeType,
  duplicateAction,
  setBindingScheme,
  updateControlScheme,
} from "./commands";
import { localDiagnostics } from "./validation";

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
          displayName: "Space",
          usages: [],
          aliases: [],
        },
      ],
    },
  ],
  devices: ["Keyboard", "Gamepad"],
  controlTypes: ["Any", "Button", "Key", "Vector2"],
  composites: [
    {
      name: "Vector2",
      displayName: "Vector 2",
      parts: ["up", "down", "left", "right"],
      parameters: [
        { name: "mode", displayName: "Mode", type: "enum", default: "0", options: [] },
      ],
    },
    {
      name: "Axis",
      displayName: "Axis",
      parts: ["negative", "positive"],
      parameters: [],
    },
  ],
  interactions: [{ name: "Tap", displayName: "Tap", parameters: [] }],
  processors: [{ name: "Invert", displayName: "Invert", parameters: [] }],
};

function newDocument() {
  return parseAsset('{"version":1,"maps":[]}');
}

describe("asset serialization", () => {
  it("round trips canonical JSON fields", () => {
    const asset = parseAsset(
      JSON.stringify({
        version: 1,
        name: "Player",
        maps: [
          {
            name: "Gameplay",
            actions: [
              {
                name: "Jump",
                id: "00000000000000000000000000000001",
                type: "button",
                control: "Button",
                priority: 2,
              },
            ],
            bindings: [
              {
                action: "Jump",
                id: "00000000000000000000000000000002",
                path: "<Keyboard>/space",
                groups: "Keyboard",
              },
            ],
          },
        ],
        schemes: [
          {
            name: "Keyboard",
            id: "00000000000000000000000000000003",
            bindingGroup: "Keyboard",
            devices: ["Keyboard"],
          },
        ],
      }),
    );
    const text = serializeAsset(asset);
    expect(text).toContain('"priority": 2');
    expect(text).toContain('"bindingGroup": "Keyboard"');
    const again = parseAsset(text);
    expect(again.maps[0].actions[0].name).toBe("Jump");
    expect(again.schemes?.[0].devices).toEqual(["Keyboard"]);
  });

  it("keeps a stable diff-friendly key order", () => {
    const text = serializeAsset(
      parseAsset(
        JSON.stringify({
          version: 1,
          name: "Stable",
          maps: [
            {
              name: "Gameplay",
              actions: [{ name: "Jump", id: "00000000000000000000000000000001" }],
              bindings: [
                {
                  action: "Jump",
                  id: "00000000000000000000000000000002",
                  path: "<Keyboard>/space",
                },
              ],
            },
          ],
        }),
      ),
    );
    expect(text.indexOf('"version"')).toBeLessThan(text.indexOf('"name"'));
    expect(text.indexOf('"name": "Gameplay"')).toBeLessThan(
      text.indexOf('"actions"'),
    );
    expect(text.indexOf('"actions"')).toBeLessThan(text.indexOf('"bindings"'));
    expect(serializeAsset(parseAsset(text))).toBe(text);
  });

  it("parses and formats parameter descriptors", () => {
    const descriptor = parseDescriptor("Vector2(mode=1, normalize=true)");
    expect(descriptor.name).toBe("Vector2");
    expect(descriptor.parameters.mode).toBe("1");
    expect(formatDescriptor(descriptor.name, descriptor.parameters)).toBe(
      "Vector2(mode=1,normalize=true)",
    );
  });

  it("makes names unique using Unity-style suffixes", () => {
    expect(uniqueName("Jump", [])).toBe("Jump");
    expect(uniqueName("Jump", ["Jump"])).toBe("Jump1");
    expect(uniqueName("Jump1", ["Jump", "Jump1"])).toBe("Jump2");
  });

  it("reports unsupported schemas with a migration hint", () => {
    expect(() => parseAsset('{"version":2,"maps":[]}')).toThrow(
      /migration required/,
    );
  });
});

describe("editor commands", () => {
  it("creates maps actions bindings and composites", () => {
    let result = addActionMap(newDocument());
    result = addAction(result.asset, 0);
    const actionName = result.asset.maps[0].actions[0].name;
    result = addBinding(result.asset, 0, actionName);
    result = addCompositeBinding(
      result.asset,
      0,
      actionName,
      "Vector2",
      {
        up: "<Keyboard>/space",
        down: "<Keyboard>/space",
        left: "<Keyboard>/space",
        right: "<Keyboard>/space",
      },
    );
    const parsed = parseAsset(serializeAsset(result.asset));
    expect(parsed.maps).toHaveLength(1);
    expect(parsed.maps[0].actions).toHaveLength(1);
    expect(parsed.maps[0].bindings).toHaveLength(2);
    expect(parsed.maps[0].bindings[1].composite).toContain("Vector2");
  });

  it("duplicates an action with fresh ids and binding references", () => {
    let result = addActionMap(newDocument());
    result = addAction(result.asset, 0);
    result = addBinding(result.asset, 0, result.asset.maps[0].actions[0].name);
    result = duplicateAction(result.asset, 0, 0);
    const map = result.asset.maps[0];
    expect(map.actions.map((action) => action.name)).toEqual([
      "New action",
      "New action1",
    ]);
    expect(map.bindings).toHaveLength(2);
    expect(map.bindings[0].id).not.toBe(map.bindings[1].id);
    expect(map.bindings[1].action).toBe("New action1");
  });

  it("changes composite type and parts", () => {
    let result = addActionMap(newDocument());
    result = addAction(result.asset, 0);
    result = addCompositeBinding(
      result.asset,
      0,
      result.asset.maps[0].actions[0].name,
      "Vector2",
      {
        up: "<Keyboard>/space",
        down: "<Keyboard>/space",
        left: "<Keyboard>/space",
        right: "<Keyboard>/space",
      },
    );
    const next = changeCompositeType(result.asset, 0, 0, "Axis", ["negative", "positive"]);
    const binding = next.maps[0].bindings[0];
    expect(binding.composite).toBe("Axis");
    expect(Object.keys(binding.parts ?? {}).sort()).toEqual(["negative", "positive"]);
  });

  it("updates binding groups when a control scheme is renamed", () => {
    let result = addActionMap(newDocument());
    result = addAction(result.asset, 0);
    result = addBinding(result.asset, 0, result.asset.maps[0].actions[0].name);
    result = addControlScheme(result.asset);
    const scheme = result.asset.schemes?.[0];
    expect(scheme).toBeDefined();
    let asset = setBindingScheme(
      result.asset,
      0,
      0,
      scheme?.bindingGroup ?? "",
      true,
    );
    asset = updateControlScheme(asset, 0, {
      name: "Gamepad",
      bindingGroup: scheme?.bindingGroup ?? "",
      devices: ["Gamepad"],
      isOptional: false,
    });
    expect(asset.maps[0].bindings[0].groups).toBe("Gamepad");
    expect(asset.schemes?.[0].devices).toEqual(["Gamepad"]);
  });

  it("reports missing composite parts before saving", () => {
    let result = addActionMap(newDocument());
    result = addAction(result.asset, 0);
    result = addCompositeBinding(
      result.asset,
      0,
      result.asset.maps[0].actions[0].name,
      "Vector2",
      { up: "<Keyboard>/space" },
    );
    const diagnostics = localDiagnostics(result.asset, catalog);
    expect(
      diagnostics.some((diagnostic) => diagnostic.code === "composite.missing-part"),
    ).toBe(true);
  });
});
