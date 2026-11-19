import { useEffect, useMemo, useRef, useState } from "react";

import {
  type AssetJson,
  type BindingJson,
  type Selection,
  bindingsForAction,
} from "./asset";

export type TreeCommand =
  | { type: "addMap" }
  | { type: "duplicateMap"; mapIndex: number }
  | { type: "deleteMap"; mapIndex: number }
  | { type: "moveMap"; from: number; to: number }
  | { type: "addAction"; mapIndex: number }
  | { type: "duplicateAction"; mapIndex: number; actionIndex: number }
  | { type: "deleteAction"; mapIndex: number; actionIndex: number }
  | { type: "moveAction"; mapIndex: number; from: number; to: number }
  | { type: "addBinding"; mapIndex: number; actionName: string }
  | { type: "addComposite"; mapIndex: number; actionName: string }
  | { type: "duplicateBinding"; mapIndex: number; bindingIndex: number }
  | { type: "deleteBinding"; mapIndex: number; bindingIndex: number }
  | { type: "moveBinding"; mapIndex: number; from: number; to: number }
  | { type: "addScheme" }
  | { type: "duplicateScheme"; schemeIndex: number }
  | { type: "deleteScheme"; schemeIndex: number }
  | { type: "moveScheme"; from: number; to: number };

export type TreeRow =
  | { kind: "asset" }
  | { kind: "map"; mapIndex: number }
  | { kind: "action"; mapIndex: number; actionIndex: number }
  | { kind: "binding"; mapIndex: number; bindingIndex: number }
  | { kind: "empty-binding"; mapIndex: number; actionIndex: number }
  | { kind: "schemes-header" }
  | { kind: "scheme"; schemeIndex: number }
  | { kind: "empty" };

interface TreePanelProps {
  asset: AssetJson | null;
  selection: Selection;
  onSelect: (selection: Selection) => void;
  onCommand: (command: TreeCommand) => void;
}

export const TREE_ROW_HEIGHT = 34;
const OVERSCAN_ROWS = 6;

export function buildTreeRows(asset: AssetJson): TreeRow[] {
  const rows: TreeRow[] = [{ kind: "asset" }];
  asset.maps.forEach((map, mapIndex) => {
    rows.push({ kind: "map", mapIndex });
    map.actions.forEach((action, actionIndex) => {
      rows.push({ kind: "action", mapIndex, actionIndex });
      const bindings = bindingsForAction(map, action.name);
      if (bindings.length === 0) {
        rows.push({ kind: "empty-binding", mapIndex, actionIndex });
        return;
      }
      for (const { index } of bindings) {
        rows.push({ kind: "binding", mapIndex, bindingIndex: index });
      }
    });
  });
  if ((asset.schemes?.length ?? 0) > 0) {
    rows.push({ kind: "schemes-header" });
    asset.schemes?.forEach((_scheme, schemeIndex) => {
      rows.push({ kind: "scheme", schemeIndex });
    });
  }
  if (asset.maps.length === 0) {
    rows.push({ kind: "empty" });
  }
  return rows;
}

export function visibleTreeRange(
  rowCount: number,
  scrollTop: number,
  viewportHeight: number,
  rowHeight = TREE_ROW_HEIGHT,
  overscan = OVERSCAN_ROWS,
): { start: number; end: number } {
  const start = Math.max(0, Math.floor(scrollTop / rowHeight) - overscan);
  const end = Math.min(
    rowCount,
    Math.ceil((scrollTop + Math.max(viewportHeight, rowHeight)) / rowHeight) +
      overscan,
  );
  return { start, end: Math.max(start, end) };
}

function bindingLabel(binding: BindingJson): string {
  if (binding.name) {
    return binding.name;
  }
  if (binding.composite) {
    return `${binding.composite}`;
  }
  return binding.path ?? "Binding";
}

