export interface CatalogParameter {
  name: string;
  displayName: string;
  type: "float" | "int" | "bool" | "enum" | string;
  default: string;
  options?: string[];
}

export interface CatalogNamedItem {
  name: string;
  displayName: string;
  parameters: CatalogParameter[];
}

export interface CatalogControl {
  name: string;
  path: string;
  layout: string;
  displayName: string;
  usages: string[];
  aliases: string[];
}

export interface CatalogLayout {
  name: string;
  displayName: string;
  extends?: string;
  controls: CatalogControl[];
}

export interface CatalogComposite {
  name: string;
  displayName: string;
  parts: string[];
  parameters: CatalogParameter[];
}

export interface EditorCatalog {
  layouts: CatalogLayout[];
  devices: string[];
  controlTypes: string[];
  composites: CatalogComposite[];
  interactions: CatalogNamedItem[];
  processors: CatalogNamedItem[];
}

export const emptyCatalog: EditorCatalog = {
  layouts: [],
  devices: [],
  controlTypes: [],
  composites: [],
  interactions: [],
  processors: [],
};

export function findComposite(
  catalog: EditorCatalog,
  name: string,
): CatalogComposite | undefined {
  const normalized = name.split("(")[0].trim().toLowerCase();
  return catalog.composites.find((composite) => {
    if (composite.name.toLowerCase() === normalized) {
      return true;
    }
    if (normalized === "1daxis" && composite.name === "Axis") {
      return true;
    }
    if (normalized === "2dvector" && composite.name === "Vector2") {
      return true;
    }
    if (normalized === "3dvector" && composite.name === "Vector3") {
      return true;
    }
    if (
      (normalized === "buttonwithonemodifier" || normalized === "onemodifier") &&
      composite.name === "OneModifier"
    ) {
      return true;
    }
    if (
      (normalized === "buttonwithtwomodifiers" || normalized === "twomodifiers") &&
      composite.name === "TwoModifiers"
    ) {
      return true;
    }
    return false;
  });
}

export function compositeParts(
  catalog: EditorCatalog,
  compositeName: string,
): string[] {
  return findComposite(catalog, compositeName)?.parts ?? [];
}

export function defaultCompositeParts(
  catalog: EditorCatalog,
  compositeName: string,
): Record<string, string> {
  const parts = compositeParts(catalog, compositeName);
  const fallback = "<Keyboard>/space";
  return Object.fromEntries(parts.map((part) => [part, fallback]));
}

export function controlPaths(catalog: EditorCatalog): CatalogControl[] {
  return catalog.layouts.flatMap((layout) => layout.controls);
}

export function controlPathSuggestions(catalog: EditorCatalog): string[] {
  return controlPaths(catalog).map((control) => control.path);
}

export function namedValues(catalog: EditorCatalog, kind: "interactions" | "processors"): string[] {
  return catalog[kind].map((item) => item.name);
}

export function parameterDefault(
  composite: CatalogComposite | undefined,
  parameter: string,
): string {
  return composite?.parameters.find((item) => item.name === parameter)?.default ?? "";
}
