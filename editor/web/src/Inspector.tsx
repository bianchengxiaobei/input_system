import { type ReactNode, useEffect, useRef, useState } from "react";

import {
  type ActionJson,
  type AssetJson,
  type BindingJson,
  type ControlSchemeJson,
  type Selection,
  actionTypeControlDefaults,
  hasBindingGroup,
  parseDescriptor,
  splitBindingGroups,
  updateDescriptorParameter,
} from "./asset";
import {
  bindingExpectedControlType,
  compositePartControlType,
  derivedBindings,
  validateControlPath,
} from "./preview";
import {
  type EditorCatalog,
  controlPathSuggestions,
  findComposite,
  namedValues,
} from "./catalog";
import {
  changeCompositeType,
  renameActionMap,
  selectedAction,
  selectedBinding,
  selectedMap,
  selectedScheme,
  setAssetName,
  setBindingScheme,
  updateAction,
  updateBinding,
  updateControlScheme,
} from "./commands";

interface InspectorProps {
  asset: AssetJson;
  selection: Selection;
  catalog: EditorCatalog;
  onChange: (label: string, next: AssetJson) => void;
  onListen?: (request: CaptureRequest, apply: (path: string) => void) => void;
}
export interface CaptureRequest {
  expectedControlType: string;
  deviceFilter: string;
  label: string;
}

function CommittedInput({
  value,
  onCommit,
  placeholder,
  list,
  type = "text",
  disabled,
}: {
  value: string;
  onCommit: (value: string) => void;
  placeholder?: string;
  list?: string;
  type?: "text" | "number";
  disabled?: boolean;
}) {
  const [draft, setDraft] = useState(value);
  const draftRef = useRef(value);
  const cancelledRef = useRef(false);
  useEffect(() => {
    draftRef.current = value;
    setDraft(value);
  }, [value]);
  return (
    <input
      className="field-input"
      type={type}
      value={draft}
      placeholder={placeholder}
      list={list}
      disabled={disabled}
      step={type === "number" ? "any" : undefined}
      onChange={(event) => {
        draftRef.current = event.target.value;
        setDraft(event.target.value);
      }}
      onBlur={() => {
        if (cancelledRef.current) {
          cancelledRef.current = false;
          draftRef.current = value;
          setDraft(value);
          return;
        }
        if (draftRef.current !== value) {
          onCommit(draftRef.current);
        }
      }}
      onKeyDown={(event) => {
        if (event.key === "Enter") {
          event.currentTarget.blur();
        } else if (event.key === "Escape") {
          cancelledRef.current = true;
          draftRef.current = value;
          setDraft(value);
          event.currentTarget.blur();
        }
      }}
    />
  );
}

function Field({
  label,
  hint,
  children,
}: {
  label: string;
  hint?: string;
  children: ReactNode;
}) {
  return (
    <label className="field">
      <span className="field-label">
        {label}
        {hint ? <span className="field-hint">{hint}</span> : null}
      </span>
      {children}
    </label>
  );
}

function Checkbox({
  label,
  checked,
  onChange,
}: {
  label: string;
  checked: boolean;
  onChange: (checked: boolean) => void;
}) {
  return (
    <label className="check-field">
      <input
        type="checkbox"
        checked={checked}
        onChange={(event) => onChange(event.target.checked)}
      />
      <span>{label}</span>
    </label>
  );
}

function optionsWithCurrent(options: string[], current: string): string[] {
  if (!current || options.includes(current)) {
    return options;
  }
  return [current, ...options];
}

function Datalists({ catalog }: { catalog: EditorCatalog }) {
  const controls = controlPathSuggestions(catalog);
  return (
    <>
      <datalist id="control-path-list">
        {controls.map((path) => (
          <option key={path} value={path} />
        ))}
      </datalist>
      <datalist id="interaction-list">
        {namedValues(catalog, "interactions").map((name) => (
          <option key={name} value={name} />
        ))}
      </datalist>
      <datalist id="processor-list">
        {namedValues(catalog, "processors").map((name) => (
          <option key={name} value={name} />
        ))}
      </datalist>
      <datalist id="device-list">
        {catalog.devices.map((device) => (
          <option key={device} value={device} />
        ))}
      </datalist>
    </>
  );
}

