import type { Diagnostic } from "./api";
import type { AssetJson } from "./asset";
import { type EditorCatalog, findComposite } from "./catalog";

export function localDiagnostics(
  asset: AssetJson | null,
  catalog: EditorCatalog,
): Diagnostic[] {
  if (!asset) {
    return [];
  }
  const result: Diagnostic[] = [];
  for (let mapIndex = 0; mapIndex < asset.maps.length; mapIndex += 1) {
    const map = asset.maps[mapIndex];
    for (let bindingIndex = 0; bindingIndex < map.bindings.length; bindingIndex += 1) {
      const binding = map.bindings[bindingIndex];
      const pointer = `/maps/${mapIndex}/bindings/${bindingIndex}`;
      if (binding.composite === undefined) {
        if (!binding.path?.trim()) {
          result.push({
            severity: "error",
            code: "binding.empty-path",
            message: "binding path must not be empty",
            jsonPointer: `${pointer}/path`,
          });
        }
        continue;
      }
      if (catalog.composites.length === 0) {
        continue;
      }
      const composite = findComposite(catalog, binding.composite);
      if (!composite) {
        result.push({
          severity: "error",
          code: "composite.unknown",
          message: `unknown composite '${binding.composite}'`,
          jsonPointer: `${pointer}/composite`,
        });
        continue;
      }
      const parts = binding.parts ?? {};
      for (const part of composite.parts) {
        const value = parts[part];
        if (value === undefined) {
          result.push({
            severity: "error",
            code: "composite.missing-part",
            message: `composite '${composite.name}' is missing part '${part}'`,
            jsonPointer: `${pointer}/parts/${part}`,
          });
        } else if (!value.trim()) {
          result.push({
            severity: "error",
            code: "composite.empty-part",
            message: `composite part '${part}' must have a control path`,
            jsonPointer: `${pointer}/parts/${part}`,
          });
        }
      }
      for (const part of Object.keys(parts)) {
        if (!composite.parts.includes(part)) {
          result.push({
            severity: "warning",
            code: "composite.unknown-part",
            message: `composite '${composite.name}' has no part '${part}'`,
            jsonPointer: `${pointer}/parts/${part}`,
          });
        }
      }
    }
  }

  for (let schemeIndex = 0; schemeIndex < (asset.schemes?.length ?? 0); schemeIndex += 1) {
    const scheme = asset.schemes?.[schemeIndex];
    if (!scheme) {
      continue;
    }
    const pointer = `/schemes/${schemeIndex}`;
    if (scheme.devices.length === 0) {
      result.push({
        severity: "error",
        code: "scheme.no-devices",
        message: "control scheme requires at least one device",
        jsonPointer: `${pointer}/devices`,
      });
    }
    if (catalog.devices.length > 0) {
      for (let deviceIndex = 0; deviceIndex < scheme.devices.length; deviceIndex += 1) {
        const device = scheme.devices[deviceIndex];
        if (!catalog.devices.some((value) => value.toLowerCase() === device.toLowerCase())) {
          result.push({
            severity: "warning",
            code: "scheme.unknown-device",
            message: `unknown device layout '${device}'`,
            jsonPointer: `${pointer}/devices/${deviceIndex}`,
          });
        }
      }
    }
  }
  return result;
}

export function mergeDiagnostics(...groups: Diagnostic[][]): Diagnostic[] {
  const result: Diagnostic[] = [];
  const seen = new Set<string>();
  for (const group of groups) {
    for (const diagnostic of group) {
      const key = `${diagnostic.severity}|${diagnostic.code}|${diagnostic.jsonPointer ?? ""}|${diagnostic.message}`;
      if (!seen.has(key)) {
        seen.add(key);
        result.push(diagnostic);
      }
    }
  }
  return result;
}
