import {
  type ActionJson,
  type ActionMapJson,
  type AssetJson,
  type BindingJson,
  type ControlSchemeJson,
  type EditResult,
  type Selection,
  bindingActionName,
  bindingsForAction,
  clone,
  cloneActionWithNewId,
  cloneBindingWithNewId,
  cloneMapWithNewIds,
  cloneSchemeWithNewId,
  defaultBinding,
  formatDescriptor,
  joinBindingGroups,
  newId,
  parseDescriptor,
  removeActionReferenceBindings,
  splitBindingGroups,
  uniqueName,
  updateBindingActionReferences,
} from "./asset";

export function editAsset(
  asset: AssetJson,
  mutate: (draft: AssetJson) => void,
): AssetJson {
  const next = clone(asset);
  mutate(next);
  return next;
}

function clamp(value: number, minimum: number, maximum: number): number {
  return Math.max(minimum, Math.min(maximum, value));
}

function moveItem<T>(items: T[], from: number, to: number): T[] {
  if (from === to || from < 0 || from >= items.length || to < 0 || to >= items.length) {
    return items;
  }
  const next = items.slice();
  const [item] = next.splice(from, 1);
  next.splice(to, 0, item);
  return next;
}

function lastBindingIndexForAction(map: ActionMapJson, actionName: string): number {
  let result = -1;
  map.bindings.forEach((binding, index) => {
    if (bindingActionName(binding).toLowerCase() === actionName.toLowerCase()) {
      result = index;
    }
  });
  return result;
}

function insertBinding(map: ActionMapJson, binding: BindingJson, actionName: string): number {
  const last = lastBindingIndexForAction(map, actionName);
  const index = last === -1 ? map.bindings.length : last + 1;
  map.bindings.splice(index, 0, binding);
  return index;
}

export function addActionMap(asset: AssetJson): EditResult {
  const next = clone(asset);
  const name = uniqueName("New action map", next.maps.map((map) => map.name));
  next.maps.push({
    name,
    id: newId(),
    actions: [],
    bindings: [],
  });
  return {
    asset: next,
    selection: { kind: "map", mapIndex: next.maps.length - 1 },
  };
}

export function renameActionMap(
  asset: AssetJson,
  mapIndex: number,
  requestedName: string,
): AssetJson {
  return editAsset(asset, (draft) => {
    const map = draft.maps[mapIndex];
    if (!map) {
      return;
    }
    const names = draft.maps
      .filter((_, index) => index !== mapIndex)
      .map((item) => item.name);
    map.name = uniqueName(requestedName.trim() || "Action map", names);
  });
}

export function duplicateActionMap(asset: AssetJson, mapIndex: number): EditResult {
  const source = asset.maps[mapIndex];
  if (!source) {
    return { asset, selection: { kind: "none" } };
  }
  const next = clone(asset);
  const names = next.maps.map((map) => map.name);
  const name = uniqueName(source.name, names);
  const duplicate = cloneMapWithNewIds(source, name);
  next.maps.splice(mapIndex + 1, 0, duplicate);
  return {
    asset: next,
    selection: { kind: "map", mapIndex: mapIndex + 1 },
  };
}

export function deleteActionMap(asset: AssetJson, mapIndex: number): EditResult {
  const next = clone(asset);
  if (!next.maps[mapIndex]) {
    return { asset, selection: { kind: "none" } };
  }
  next.maps.splice(mapIndex, 1);
  if (next.maps.length === 0) {
    return { asset: next, selection: { kind: "none" } };
  }
  return {
    asset: next,
    selection: {
      kind: "map",
      mapIndex: clamp(mapIndex, 0, next.maps.length - 1),
    },
  };
}

export function moveActionMap(asset: AssetJson, from: number, to: number): EditResult {
  const next = clone(asset);
  const target = clamp(to, 0, next.maps.length - 1);
  next.maps = moveItem(next.maps, from, target);
  return {
    asset: next,
    selection: { kind: "map", mapIndex: target },
  };
}

export function addAction(asset: AssetJson, mapIndex: number): EditResult {
  const next = clone(asset);
  const map = next.maps[mapIndex];
  if (!map) {
    return { asset, selection: { kind: "none" } };
  }
  const name = uniqueName("New action", map.actions.map((action) => action.name));
  map.actions.push({
    name,
    id: newId(),
    type: "button",
    control: "Button",
  });
  return {
    asset: next,
    selection: {
      kind: "action",
      mapIndex,
      actionIndex: map.actions.length - 1,
    },
  };
}

