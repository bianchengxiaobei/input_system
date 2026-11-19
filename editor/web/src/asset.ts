export type ActionType = "value" | "button" | "passthrough";

export interface ActionJson {
  name: string;
  id?: string;
  type: ActionType;
  control?: string;
  processors?: string;
  interactions?: string;
  initialStateCheck?: boolean;
  priority?: number;
}

export interface BindingJson {
  action: string;
  id?: string;
  name?: string;
  path?: string;
  composite?: string;
  parts?: Record<string, string>;
  interactions?: string;
  processors?: string;
  groups?: string;
}

export interface ActionMapJson {
  name: string;
  id?: string;
  actions: ActionJson[];
  bindings: BindingJson[];
}

export interface ControlSchemeJson {
  name: string;
  id?: string;
  bindingGroup?: string;
  devices: string[];
  isOptional?: boolean;
}

export interface AssetJson {
  version: 1;
  name?: string;
  maps: ActionMapJson[];
  schemes?: ControlSchemeJson[];
}

export type Selection =
  | { kind: "none" }
  | { kind: "map"; mapIndex: number }
  | { kind: "action"; mapIndex: number; actionIndex: number }
  | { kind: "binding"; mapIndex: number; bindingIndex: number }
  | { kind: "scheme"; schemeIndex: number };

export interface Snapshot {
  asset: AssetJson;
  selection: Selection;
}

export interface EditCommand {
  label: string;
  before: Snapshot;
  after: Snapshot;
}

export interface EditResult {
  asset: AssetJson;
  selection: Selection;
}

export interface Descriptor {
  name: string;
  parameters: Record<string, string>;
}

export function newId(): string {
  const values = new Uint8Array(16);
  if (typeof crypto !== "undefined" && typeof crypto.getRandomValues === "function") {
    crypto.getRandomValues(values);
  } else {
    for (let index = 0; index < values.length; index += 1) {
      values[index] = Math.floor(Math.random() * 256);
    }
  }
  values[0] = Math.max(1, values[0]);
  return Array.from(values, (value) => value.toString(16).padStart(2, "0")).join("");
}

export function clone<T>(value: T): T {
  return JSON.parse(JSON.stringify(value)) as T;
}