function AssetInspector({
  asset,
  onChange,
}: {
  asset: AssetJson;
  onChange: InspectorProps["onChange"];
}) {
  const actionCount = asset.maps.reduce((count, map) => count + map.actions.length, 0);
  const bindingCount = asset.maps.reduce((count, map) => count + map.bindings.length, 0);
  return (
    <div className="inspector-body">
      <div className="inspector-title">
        <div>
          <h3>{asset.name || "Untitled asset"}</h3>
          <p className="subtle">
            {asset.maps.length} maps | {actionCount} actions | {bindingCount} bindings |{" "}
            {asset.schemes?.length ?? 0} schemes
          </p>
        </div>
      </div>
      <div className="form-grid">
        <Field label="Asset name">
          <CommittedInput
            value={asset.name ?? ""}
            onCommit={(value) =>
              onChange("Rename asset", setAssetName(asset, value))
            }
            placeholder="Player controls"
          />
        </Field>
      </div>
      <p className="inspector-help">
        Select an action map, action, binding, or control scheme in the tree to edit it.
        Ctrl+Z / Ctrl+Shift+Z undo and redo; Ctrl+D duplicates the current selection.
      </p>
    </div>
  );
}

function MapInspector({
  asset,
  mapIndex,
  onChange,
}: {
  asset: AssetJson;
  mapIndex: number;
  onChange: InspectorProps["onChange"];
}) {
  const map = selectedMap(asset, { kind: "map", mapIndex });
  if (!map) {
    return null;
  }
  return (
    <div className="inspector-body">
      <div className="inspector-title">
        <div>
          <h3>{map.name}</h3>
          <p className="subtle">Action map</p>
        </div>
      </div>
      <div className="form-grid">
        <Field label="Name">
          <CommittedInput
            value={map.name}
            onCommit={(value) =>
              onChange("Rename action map", renameActionMap(asset, mapIndex, value))
            }
          />
        </Field>
        <Field label="Actions">
          <input className="field-input" value={map.actions.length} readOnly />
        </Field>
        <Field label="Bindings">
          <input className="field-input" value={map.bindings.length} readOnly />
        </Field>
      </div>
    </div>
  );
}