export function duplicateAction(
  asset: AssetJson,
  mapIndex: number,
  actionIndex: number,
): EditResult {
  const source = asset.maps[mapIndex]?.actions[actionIndex];
  if (!source) {
    return { asset, selection: { kind: "none" } };
  }
  const next = clone(asset);
  const map = next.maps[mapIndex];
  const name = uniqueName(source.name, map.actions.map((action) => action.name));
  const duplicate = cloneActionWithNewId(source, name);
  map.actions.splice(actionIndex + 1, 0, duplicate);

  const sourceBindings = map.bindings.filter(
    (binding) => bindingActionName(binding).toLowerCase() === source.name.toLowerCase(),
  );
  if (sourceBindings.length > 0) {
    const last = lastBindingIndexForAction(map, source.name);
    const existingNames = map.bindings
      .map((binding) => binding.name)
      .filter((value): value is string => Boolean(value));
    const clones = sourceBindings.map((binding) => {
      const bindingName = binding.name
        ? uniqueName(binding.name, existingNames)
        : undefined;
      if (bindingName) {
        existingNames.push(bindingName);
      }
      return cloneBindingWithNewId(binding, name, bindingName);
    });
    map.bindings.splice(last + 1, 0, ...clones);
  }

  return {
    asset: next,
    selection: {
      kind: "action",
      mapIndex,
      actionIndex: actionIndex + 1,
    },
  };
}

export function deleteAction(
  asset: AssetJson,
  mapIndex: number,
  actionIndex: number,
): EditResult {
  const next = clone(asset);
  const map = next.maps[mapIndex];
  const action = map?.actions[actionIndex];
  if (!map || !action) {
    return { asset, selection: { kind: "none" } };
  }
  removeActionReferenceBindings(map, action.name);
  map.actions.splice(actionIndex, 1);
  return {
    asset: next,
    selection: { kind: "map", mapIndex },
  };
}

export function moveAction(
  asset: AssetJson,
  mapIndex: number,
  from: number,
  to: number,
): EditResult {
  const next = clone(asset);
  const map = next.maps[mapIndex];
  if (!map) {
    return { asset, selection: { kind: "none" } };
  }
  const target = clamp(to, 0, map.actions.length - 1);
  map.actions = moveItem(map.actions, from, target);
  return {
    asset: next,
    selection: { kind: "action", mapIndex, actionIndex: target },
  };
}

export function updateAction(
  asset: AssetJson,
  mapIndex: number,
  actionIndex: number,
  patch: Partial<ActionJson>,
): AssetJson {
  return editAsset(asset, (draft) => {
    const map = draft.maps[mapIndex];
    const action = map?.actions[actionIndex];
    if (!map || !action) {
      return;
    }
    if (patch.name !== undefined && patch.name.trim() !== action.name) {
      const requested = patch.name.trim() || action.name;
      const names = map.actions
        .filter((_, index) => index !== actionIndex)
        .map((item) => item.name);
      const nextName = uniqueName(requested, names);
      updateBindingActionReferences(map, action.name, nextName);
      action.name = nextName;
    }
    if (patch.type !== undefined) {
      action.type = patch.type;
    }
    if (patch.control !== undefined) {
      action.control = patch.control.trim() || undefined;
    }
    if (patch.processors !== undefined) {
      action.processors = patch.processors.trim() || undefined;
    }
    if (patch.interactions !== undefined) {
      action.interactions = patch.interactions.trim() || undefined;
    }
    if (patch.initialStateCheck !== undefined) {
      action.initialStateCheck = patch.initialStateCheck || undefined;
    }
    if (patch.priority !== undefined) {
      action.priority = patch.priority === 0 ? undefined : patch.priority;
    }
  });
}

export function addBinding(
  asset: AssetJson,
  mapIndex: number,
  actionName: string,
): EditResult {
  const next = clone(asset);
  const map = next.maps[mapIndex];
  if (!map) {
    return { asset, selection: { kind: "none" } };
  }
  const binding = defaultBinding(actionName);
  const existingNames = map.bindings
    .map((item) => item.name)
    .filter((value): value is string => Boolean(value));
  binding.name = uniqueName("New binding", existingNames);
  const index = insertBinding(map, binding, actionName);
  return {
    asset: next,
    selection: { kind: "binding", mapIndex, bindingIndex: index },
  };
}