function bindingDetail(binding: BindingJson): string {
  if (binding.composite) {
    const parts = binding.parts ? Object.keys(binding.parts).length : 0;
    return parts === 1 ? "1 part" : `${parts} parts`;
  }
  if (binding.name) {
    return binding.path ?? "";
  }
  return binding.groups ? `groups: ${binding.groups}` : "";
}

function isSelected(selection: Selection, candidate: Selection): boolean {
  if (selection.kind !== candidate.kind) {
    return false;
  }
  if (selection.kind === "map" && candidate.kind === "map") {
    return selection.mapIndex === candidate.mapIndex;
  }
  if (selection.kind === "action" && candidate.kind === "action") {
    return (
      selection.mapIndex === candidate.mapIndex &&
      selection.actionIndex === candidate.actionIndex
    );
  }
  if (selection.kind === "binding" && candidate.kind === "binding") {
    return (
      selection.mapIndex === candidate.mapIndex &&
      selection.bindingIndex === candidate.bindingIndex
    );
  }
  if (selection.kind === "scheme" && candidate.kind === "scheme") {
    return selection.schemeIndex === candidate.schemeIndex;
  }
  return false;
}

function RowButton({
  label,
  title,
  disabled,
  onClick,
}: {
  label: string;
  title: string;
  disabled?: boolean;
  onClick: () => void;
}) {
  return (
    <button
      type="button"
      className="row-action"
      title={title}
      aria-label={title}
      disabled={disabled}
      onClick={(event) => {
        event.stopPropagation();
        onClick();
      }}
    >
      {label}
    </button>
  );
}