function ActionInspector({
  asset,
  mapIndex,
  actionIndex,
  catalog,
  onChange,
}: {
  asset: AssetJson;
  mapIndex: number;
  actionIndex: number;
  catalog: EditorCatalog;
  onChange: InspectorProps["onChange"];
}) {
  const action = selectedAction(asset, { kind: "action", mapIndex, actionIndex });
  if (!action) {
    return null;
  }
  const map = asset.maps[mapIndex];
  const bindingCount = map.bindings.filter(
    (binding) =>
      binding.action.split("/").pop()?.toLowerCase() === action.name.toLowerCase(),
  ).length;

  const update = (label: string, patch: Partial<ActionJson>) =>
    onChange(label, updateAction(asset, mapIndex, actionIndex, patch));

  return (
    <div className="inspector-body">
      <div className="inspector-title">
        <div>
          <h3>{action.name}</h3>
          <p className="subtle">
            {action.type} action | {bindingCount} binding{bindingCount === 1 ? "" : "s"}
          </p>
        </div>
      </div>
      <div className="form-grid">
        <Field label="Name">
          <CommittedInput
            value={action.name}
            onCommit={(value) => update("Rename action", { name: value })}
          />
        </Field>
        <Field label="Action type">
          <select
            className="field-input"
            value={action.type}
            onChange={(event) =>
              update("Change action type", {
                type: event.target.value as ActionJson["type"],
              })
            }
          >
            <option value="button">Button</option>
            <option value="value">Value</option>
            <option value="passthrough">PassThrough</option>
          </select>
        </Field>
        <Field label="Control type">
          <select
            className="field-input"
            value={action.control ?? ""}
            onChange={(event) =>
              update("Change control type", { control: event.target.value })
            }
          >
            <option value="">Any</option>
            {optionsWithCurrent(
              catalog.controlTypes.filter((type) => type !== "Any"),
              action.control ?? "",
            ).map((type) => (
              <option key={type} value={type}>
                {type}
              </option>
            ))}
          </select>
        </Field>
        <Field label="Priority" hint="higher values win">
          <CommittedInput
            type="number"
            value={String(action.priority ?? 0)}
            onCommit={(value) => {
              const priority = Number(value);
              if (Number.isInteger(priority)) {
                update("Change action priority", { priority });
              }
            }}
          />
        </Field>
        <div className="field">
          <Checkbox
            label="Initial state check"
            checked={Boolean(action.initialStateCheck)}
            onChange={(checked) =>
              update("Change initial state check", { initialStateCheck: checked })
            }
          />
        </div>
        <Field label="Interactions" hint="semicolon separated">
          <CommittedInput
            value={action.interactions ?? ""}
            list="interaction-list"
            placeholder="Hold;Tap"
            onCommit={(value) => update("Change action interactions", { interactions: value })}
          />
        </Field>
        <Field label="Processors" hint="semicolon separated">
          <CommittedInput
            value={action.processors ?? ""}
            list="processor-list"
            placeholder="Invert;AxisDeadzone"
            onCommit={(value) => update("Change action processors", { processors: value })}
          />
        </Field>
      </div>
      {!action.control && action.type !== "passthrough" ? (
        <button
          type="button"
          className="small-button"
          onClick={() =>
            update("Set default control type", {
              control: actionTypeControlDefaults(action.type),
            })
          }
        >
          Set default control type
        </button>
      ) : null}
    </div>
  );
}

function BindingGroups({
  asset,
  mapIndex,
  binding,
  bindingIndex,
  onChange,
}: {
  asset: AssetJson;
  mapIndex: number;
  binding: BindingJson;
  bindingIndex: number;
  onChange: InspectorProps["onChange"];
}) {
  const schemes = asset.schemes ?? [];
  if (schemes.length === 0) {
    return <p className="inspector-help">Add a control scheme to associate bindings with devices.</p>;
  }
  return (
    <div className="scheme-checkboxes">
      {schemes.map((scheme, schemeIndex) => {
        const group = scheme.bindingGroup || scheme.name;
        return (
          <Checkbox
            key={scheme.id ?? `${schemeIndex}`}
            label={`Use in ${scheme.name}`}
            checked={hasBindingGroup(binding.groups, group)}
            onChange={(checked) =>
              onChange(
                "Change binding control schemes",
                setBindingScheme(asset, mapIndex, bindingIndex, group, checked),
              )
            }
          />
        );
      })}
    </div>
  );
}

