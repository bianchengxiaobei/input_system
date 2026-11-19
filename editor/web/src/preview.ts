import { type AssetJson, bindingActionName } from "./asset";
import type { CatalogControl, EditorCatalog } from "./catalog";

export interface PreviewControl {
  path: string;
  layout: string;
  value: string;
  offset: number;
  sizeInBits: number;
  button: boolean;
  axis: boolean;
}

export interface PreviewDevice {
  deviceId: number;
  name: string;
  layout: string;
  enabled: boolean;
  connected: boolean;
  controls: PreviewControl[];
}

export interface PreviewAction {
  map: string;
  name: string;
  phase: "Disabled" | "Waiting" | "Started" | "Performed" | "Canceled" | "Invalid";
  value: string;
  drivingControl: string;
  lastStartedTime: number;
  lastPerformedTime: number;
  lastCanceledTime: number;
  lastPerformedFrame: number;
  lastCanceledFrame: number;
}

export interface PreviewBinding {
  map: string;
  action: string;
  path: string;
  interactions: string;
  processors: string;
  name: string;
  overridden: boolean;
  composite: boolean;
  partOfComposite: boolean;
}

export interface PreviewEvent {
  time: number;
  type: string;
  deviceId: number;
  size: number;
}

export interface PreviewMouseInput {
  x?: number;
  y?: number;
  button?: number;
  pressed?: boolean;
  wheelX?: number;
  wheelY?: number;
  deltaX?: number;
  deltaY?: number;
}

export interface CapturedControl {
  path: string;
  deviceId: string;
  controlLayout: string;
  value: number;
  isButton: boolean;
  timestamp: number;
}

export interface PreviewSnapshot {
  generation: number;
  running: boolean;
  asset: string;
  devices: PreviewDevice[];
  actions: PreviewAction[];
  bindings: PreviewBinding[];
  events: PreviewEvent[];
  hid: { enabled: boolean; platform: boolean };
  captured: CapturedControl | null;
}

export const emptyPreviewSnapshot: PreviewSnapshot = {
  generation: 0,
  running: false,
  asset: "",
  devices: [],
  actions: [],
  bindings: [],
  events: [],
  hid: { enabled: false, platform: false },
  captured: null,
};

export interface DerivedBinding {
  map: string;
  action: string;
  path: string;
  name: string;
  mapIndex: number;
  bindingIndex: number;
}

function normalize(value: string): string {
  return value.trim().toLowerCase();
}

function layoutName(path: string): string {
  const match = /^<([^>]+)>/.exec(path.trim());
  return match ? match[1].toLowerCase() : "";
}

function withoutLayout(path: string): string {
  return path.replace(/^<[^>]+>\s*\/?/, "").replace(/^\/+/, "");
}

export function findControl(
  catalog: EditorCatalog,
  path: string,
): { layout: string; control: CatalogControl } | null {
  const candidate = normalize(path);
  if (!candidate) {
    return null;
  }
  for (const layout of catalog.layouts) {
    for (const control of layout.controls) {
      if (normalize(control.path) === candidate) {
        return { layout: layout.name, control };
      }
    }
  }
  return null;
}

export function validateControlPath(
  catalog: EditorCatalog,
  path: string,
  expectedControlType = "",
): string | null {
  const value = path.trim();
  if (!value) {
    return "Control path is empty";
  }
  if (value.includes("*") || value.includes("{")) {
    return null;
  }
  if (catalog.layouts.length === 0) {
    return null;
  }
  const found = findControl(catalog, value);
  if (!found) {
    const layout = layoutName(value);
    if (layout && !catalog.layouts.some((item) => normalize(item.name) === layout)) {
      return `Unknown device layout <${layoutName(value)}>`;
    }
    const control = withoutLayout(value).toLowerCase();
    if (!control) {
      return "Control path does not contain a control name";
    }
    return `No catalog control matches ${control}`;
  }
  const expected = normalize(expectedControlType);
  if (!expected || expected === "any") {
    return null;
  }
  const layout = normalize(found.control.layout);
  if (expected === layout) {
    return null;
  }
  if (expected === "button" && ["button", "key", "anykey"].includes(layout)) {
    return null;
  }
  if (
    expected === "axis" &&
    ["axis", "delta", "vector2", "vector3", "stick", "dpad", "integer", "double"].includes(layout)
  ) {
    return null;
  }
  if (expected === "vector2" && ["vector2", "delta", "stick"].includes(layout)) {
    return null;
  }
  if (expected === "vector3" && layout === "vector3") {
    return null;
  }
  if (expected === "stick" && ["stick", "vector2"].includes(layout)) {
    return null;
  }
  if (expected === "dpad" && layout === "dpad") {
    return null;
  }
  return `Layout ${found.control.layout} is not compatible with ${expectedControlType}`;
}

export function bindingExpectedControlType(
  asset: AssetJson,
  mapIndex: number,
  bindingIndex: number,
): string {
  const map = asset.maps[mapIndex];
  const binding = map?.bindings[bindingIndex];
  if (!map || !binding) {
    return "";
  }
  const name = bindingActionName(binding);
  const action = map.actions.find((candidate) => normalize(candidate.name) === normalize(name));
  return action?.control ?? "";
}

export function compositePartControlType(compositeName: string): string {
  const name = normalize(compositeName.split("(")[0]);
  if (name.includes("modifier") || name.includes("button")) {
    return "Button";
  }
  return "";
}

export function derivedBindings(
  asset: AssetJson,
  mapIndex: number,
  bindingIndex: number,
): DerivedBinding[] {
  const binding = asset.maps[mapIndex]?.bindings[bindingIndex];
  const target = binding?.path?.trim().toLowerCase();
  if (!target) {
    return [];
  }
  const result: DerivedBinding[] = [];
  asset.maps.forEach((map, candidateMapIndex) => {
    map.bindings.forEach((candidate, candidateBindingIndex) => {
      if (candidateMapIndex === mapIndex && candidateBindingIndex === bindingIndex) {
        return;
      }
      if ((candidate.path ?? "").trim().toLowerCase() !== target) {
        return;
      }
      result.push({
        map: map.name,
        action: bindingActionName(candidate),
        path: candidate.path ?? "",
        name: candidate.name ?? "",
        mapIndex: candidateMapIndex,
        bindingIndex: candidateBindingIndex,
      });
    });
  });
  return result;
}