export default function TreePanel({
  asset,
  selection,
  onSelect,
  onCommand,
}: TreePanelProps) {
  const treeRef = useRef<HTMLDivElement>(null);
  const [scrollTop, setScrollTop] = useState(0);
  const [viewportHeight, setViewportHeight] = useState(480);
  const rows = useMemo(() => (asset ? buildTreeRows(asset) : []), [asset]);

  useEffect(() => {
    const element = treeRef.current;
    if (!element) {
      return;
    }
    const measure = () => setViewportHeight(element.clientHeight || 480);
    measure();
    if (typeof ResizeObserver === "undefined") {
      return;
    }
    const observer = new ResizeObserver(measure);
    observer.observe(element);
    return () => observer.disconnect();
  }, [asset]);

  if (!asset) {
    return (
      <aside className="panel browser-panel">
        <div className="panel-heading">
          <h2>Asset</h2>
        </div>
        <p className="empty">Open or create an asset to start editing.</p>
      </aside>
    );
  }

  const range = visibleTreeRange(rows.length, scrollTop, viewportHeight);
  const visibleRows = rows.slice(range.start, range.end);

  const renderRow = (row: TreeRow) => {
    if (row.kind === "asset") {
      return (
        <div className="asset-name virtual-asset-name">
          {asset.name || "Untitled asset"}
        </div>
      );
    }

    if (row.kind === "empty") {
      return <p className="empty virtual-empty">No action maps. Add one to begin.</p>;
    }

    if (row.kind === "schemes-header") {
      return <div className="tree-row map-row">Control Schemes</div>;
    }

    if (row.kind === "scheme") {
      const scheme = asset.schemes?.[row.schemeIndex];
      if (!scheme) {
        return null;
      }
      const schemeSelection: Selection = {
        kind: "scheme",
        schemeIndex: row.schemeIndex,
      };
      return (
        <div
          className={`tree-row action-row ${isSelected(selection, schemeSelection) ? "selected" : ""}`}
          onClick={() => onSelect(schemeSelection)}
        >
          <span className="tree-label">
            {scheme.name}
            <span className="type-pill">{scheme.devices.join(" + ")}</span>
          </span>
          <span className="row-actions">
            <RowButton
              label="^"
              title="Move scheme up"
              disabled={row.schemeIndex === 0}
              onClick={() =>
                onCommand({
                  type: "moveScheme",
                  from: row.schemeIndex,
                  to: row.schemeIndex - 1,
                })
              }
            />
            <RowButton
              label="v"
              title="Move scheme down"
              disabled={row.schemeIndex === (asset.schemes?.length ?? 1) - 1}
              onClick={() =>
                onCommand({
                  type: "moveScheme",
                  from: row.schemeIndex,
                  to: row.schemeIndex + 1,
                })
              }
            />
            <RowButton
              label="Dup"
              title="Duplicate scheme"
              onClick={() =>
                onCommand({ type: "duplicateScheme", schemeIndex: row.schemeIndex })
              }
            />
            <RowButton
              label="Del"
              title="Delete scheme"
              onClick={() =>
                onCommand({ type: "deleteScheme", schemeIndex: row.schemeIndex })
              }
            />
          </span>
        </div>
      );
    }

    const map = asset.maps[row.mapIndex];
    if (!map) {
      return null;
    }

    if (row.kind === "map") {
      const mapSelection: Selection = { kind: "map", mapIndex: row.mapIndex };
      return (
        <div
          className={`tree-row map-row ${isSelected(selection, mapSelection) ? "selected" : ""}`}
          onClick={() => onSelect(mapSelection)}
        >
          <span className="tree-label">{map.name}</span>
          <span className="row-actions">
            <RowButton
              label="+"
              title="Add action"
              onClick={() => onCommand({ type: "addAction", mapIndex: row.mapIndex })}
            />
            <RowButton
              label="^"
              title="Move map up"
              disabled={row.mapIndex === 0}
              onClick={() =>
                onCommand({
                  type: "moveMap",
                  from: row.mapIndex,
                  to: row.mapIndex - 1,
                })
              }
            />
            <RowButton
              label="v"
              title="Move map down"
              disabled={row.mapIndex === asset.maps.length - 1}
              onClick={() =>
                onCommand({
                  type: "moveMap",
                  from: row.mapIndex,
                  to: row.mapIndex + 1,
                })
              }
            />
            <RowButton
              label="Dup"
              title="Duplicate map"
              onClick={() => onCommand({ type: "duplicateMap", mapIndex: row.mapIndex })}
            />
            <RowButton
              label="Del"
              title="Delete map"
              onClick={() => onCommand({ type: "deleteMap", mapIndex: row.mapIndex })}
            />
          </span>
        </div>
      );
    }

    if (row.kind === "action") {
      const action = map.actions[row.actionIndex];
      if (!action) {
        return null;
      }
      const actionSelection: Selection = {
        kind: "action",
        mapIndex: row.mapIndex,
        actionIndex: row.actionIndex,
      };
      return (
        <div
          className={`tree-row action-row ${isSelected(selection, actionSelection) ? "selected" : ""}`}
          onClick={() => onSelect(actionSelection)}
        >
          <span className="tree-label">
            {action.name}
            <span className="type-pill">{action.type}</span>
          </span>
          <span className="row-actions">
            <RowButton
              label="+"
              title="Add binding"
              onClick={() =>
                onCommand({
                  type: "addBinding",
                  mapIndex: row.mapIndex,
                  actionName: action.name,
                })
              }
            />
            <RowButton
              label="C"
              title="Add composite"
              onClick={() =>
                onCommand({
                  type: "addComposite",
                  mapIndex: row.mapIndex,
                  actionName: action.name,
                })
              }
            />
            <RowButton
              label="^"
              title="Move action up"
              disabled={row.actionIndex === 0}
              onClick={() =>
                onCommand({
                  type: "moveAction",
                  mapIndex: row.mapIndex,
                  from: row.actionIndex,
                  to: row.actionIndex - 1,
                })
              }
            />
            <RowButton
              label="v"
              title="Move action down"
              disabled={row.actionIndex === map.actions.length - 1}
              onClick={() =>
                onCommand({
                  type: "moveAction",
                  mapIndex: row.mapIndex,
                  from: row.actionIndex,
                  to: row.actionIndex + 1,
                })
              }
            />
            <RowButton
              label="Dup"
              title="Duplicate action"
              onClick={() =>
                onCommand({
                  type: "duplicateAction",
                  mapIndex: row.mapIndex,
                  actionIndex: row.actionIndex,
                })
              }
            />
            <RowButton
              label="Del"
              title="Delete action"
              onClick={() =>
                onCommand({
                  type: "deleteAction",
                  mapIndex: row.mapIndex,
                  actionIndex: row.actionIndex,
                })
              }
            />
          </span>
        </div>
      );
    }

    if (row.kind === "empty-binding") {
      return (
        <div className="tree-row binding-row muted virtual-binding-row">
          <span>No bindings</span>
        </div>
      );
    }

    const binding = map.bindings[row.bindingIndex];
    if (!binding) {
      return null;
    }
    const bindingSelection: Selection = {
      kind: "binding",
      mapIndex: row.mapIndex,
      bindingIndex: row.bindingIndex,
    };
    return (
      <div
        className={`tree-row binding-row virtual-binding-row ${isSelected(selection, bindingSelection) ? "selected" : ""}`}
        onClick={() => onSelect(bindingSelection)}
      >
        <span className="binding-label">
          <span className="binding-name">{bindingLabel(binding)}</span>
          <span className="binding-detail">{bindingDetail(binding)}</span>
        </span>
        <span className="row-actions">
          <RowButton
            label="^"
            title="Move binding up"
            disabled={row.bindingIndex === 0}
            onClick={() =>
              onCommand({
                type: "moveBinding",
                mapIndex: row.mapIndex,
                from: row.bindingIndex,
                to: row.bindingIndex - 1,
              })
            }
          />
          <RowButton
            label="v"
            title="Move binding down"
            disabled={row.bindingIndex === map.bindings.length - 1}
            onClick={() =>
              onCommand({
                type: "moveBinding",
                mapIndex: row.mapIndex,
                from: row.bindingIndex,
                to: row.bindingIndex + 1,
              })
            }
          />
          <RowButton
            label="Dup"
            title="Duplicate binding"
            onClick={() =>
              onCommand({
                type: "duplicateBinding",
                mapIndex: row.mapIndex,
                bindingIndex: row.bindingIndex,
              })
            }
          />
          <RowButton
            label="Del"
            title="Delete binding"
            onClick={() =>
              onCommand({
                type: "deleteBinding",
                mapIndex: row.mapIndex,
                bindingIndex: row.bindingIndex,
              })
            }
          />
        </span>
      </div>
    );
  };

  return (
    <aside className="panel browser-panel">
      <div className="panel-heading tree-heading">
        <h2>Asset</h2>
        <div className="heading-actions">
          <button
            type="button"
            className="small-button"
            onClick={() => onCommand({ type: "addMap" })}
          >
            + Map
          </button>
          <button
            type="button"
            className="small-button"
            onClick={() => onCommand({ type: "addScheme" })}
          >
            + Scheme
          </button>
        </div>
      </div>
      <div
        className="tree"
        ref={treeRef}
        onScroll={(event) => setScrollTop(event.currentTarget.scrollTop)}
      >
        <div
          className="virtual-tree"
          style={{ height: `${rows.length * TREE_ROW_HEIGHT}px` }}
        >
          {visibleRows.map((row, index) => {
            const rowIndex = range.start + index;
            const key =
              row.kind === "map"
                ? `map-${row.mapIndex}`
                : row.kind === "action"
                  ? `action-${row.mapIndex}-${row.actionIndex}`
                  : row.kind === "binding"
                    ? `binding-${row.mapIndex}-${row.bindingIndex}`
                    : row.kind === "empty-binding"
                      ? `empty-binding-${row.mapIndex}-${row.actionIndex}`
                      : row.kind === "scheme"
                        ? `scheme-${row.schemeIndex}`
                        : row.kind;
            return (
              <div
                className="virtual-row"
                key={key}
                style={{
                  top: `${rowIndex * TREE_ROW_HEIGHT}px`,
                  height: `${TREE_ROW_HEIGHT}px`,
                }}
              >
                {renderRow(row)}
              </div>
            );
          })}
        </div>
      </div>
    </aside>
  );
}