function CompositeEditor({
  asset,
  mapIndex,
  binding,
  bindingIndex,
  catalog,
  onChange,
  onListen,
}: {
  asset: AssetJson;
  mapIndex: number;
  binding: BindingJson;
  bindingIndex: number;
  catalog: EditorCatalog;
  onChange: InspectorProps["onChange"];
  onListen?: InspectorProps["onListen"];
}) {
  const descriptor = parseDescriptor(binding.composite ?? "");
  const composite = findComposite(catalog, descriptor.name);
  const partNames = Array.from(
    new Set([...(composite?.parts ?? []), ...Object.keys(binding.parts ?? {})]),
  );
  const update = (label: string, patch: Partial<BindingJson>) =>
    onChange(label, updateBinding(asset, mapIndex, bindingIndex, patch));

  return (
    <div className="subsection">
      <h4>Composite</h4>
      <div className="form-grid">
        <Field label="Type">
          <select
            className="field-input"
            value={descriptor.name}
            onChange={(event) => {
              const nextName = event.target.value;
              const nextComposite = findComposite(catalog, nextName);
              onChange(
                "Change composite type",
                changeCompositeType(
                  asset,
                  mapIndex,
                  bindingIndex,
                  nextName,
                  nextComposite?.parts ?? Object.keys(binding.parts ?? {}),
                ),
              );
            }}
          >
            {optionsWithCurrent(
              catalog.composites.map((item) => item.name),
              descriptor.name,
            ).map((name) => (
              <option key={name} value={name}>
                {catalog.composites.find((item) => item.name === name)?.displayName ?? name}
              </option>
            ))}
          </select>
        </Field>
        <Field label="Descriptor">
          <CommittedInput
            value={binding.composite ?? ""}
            onCommit={(value) => update("Change composite descriptor", { composite: value })}
          />
        </Field>
      </div>

      {composite && composite.parameters.length > 0 ? (
        <div className="form-grid">
          {composite.parameters.map((parameter) => {
            const value = descriptor.parameters[parameter.name] ?? parameter.default;
            const commit = (nextValue: string) =>
              update(
                "Change composite parameter",
                {
                  composite: updateDescriptorParameter(
                    binding.composite ?? composite.name,
                    parameter.name,
                    nextValue,
                  ),
                },
              );
            if (parameter.type === "enum") {
              const options = parameter.options ?? [];
              const selected = options.includes(value) ? value : "0";
              return (
                <Field key={parameter.name} label={parameter.displayName}>
                  <select
                    className="field-input"
                    value={selected}
                    onChange={(event) => commit(event.target.value)}
                  >
                    {options.map((option, index) => (
                      <option key={option} value={String(index)}>
                        {option}
                      </option>
                    ))}
                  </select>
                </Field>
              );
            }
            if (parameter.type === "bool") {
              return (
                <Field key={parameter.name} label={parameter.displayName}>
                  <select
                    className="field-input"
                    value={value === "true" ? "true" : "false"}
                    onChange={(event) => commit(event.target.value)}
                  >
                    <option value="false">False</option>
                    <option value="true">True</option>
                  </select>
                </Field>
              );
            }
            return (
              <Field key={parameter.name} label={parameter.displayName}>
                <CommittedInput
                  type="number"
                  value={value}
                  onCommit={commit}
                />
              </Field>
            );
          })}
        </div>
      ) : null}

      {!composite ? (
        <p className="inline-warning">
          Unknown composite type. The C++ validator will reject this asset until it is fixed.
        </p>
      ) : null}

      <div className="parts-list">
        {partNames.map((part) => (
          <div className="part-row" key={part}>
            <span className="part-name">{part}</span>
            <div className="path-input-row">
              <CommittedInput
                value={binding.parts?.[part] ?? ""}
                list="control-path-list"
                placeholder="<Gamepad>/leftStick/x"
                onCommit={(value) =>
                  update("Change composite part", {
                    parts: {
                      ...(binding.parts ?? {}),
                      [part]: value,
                    },
                  })
                }
              />
              {onListen ? (
                <button
                  type="button"
                  className="small-button listen-button"
                  onClick={() =>
                    onListen(
                      {
                        expectedControlType: compositePartControlType(descriptor.name),
                        deviceFilter: "",
                        label: `composite part ${part}`,
                      },
                      (value) =>
                        update("Capture composite part", {
                          parts: {
                            ...(binding.parts ?? {}),
                            [part]: value,
                          },
                        }),
                    )
                  }
                >
                  Listen
                </button>
              ) : null}
            </div>
          </div>
        ))}
      </div>
    </div>
  );
}