export function addCompositeBinding(
  asset: AssetJson,
  mapIndex: number,
  actionName: string,
  compositeName: string,
  parts: Record<string, string>,
): EditResult {
  const next = clone(asset);
  const map = next.maps[mapIndex];
  if (!map) {
    return { asset, selection: { kind: "none" } };
  }
  const binding: BindingJson = {
    action: actionName,
    id: newId(),
    name: uniqueName(
      compositeName,
      map.bindings
        .map((item) => item.name)
        .filter((value): value is string => Boolean(value)),
    ),
    composite: compositeName,
    parts,
  };
  const index = insertBinding(map, binding, actionName);
  return {
    asset: next,
    selection: { kind: "binding", mapIndex, bindingIndex: index },
  };
}

export function updateBinding(
  asset: AssetJson,
  mapIndex: number,
  bindingIndex: number,
  patch: Partial<BindingJson>,
): AssetJson {
  return editAsset(asset, (draft) => {
    const binding = draft.maps[mapIndex]?.bindings[bindingIndex];
    if (!binding) {
      return;
    }
    if (patch.name !== undefined) {
      binding.name = patch.name.trim() || undefined;
    }
    if (patch.path !== undefined) {
      binding.path = patch.path;
      delete binding.composite;
      delete binding.parts;
    }
    if (patch.composite !== undefined) {
      binding.composite = patch.composite;
      binding.path = undefined;
    }
    if (patch.parts !== undefined) {
      binding.parts = patch.parts;
    }
    if (patch.interactions !== undefined) {
      binding.interactions = patch.interactions.trim() || undefined;
    }
    if (patch.processors !== undefined) {
      binding.processors = patch.processors.trim() || undefined;
    }
    if (patch.groups !== undefined) {
      binding.groups = joinBindingGroups(splitBindingGroups(patch.groups));
    }
  });
}

export function changeCompositeType(
  asset: AssetJson,
  mapIndex: number,
  bindingIndex: number,
  compositeName: string,
  partNames: string[],
): AssetJson {
  return editAsset(asset, (draft) => {
    const binding = draft.maps[mapIndex]?.bindings[bindingIndex];
    if (!binding) {
      return;
    }
    const descriptor = parseDescriptor(binding.composite ?? "");
    const sameType = descriptor.name.toLowerCase() === compositeName.toLowerCase();
    binding.composite = formatDescriptor(
      compositeName,
      sameType ? descriptor.parameters : {},
    );
    const currentParts = binding.parts ?? {};
    binding.parts = Object.fromEntries(
      partNames.map((part) => [
        part,
        currentParts[part] || "<Keyboard>/space",
      ]),
    );
  });
}

export function pasteActionMap(asset: AssetJson, source: ActionMapJson): EditResult {
  const next = clone(asset);
  const name = uniqueName(source.name, next.maps.map((map) => map.name));
  next.maps.push(cloneMapWithNewIds(source, name));
  return {
    asset: next,
    selection: { kind: "map", mapIndex: next.maps.length - 1 },
  };
}

export function pasteAction(
  asset: AssetJson,
  mapIndex: number,
  source: ActionJson,
  sourceBindings: BindingJson[],
): EditResult {
  const next = clone(asset);
  const map = next.maps[mapIndex];
  if (!map) {
    return { asset, selection: { kind: "none" } };
  }
  const name = uniqueName(source.name, map.actions.map((action) => action.name));
  const action = cloneActionWithNewId(source, name);
  map.actions.push(action);

  if (sourceBindings.length > 0) {
    const bindingNames = map.bindings
      .map((binding) => binding.name)
      .filter((value): value is string => Boolean(value));
    const bindings = sourceBindings.map((binding) => {
      const bindingName = binding.name
        ? uniqueName(binding.name, bindingNames)
        : undefined;
      if (bindingName) {
        bindingNames.push(bindingName);
      }
      return cloneBindingWithNewId(binding, name, bindingName);
    });
    map.bindings.push(...bindings);
  }

  return {
    asset: next,
    selection: {
      kind: "action",
      mapIndex,
      actionIndex: map.actions.length - 1,
    },
  };
}

export function pasteBinding(
  asset: AssetJson,
  mapIndex: number,
  actionName: string,
  source: BindingJson,
): EditResult {
  const next = clone(asset);
  const map = next.maps[mapIndex];
  if (!map) {
    return { asset, selection: { kind: "none" } };
  }
  const names = map.bindings
    .map((binding) => binding.name)
    .filter((value): value is string => Boolean(value));
  const name = uniqueName(source.name || "New binding", names);
  const binding = cloneBindingWithNewId(source, actionName, name);
  const index = insertBinding(map, binding, actionName);
  return {
    asset: next,
    selection: { kind: "binding", mapIndex, bindingIndex: index },
  };
}