function record(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function optionalString(value: unknown, path: string): string | undefined {
  if (value === undefined) {
    return undefined;
  }
  if (typeof value !== "string") {
    throw new Error(`${path} must be a string`);
  }
  return value;
}

function optionalBoolean(value: unknown, path: string): boolean | undefined {
  if (value === undefined) {
    return undefined;
  }
  if (typeof value !== "boolean") {
    throw new Error(`${path} must be a boolean`);
  }
  return value;
}

function optionalNumber(value: unknown, path: string): number | undefined {
  if (value === undefined) {
    return undefined;
  }
  if (typeof value !== "number" || !Number.isInteger(value)) {
    throw new Error(`${path} must be an integer`);
  }
  return value;
}

function parseAction(value: unknown, path: string): ActionJson {
  if (!record(value)) {
    throw new Error(`${path} must be an object`);
  }
  if (typeof value.name !== "string" || value.name.trim() === "") {
    throw new Error(`${path}.name must be a non-empty string`);
  }
  const type = value.type === undefined ? "button" : String(value.type).toLowerCase();
  if (type !== "value" && type !== "button" && type !== "passthrough") {
    throw new Error(`${path}.type must be value, button, or passthrough`);
  }
  return {
    name: value.name,
    id: optionalString(value.id, `${path}.id`),
    type,
    control: optionalString(value.control, `${path}.control`),
    processors: optionalString(value.processors, `${path}.processors`),
    interactions: optionalString(value.interactions, `${path}.interactions`),
    initialStateCheck: optionalBoolean(
      value.initialStateCheck,
      `${path}.initialStateCheck`,
    ),
    priority: optionalNumber(value.priority, `${path}.priority`),
  };
}

function parseParts(value: unknown, path: string): Record<string, string> | undefined {
  if (value === undefined) {
    return undefined;
  }
  if (!record(value)) {
    throw new Error(`${path} must be an object`);
  }
  const parts: Record<string, string> = {};
  for (const [name, partPath] of Object.entries(value)) {
    if (typeof partPath !== "string") {
      throw new Error(`${path}.${name} must be a string`);
    }
    parts[name] = partPath;
  }
  return parts;
}

function parseBinding(value: unknown, path: string): BindingJson {
  if (!record(value)) {
    throw new Error(`${path} must be an object`);
  }
  if (typeof value.action !== "string" || value.action.trim() === "") {
    throw new Error(`${path}.action must be a non-empty string`);
  }
  const pathValue = optionalString(value.path, `${path}.path`);
  const composite = optionalString(value.composite, `${path}.composite`);
  if (pathValue === undefined && composite === undefined) {
    throw new Error(`${path} must contain path or composite`);
  }
  if (pathValue !== undefined && composite !== undefined) {
    throw new Error(`${path} cannot contain both path and composite`);
  }
  const parts = parseParts(value.parts, `${path}.parts`);
  if (composite === undefined && parts !== undefined) {
    throw new Error(`${path}.parts requires composite`);
  }
  return {
    action: value.action,
    id: optionalString(value.id, `${path}.id`),
    name: optionalString(value.name, `${path}.name`),
    path: pathValue,
    composite,
    parts,
    interactions: optionalString(value.interactions, `${path}.interactions`),
    processors: optionalString(value.processors, `${path}.processors`),
    groups: optionalString(value.groups, `${path}.groups`),
  };
}

function parseMap(value: unknown, path: string): ActionMapJson {
  if (!record(value)) {
    throw new Error(`${path} must be an object`);
  }
  if (typeof value.name !== "string" || value.name.trim() === "") {
    throw new Error(`${path}.name must be a non-empty string`);
  }
  const actions = value.actions === undefined ? [] : value.actions;
  if (!Array.isArray(actions)) {
    throw new Error(`${path}.actions must be an array`);
  }
  const bindings = value.bindings === undefined ? [] : value.bindings;
  if (!Array.isArray(bindings)) {
    throw new Error(`${path}.bindings must be an array`);
  }
  return {
    name: value.name,
    id: optionalString(value.id, `${path}.id`),
    actions: actions.map((action, index) =>
      parseAction(action, `${path}.actions[${index}]`),
    ),
    bindings: bindings.map((binding, index) =>
      parseBinding(binding, `${path}.bindings[${index}]`),
    ),
  };
}

function parseScheme(value: unknown, path: string): ControlSchemeJson {
  if (!record(value)) {
    throw new Error(`${path} must be an object`);
  }
  if (typeof value.name !== "string" || value.name.trim() === "") {
    throw new Error(`${path}.name must be a non-empty string`);
  }
  if (!Array.isArray(value.devices)) {
    throw new Error(`${path}.devices must be an array`);
  }
  const devices = value.devices.map((device, index) => {
    if (typeof device !== "string") {
      throw new Error(`${path}.devices[${index}] must be a string`);
    }
    return device;
  });
  return {
    name: value.name,
    id: optionalString(value.id, `${path}.id`),
    bindingGroup: optionalString(value.bindingGroup, `${path}.bindingGroup`),
    devices,
    isOptional: optionalBoolean(value.isOptional, `${path}.isOptional`),
  };
}

export function parseAsset(text: string): AssetJson {
  let parsed: unknown;
  try {
    parsed = JSON.parse(text);
  } catch (error) {
    throw new Error(error instanceof Error ? error.message : String(error));
  }
  if (!record(parsed)) {
    throw new Error("asset root must be an object");
  }
  if (parsed.version !== 1) {
    if (parsed.version === undefined) {
      throw new Error("asset version must be 1");
    }
    throw new Error(
      `unsupported asset schema version ${String(parsed.version)}; editor supports version 1 (migration required)`,
    );
  }
  if (!Array.isArray(parsed.maps)) {
    throw new Error("asset maps must be an array");
  }
  if (parsed.name !== undefined && typeof parsed.name !== "string") {
    throw new Error("asset name must be a string");
  }
  if (parsed.schemes !== undefined && !Array.isArray(parsed.schemes)) {
    throw new Error("asset schemes must be an array");
  }
  const asset: AssetJson = {
    version: 1,
    name: parsed.name,
    maps: parsed.maps.map((map, index) => parseMap(map, `maps[${index}]`)),
    schemes: Array.isArray(parsed.schemes)
      ? parsed.schemes.map((scheme, index) => parseScheme(scheme, `schemes[${index}]`))
      : [],
  };
  for (const map of asset.maps) {
    map.id ??= newId();
    for (const action of map.actions) {
      action.id ??= newId();
    }
    for (const binding of map.bindings) {
      binding.id ??= newId();
    }
  }
  for (const scheme of asset.schemes ?? []) {
    scheme.id ??= newId();
  }
  return asset;
}

export function serializeAsset(asset: AssetJson): string {
  const maps = asset.maps.map((map) => {
    const output: Record<string, unknown> = {
      name: map.name,
    };
    if (map.id) {
      output.id = map.id;
    }
    output.actions = map.actions.map((action) => {
      const actionOutput: Record<string, unknown> = {
        name: action.name,
      };
      if (action.id) {
        actionOutput.id = action.id;
      }
      actionOutput.type = action.type;
      if (action.control) {
        actionOutput.control = action.control;
      }
      if (action.processors) {
        actionOutput.processors = action.processors;
      }
      if (action.interactions) {
        actionOutput.interactions = action.interactions;
      }
      if (action.initialStateCheck) {
        actionOutput.initialStateCheck = true;
      }
      if (action.priority) {
        actionOutput.priority = action.priority;
      }
      return actionOutput;
    });
    output.bindings = map.bindings.map((binding) => {
      const bindingOutput: Record<string, unknown> = {
        action: binding.action,
      };
      if (binding.id) {
        bindingOutput.id = binding.id;
      }
      if (binding.name) {
        bindingOutput.name = binding.name;
      }
      if (binding.composite !== undefined) {
        bindingOutput.composite = binding.composite;
      } else if (binding.path !== undefined) {
        bindingOutput.path = binding.path;
      }
      if (binding.interactions) {
        bindingOutput.interactions = binding.interactions;
      }
      if (binding.processors) {
        bindingOutput.processors = binding.processors;
      }
      if (binding.groups) {
        bindingOutput.groups = binding.groups;
      }
      if (binding.parts && Object.keys(binding.parts).length > 0) {
        bindingOutput.parts = binding.parts;
      }
      return bindingOutput;
    });
    return output;
  });

  const output: Record<string, unknown> = {
    version: 1,
  };
  if (asset.name) {
    output.name = asset.name;
  }
  output.maps = maps;
  if (asset.schemes && asset.schemes.length > 0) {
    output.schemes = asset.schemes.map((scheme) => {
      const schemeOutput: Record<string, unknown> = {
        name: scheme.name,
      };
      if (scheme.id) {
        schemeOutput.id = scheme.id;
      }
      if (scheme.bindingGroup) {
        schemeOutput.bindingGroup = scheme.bindingGroup;
      }
      schemeOutput.devices = scheme.devices;
      if (scheme.isOptional) {
        schemeOutput.isOptional = true;
      }
      return schemeOutput;
    });
  }
  return JSON.stringify(output, null, 2);
}

export function uniqueName(base: string, existing: Iterable<string>): string {
  const names = new Set<string>();
  for (const name of existing) {
    names.add(name.toLowerCase());
  }
  let stem = base;
  let counter = 1;
  const match = /^(.*?)(\d+)$/.exec(base);
  if (match) {
    stem = match[1];
    counter = Number(match[2]) + 1;
  }
  let candidate = base;
  while (names.has(candidate.toLowerCase())) {
    candidate = `${stem}${counter}`;
    counter += 1;
  }
  return candidate;
}

export function bindingActionName(binding: BindingJson): string {
  const slash = binding.action.lastIndexOf("/");
  return slash === -1 ? binding.action : binding.action.slice(slash + 1);
}

export function bindsAction(binding: BindingJson, actionName: string): boolean {
  return bindingActionName(binding).toLowerCase() === actionName.toLowerCase();
}

export function bindingsForAction(
  map: ActionMapJson,
  actionName: string,
): Array<{ binding: BindingJson; index: number }> {
  const result: Array<{ binding: BindingJson; index: number }> = [];
  map.bindings.forEach((binding, index) => {
    if (bindsAction(binding, actionName)) {
      result.push({ binding, index });
    }
  });
  return result;
}

export function splitBindingGroups(groups: string | undefined): string[] {
  if (!groups) {
    return [];
  }
  return groups
    .split(/[;,]/)
    .map((group) => group.trim())
    .filter((group) => group.length > 0);
}

export function joinBindingGroups(groups: string[]): string | undefined {
  const seen = new Set<string>();
  const unique = groups.filter((group) => {
    const key = group.toLowerCase();
    if (seen.has(key)) {
      return false;
    }
    seen.add(key);
    return true;
  });
  return unique.length > 0 ? unique.join(";") : undefined;
}

export function hasBindingGroup(groups: string | undefined, group: string): boolean {
  return splitBindingGroups(groups).some(
    (value) => value.toLowerCase() === group.toLowerCase(),
  );
}

export function parseDescriptor(text: string): Descriptor {
  const value = text.trim();
  const open = value.indexOf("(");
  if (open === -1 || !value.endsWith(")")) {
    return { name: value, parameters: {} };
  }
  const name = value.slice(0, open).trim();
  const body = value.slice(open + 1, -1);
  const parameters: Record<string, string> = {};
  let start = 0;
  let depth = 0;
  let quote = false;
  for (let index = 0; index <= body.length; index += 1) {
    const character = body[index];
    if (character === '"' || character === "'") {
      quote = !quote;
    } else if (!quote) {
      if (character === "(") {
        depth += 1;
      } else if (character === ")") {
        depth -= 1;
      }
    }
    if ((character === "," && depth === 0 && !quote) || index === body.length) {
      const part = body.slice(start, index).trim();
      if (part) {
        const equals = part.indexOf("=");
        if (equals === -1) {
          parameters[part] = "true";
        } else {
          parameters[part.slice(0, equals).trim()] = part
            .slice(equals + 1)
            .trim()
            .replace(/^["']|["']$/g, "");
        }
      }
      start = index + 1;
    }
  }
  return { name, parameters };
}

export function formatDescriptor(name: string, parameters: Record<string, string>): string {
  const entries = Object.entries(parameters).filter(([, value]) => value !== "");
  if (entries.length === 0) {
    return name;
  }
  return `${name}(${entries.map(([key, value]) => `${key}=${value}`).join(",")})`;
}

export function updateDescriptorParameter(
  descriptor: string,
  parameter: string,
  value: string,
): string {
  const parsed = parseDescriptor(descriptor);
  parsed.parameters[parameter] = value;
  return formatDescriptor(parsed.name, parsed.parameters);
}

export function actionTypeControlDefaults(type: ActionType): string {
  if (type === "button") {
    return "Button";
  }
  if (type === "passthrough") {
    return "";
  }
  return "Vector2";
}

export function normalizeSelection(asset: AssetJson, selection: Selection): Selection {
  if (selection.kind === "none") {
    return selection;
  }
  if (selection.kind === "scheme") {
    return selection.schemeIndex >= 0 && selection.schemeIndex < (asset.schemes?.length ?? 0)
      ? selection
      : { kind: "none" };
  }
  const map = asset.maps[selection.mapIndex];
  if (!map) {
    return { kind: "none" };
  }
  if (selection.kind === "map") {
    return selection;
  }
  if (selection.kind === "action") {
    return selection.actionIndex >= 0 && selection.actionIndex < map.actions.length
      ? selection
      : { kind: "map", mapIndex: selection.mapIndex };
  }
  return selection.bindingIndex >= 0 && selection.bindingIndex < map.bindings.length
    ? selection
    : { kind: "map", mapIndex: selection.mapIndex };
}

export function selectionForPointer(asset: AssetJson, pointer: string): Selection | null {
  const parts = pointer.split("/").filter((part) => part.length > 0);
  if (parts[0] === "schemes" && parts.length >= 2) {
    const schemeIndex = Number(parts[1]);
    return Number.isInteger(schemeIndex) ? { kind: "scheme", schemeIndex } : null;
  }
  if (parts[0] !== "maps" || parts.length < 2) {
    return null;
  }
  const mapIndex = Number(parts[1]);
  if (!Number.isInteger(mapIndex)) {
    return null;
  }
  if (parts[2] === "actions" && parts.length >= 4) {
    const actionIndex = Number(parts[3]);
    return Number.isInteger(actionIndex)
      ? { kind: "action", mapIndex, actionIndex }
      : null;
  }
  if (parts[2] === "bindings" && parts.length >= 4) {
    const bindingIndex = Number(parts[3]);
    return Number.isInteger(bindingIndex)
      ? { kind: "binding", mapIndex, bindingIndex }
      : null;
  }
  return { kind: "map", mapIndex };
}

export function removeActionReferenceBindings(
  map: ActionMapJson,
  actionName: string,
): void {
  map.bindings = map.bindings.filter((binding) => !bindsAction(binding, actionName));
}

export function updateBindingActionReferences(
  map: ActionMapJson,
  oldName: string,
  newName: string,
): void {
  for (const binding of map.bindings) {
    if (!bindsAction(binding, oldName)) {
      continue;
    }
    const slash = binding.action.lastIndexOf("/");
    binding.action = `${slash === -1 ? "" : binding.action.slice(0, slash + 1)}${newName}`;
  }
}

export function cloneBindingWithNewId(
  binding: BindingJson,
  actionName: string,
  name: string | undefined,
): BindingJson {
  return {
    ...clone(binding),
    id: newId(),
    action: actionName,
    name,
  };
}

export function cloneActionWithNewId(
  action: ActionJson,
  name: string,
): ActionJson {
  return {
    ...clone(action),
    id: newId(),
    name,
  };
}

export function cloneMapWithNewIds(map: ActionMapJson, name: string): ActionMapJson {
  const result = clone(map);
  result.id = newId();
  result.name = name;
  result.actions = result.actions.map((action) => ({
    ...action,
    id: newId(),
  }));
  result.bindings = result.bindings.map((binding) => ({
    ...binding,
    id: newId(),
  }));
  return result;
}

export function cloneSchemeWithNewId(
  scheme: ControlSchemeJson,
  name: string,
  bindingGroup: string,
): ControlSchemeJson {
  return {
    ...clone(scheme),
    id: newId(),
    name,
    bindingGroup,
  };
}

export function defaultBinding(actionName: string): BindingJson {
  return {
    action: actionName,
    id: newId(),
    path: "<Keyboard>/space",
  };
}