function BindingInspector({
  asset,
  mapIndex,
  bindingIndex,
  catalog,
  onChange,
  onListen,
}: {
  asset: AssetJson;
  mapIndex: number;
  bindingIndex: number;
  catalog: EditorCatalog;
  onChange: InspectorProps["onChange"];
  onListen?: InspectorProps["onListen"];
}) {
  const binding = selectedBinding(asset, { kind: "binding", mapIndex, bindingIndex });
  if (!binding) {
    return null;
  }
  const update = (label: string, patch: Partial<BindingJson>) =>
    onChange(label, updateBinding(asset, mapIndex, bindingIndex, patch));
  const expectedControlType = bindingExpectedControlType(asset, mapIndex, bindingIndex);
  const pathError =
    binding.composite === undefined
      ? validateControlPath(catalog, binding.path ?? "", expectedControlType)
      : null;
  const derived = derivedBindings(asset, mapIndex, bindingIndex);

  return (
    <div className="inspector-body">
      <div className="inspector-title">
        <div>
          <h3>{binding.name || (binding.composite ? "Composite binding" : "Binding")}</h3>
          <p className="subtle">Action: {binding.action}</p>
        </div>
      </div>
      <div className="form-grid">
        <Field label="Name">
          <CommittedInput
            value={binding.name ?? ""}
            placeholder="Optional"
            onCommit={(value) => update("Rename binding", { name: value })}
          />
        </Field>
        {binding.composite === undefined ? (
          <Field label="Control path" hint={expectedControlType || "any control"}>
            <div className="path-input-row">
              <CommittedInput
                value={binding.path ?? ""}
                list="control-path-list"
                placeholder="<Keyboard>/space"
                onCommit={(value) => update("Change binding path", { path: value })}
              />
              {onListen ? (
                <button
                  type="button"
                  className="small-button listen-button"
                  onClick={() =>
                    onListen(
                      {
                        expectedControlType,
                        deviceFilter: "",
                        label: `${binding.action} path`,
                      },
                      (value) => update("Capture binding path", { path: value }),
                    )
                  }
                >
                  Listen
                </button>
              ) : null}
            </div>
            {pathError && (binding.path ?? "").trim() ? (
              <span className="field-warning">{pathError}</span>
            ) : null}
          </Field>
        ) : null}
        <Field label="Groups" hint="semicolon separated">
          <CommittedInput
            value={binding.groups ?? ""}
            placeholder="Keyboard&Mouse"
            onCommit={(value) => update("Change binding groups", { groups: value })}
          />
        </Field>
        <Field label="Interactions" hint="semicolon separated">
          <CommittedInput
            value={binding.interactions ?? ""}
            list="interaction-list"
            placeholder="Tap"
            onCommit={(value) => update("Change binding interactions", { interactions: value })}
          />
        </Field>
        <Field label="Processors" hint="semicolon separated">
          <CommittedInput
            value={binding.processors ?? ""}
            list="processor-list"
            placeholder="StickDeadzone"
            onCommit={(value) => update("Change binding processors", { processors: value })}
          />
        </Field>
      </div>

      {binding.composite !== undefined ? (
        <CompositeEditor
          asset={asset}
          mapIndex={mapIndex}
          binding={binding}
          bindingIndex={bindingIndex}
          catalog={catalog}
          onChange={onChange}
          onListen={onListen}
        />
      ) : null}

      <div className="subsection">
        <h4>Derived bindings</h4>
        {derived.length === 0 ? (
          <p className="subtle">No other bindings use this control path.</p>
        ) : (
          <div className="derived-list">
            {derived.map((item) => (
              <div className="derived-row" key={`${item.mapIndex}/${item.bindingIndex}`}>
                <span className="badge">{item.map}</span>
                <span>{item.action}</span>
                <span className="derived-path">{item.path}</span>
                {item.name ? <span className="count">{item.name}</span> : null}
              </div>
            ))}
          </div>
        )}
      </div>

      <div className="subsection">
        <h4>Use in control scheme</h4>
        <BindingGroups
          asset={asset}
          mapIndex={mapIndex}
          binding={binding}
          bindingIndex={bindingIndex}
          onChange={onChange}
        />
        {splitBindingGroups(binding.groups).length > 0 ? (
          <p className="subtle">Groups: {splitBindingGroups(binding.groups).join(", ")}</p>
        ) : null}
      </div>
    </div>
  );
}