export function pasteControlScheme(
  asset: AssetJson,
  source: ControlSchemeJson,
): EditResult {
  const next = clone(asset);
  if (!next.schemes) {
    next.schemes = [];
  }
  const name = uniqueName(source.name, next.schemes.map((scheme) => scheme.name));
  const groups = next.schemes
    .map((scheme) => scheme.bindingGroup ?? scheme.name)
    .filter((group) => group.length > 0);
  const group = uniqueName(source.bindingGroup || source.name, groups);
  next.schemes.push(cloneSchemeWithNewId(source, name, group));
  return {
    asset: next,
    selection: { kind: "scheme", schemeIndex: next.schemes.length - 1 },
  };
}

export function duplicateBinding(asset: AssetJson, mapIndex: number, bindingIndex: number): EditResult {
  const next = clone(asset);
  const map = next.maps[mapIndex];
  const source = map?.bindings[bindingIndex];
  if (!map || !source) {
    return { asset, selection: { kind: "none" } };
  }
  const names = map.bindings
    .map((binding) => binding.name)
    .filter((value): value is string => Boolean(value));
  const name = uniqueName(source.name || "New binding", names);
  const duplicate = cloneBindingWithNewId(source, source.action, name);
  map.bindings.splice(bindingIndex + 1, 0, duplicate);
  return {
    asset: next,
    selection: { kind: "binding", mapIndex, bindingIndex: bindingIndex + 1 },
  };
}

export function deleteBinding(asset: AssetJson, mapIndex: number, bindingIndex: number): EditResult {
  const next = clone(asset);
  const map = next.maps[mapIndex];
  if (!map || !map.bindings[bindingIndex]) {
    return { asset, selection: { kind: "none" } };
  }
  map.bindings.splice(bindingIndex, 1);
  return {
    asset: next,
    selection: { kind: "map", mapIndex },
  };
}

export function moveBinding(
  asset: AssetJson,
  mapIndex: number,
  from: number,
  to: number,
): EditResult {
  const next = clone(asset);
  const map = next.maps[mapIndex];
  if (!map) {
    return { asset, selection: { kind: "none" } };
  }
  const target = clamp(to, 0, map.bindings.length - 1);
  map.bindings = moveItem(map.bindings, from, target);
  return {
    asset: next,
    selection: { kind: "binding", mapIndex, bindingIndex: target },
  };
}

export function addControlScheme(asset: AssetJson): EditResult {
  const next = clone(asset);
  if (!next.schemes) {
    next.schemes = [];
  }
  const name = uniqueName("New Control Scheme", next.schemes.map((scheme) => scheme.name));
  next.schemes.push({
    name,
    id: newId(),
    bindingGroup: name,
    devices: ["Keyboard"],
  });
  return {
    asset: next,
    selection: { kind: "scheme", schemeIndex: next.schemes.length - 1 },
  };
}

export function duplicateControlScheme(asset: AssetJson, schemeIndex: number): EditResult {
  const source = asset.schemes?.[schemeIndex];
  if (!source) {
    return { asset, selection: { kind: "none" } };
  }
  const next = clone(asset);
  if (!next.schemes) {
    next.schemes = [];
  }
  const name = uniqueName(source.name, next.schemes.map((scheme) => scheme.name));
  const groups = next.schemes
    .map((scheme) => scheme.bindingGroup ?? scheme.name)
    .filter((group) => group.length > 0);
  const group = uniqueName(source.bindingGroup || source.name, groups);
  next.schemes.splice(schemeIndex + 1, 0, cloneSchemeWithNewId(source, name, group));
  return {
    asset: next,
    selection: { kind: "scheme", schemeIndex: schemeIndex + 1 },
  };
}

export function deleteControlScheme(asset: AssetJson, schemeIndex: number): EditResult {
  const next = clone(asset);
  const scheme = next.schemes?.[schemeIndex];
  if (!scheme) {
    return { asset, selection: { kind: "none" } };
  }
  const group = scheme.bindingGroup || scheme.name;
  for (const map of next.maps) {
    for (const binding of map.bindings) {
      binding.groups = joinBindingGroups(
        splitBindingGroups(binding.groups).filter(
          (value) => value.toLowerCase() !== group.toLowerCase(),
        ),
      );
    }
  }
  next.schemes?.splice(schemeIndex, 1);
  if (!next.schemes || next.schemes.length === 0) {
    return { asset: next, selection: { kind: "none" } };
  }
  return {
    asset: next,
    selection: {
      kind: "scheme",
      schemeIndex: clamp(schemeIndex, 0, next.schemes.length - 1),
    },
  };
}

export function moveControlScheme(
  asset: AssetJson,
  from: number,
  to: number,
): EditResult {
  const next = clone(asset);
  if (!next.schemes || next.schemes.length === 0) {
    return { asset, selection: { kind: "none" } };
  }
  const target = clamp(to, 0, next.schemes.length - 1);
  next.schemes = moveItem(next.schemes, from, target);
  return {
    asset: next,
    selection: { kind: "scheme", schemeIndex: target },
  };
}

export function updateControlScheme(
  asset: AssetJson,
  schemeIndex: number,
  fields: {
    name: string;
    bindingGroup: string;
    devices: string[];
    isOptional: boolean;
  },
): AssetJson {
  return editAsset(asset, (draft) => {
    const scheme = draft.schemes?.[schemeIndex];
    if (!scheme || !draft.schemes) {
      return;
    }
    const oldGroup = scheme.bindingGroup || scheme.name;
    const names = draft.schemes
      .filter((_, index) => index !== schemeIndex)
      .map((item) => item.name);
    const name = uniqueName(fields.name.trim() || "Control Scheme", names);
    const groups = draft.schemes
      .filter((_, index) => index !== schemeIndex)
      .map((item) => item.bindingGroup ?? item.name)
      .filter((group) => group.length > 0);
    const requested = fields.bindingGroup.trim();
    const requestedGroup =
      !requested || requested.toLowerCase() === oldGroup.toLowerCase()
        ? name
        : requested;
    const bindingGroup = uniqueName(requestedGroup, groups);
    if (oldGroup && oldGroup.toLowerCase() !== bindingGroup.toLowerCase()) {
      for (const map of draft.maps) {
        for (const binding of map.bindings) {
          const groupsForBinding = splitBindingGroups(binding.groups).map((group) =>
            group.toLowerCase() === oldGroup.toLowerCase() ? bindingGroup : group,
          );
          binding.groups = joinBindingGroups(groupsForBinding);
        }
      }
    }
    scheme.name = name;
    scheme.bindingGroup = bindingGroup;
    const devices = fields.devices.map((device) => device.trim()).filter(Boolean);
    scheme.devices = devices.length > 0 ? devices : ["Keyboard"];
    scheme.isOptional = fields.isOptional || undefined;
  });
}

export function setBindingScheme(
  asset: AssetJson,
  mapIndex: number,
  bindingIndex: number,
  group: string,
  enabled: boolean,
): AssetJson {
  return editAsset(asset, (draft) => {
    const binding = draft.maps[mapIndex]?.bindings[bindingIndex];
    if (!binding) {
      return;
    }
    const groups = splitBindingGroups(binding.groups).filter(
      (value) => value.toLowerCase() !== group.toLowerCase(),
    );
    if (enabled) {
      groups.push(group);
    }
    binding.groups = joinBindingGroups(groups);
  });
}

export function setAssetName(asset: AssetJson, name: string): AssetJson {
  return editAsset(asset, (draft) => {
    draft.name = name.trim() || undefined;
  });
}

export function selectionLabel(asset: AssetJson, selection: Selection): string {
  if (selection.kind === "map") {
    return asset.maps[selection.mapIndex]?.name ?? "Action map";
  }
  if (selection.kind === "action") {
    return asset.maps[selection.mapIndex]?.actions[selection.actionIndex]?.name ?? "Action";
  }
  if (selection.kind === "binding") {
    return asset.maps[selection.mapIndex]?.bindings[selection.bindingIndex]?.name ?? "Binding";
  }
  if (selection.kind === "scheme") {
    return asset.schemes?.[selection.schemeIndex]?.name ?? "Control scheme";
  }
  return "Asset";
}

export function selectedAction(
  asset: AssetJson,
  selection: Selection,
): ActionJson | undefined {
  if (selection.kind !== "action") {
    return undefined;
  }
  return asset.maps[selection.mapIndex]?.actions[selection.actionIndex];
}

export function selectedMap(asset: AssetJson, selection: Selection): ActionMapJson | undefined {
  if (selection.kind === "map" || selection.kind === "action" || selection.kind === "binding") {
    return asset.maps[selection.mapIndex];
  }
  return undefined;
}

export function selectedBinding(
  asset: AssetJson,
  selection: Selection,
): BindingJson | undefined {
  if (selection.kind !== "binding") {
    return undefined;
  }
  return asset.maps[selection.mapIndex]?.bindings[selection.bindingIndex];
}

export function selectedScheme(
  asset: AssetJson,
  selection: Selection,
): ControlSchemeJson | undefined {
  if (selection.kind !== "scheme") {
    return undefined;
  }
  return asset.schemes?.[selection.schemeIndex];
}