function SchemeInspector({
  asset,
  schemeIndex,
  catalog,
  onChange,
}: {
  asset: AssetJson;
  schemeIndex: number;
  catalog: EditorCatalog;
  onChange: InspectorProps["onChange"];
}) {
  const scheme = selectedScheme(asset, { kind: "scheme", schemeIndex });
  if (!scheme) {
    return null;
  }
  const update = (fields: {
    name: string;
    bindingGroup: string;
    devices: string[];
    isOptional: boolean;
  }) => onChange("Edit control scheme", updateControlScheme(asset, schemeIndex, fields));
  const current = {
    name: scheme.name,
    bindingGroup: scheme.bindingGroup ?? scheme.name,
    devices: scheme.devices,
    isOptional: Boolean(scheme.isOptional),
  };
  const deviceOptions = catalog.devices;
  const availableDevice =
    deviceOptions.find((device) => !current.devices.includes(device)) ?? "Keyboard";

  return (
    <div className="inspector-body">
      <div className="inspector-title">
        <div>
          <h3>{scheme.name}</h3>
          <p className="subtle">Control scheme</p>
        </div>
      </div>
      <div className="form-grid">
        <Field label="Name">
          <CommittedInput
            value={scheme.name}
            onCommit={(value) => update({ ...current, name: value })}
          />
        </Field>
        <Field label="Binding group">
          <CommittedInput
            value={current.bindingGroup}
            onCommit={(value) => update({ ...current, bindingGroup: value })}
          />
        </Field>
        <div className="field">
          <Checkbox
            label="Optional"
            checked={current.isOptional}
            onChange={(checked) => update({ ...current, isOptional: checked })}
          />
        </div>
      </div>

      <div className="subsection">
        <h4>Required devices</h4>
        <div className="device-list">
          {current.devices.map((device, deviceIndex) => (
            <div className="device-row" key={`${device}-${deviceIndex}`}>
              <CommittedInput
                value={device}
                list="device-list"
                onCommit={(value) => {
                  const devices = current.devices.slice();
                  devices[deviceIndex] = value;
                  update({ ...current, devices });
                }}
              />
              <button
                type="button"
                className="small-button"
                disabled={current.devices.length === 1}
                onClick={() =>
                  update({
                    ...current,
                    devices: current.devices.filter((_, index) => index !== deviceIndex),
                  })
                }
              >
                Remove
              </button>
            </div>
          ))}
        </div>
        <button
          type="button"
          className="small-button"
          onClick={() => update({ ...current, devices: [...current.devices, availableDevice] })}
        >
          + Device
        </button>
      </div>

      <p className="inspector-help">
        Bindings are included in this scheme when their groups contain{" "}
        <code>{current.bindingGroup || current.name}</code>.
      </p>
    </div>
  );
}

export default function Inspector({
  asset,
  selection,
  catalog,
  onChange,
  onListen,
}: InspectorProps) {
  let content: ReactNode = <AssetInspector asset={asset} onChange={onChange} />;
  if (selection.kind === "map") {
    content = (
      <MapInspector asset={asset} mapIndex={selection.mapIndex} onChange={onChange} />
    );
  } else if (selection.kind === "action") {
    content = (
      <ActionInspector
        asset={asset}
        mapIndex={selection.mapIndex}
        actionIndex={selection.actionIndex}
        catalog={catalog}
        onChange={onChange}
      />
    );
  } else if (selection.kind === "binding") {
    content = (
      <BindingInspector
        asset={asset}
        mapIndex={selection.mapIndex}
        bindingIndex={selection.bindingIndex}
        catalog={catalog}
        onChange={onChange}
        onListen={onListen}
      />
    );
  } else if (selection.kind === "scheme") {
    content = (
      <SchemeInspector
        asset={asset}
        schemeIndex={selection.schemeIndex}
        catalog={catalog}
        onChange={onChange}
      />
    );
  }

  return (
    <div className="inspector-panel">
      <Datalists catalog={catalog} />
      {content}
    </div>
  );
}
