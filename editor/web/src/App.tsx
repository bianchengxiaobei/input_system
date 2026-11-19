import {
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState,
  type CSSProperties,
} from "react";

import {
  EditorClient,
  RpcError,
  type BakeResult,
  type Diagnostic,
  type OpenResult,
  type SaveResult,
} from "./api";
import {
  type ActionJson,
  type ActionMapJson,
  type AssetJson,
  type BindingJson,
  type ControlSchemeJson,
  type EditCommand,
  type Selection,
  bindingActionName,
  normalizeSelection,
  parseAsset,
  serializeAsset,
  selectionForPointer,
} from "./asset";
import {
  addAction,
  addActionMap,
  addBinding,
  addCompositeBinding,
  addControlScheme,
  deleteAction,
  deleteActionMap,
  deleteBinding,
  deleteControlScheme,
  duplicateAction,
  duplicateActionMap,
  duplicateBinding,
  duplicateControlScheme,
  moveAction,
  moveActionMap,
  moveBinding,
  moveControlScheme,
  pasteAction,
  pasteActionMap,
  pasteBinding,
  pasteControlScheme,
} from "./commands";
import {
  type EditorCatalog,
  defaultCompositeParts,
  emptyCatalog,
} from "./catalog";
import DiagnosticsPanel from "./DiagnosticsPanel";
import HidPage from "./HidPage";
import PreviewPage from "./PreviewPage";
import {
  type CapturedControl,
  type PreviewMouseInput,
  type PreviewSnapshot,
  emptyPreviewSnapshot,
} from "./preview";
import {
  addRecentPath,
  clampDiagnosticsWidth,
  clampTreeWidth,
  loadPreferences,
  savePreferences,
  suggestedBinaryPath,
  suggestedJsonPath,
  type EditorView,
} from "./preferences";
import Inspector, { type CaptureRequest } from "./Inspector";
import RawJsonPanel from "./RawJsonPanel";
import TreePanel, { type TreeCommand } from "./TreePanel";
import { localDiagnostics, mergeDiagnostics } from "./validation";

type View = EditorView;

type Clipboard =
  | { kind: "map"; map: ActionMapJson }
  | { kind: "action"; action: ActionJson; bindings: BindingJson[] }
  | { kind: "binding"; binding: BindingJson }
  | { kind: "scheme"; scheme: ControlSchemeJson };

const HISTORY_LIMIT = 200;

function jsonError(text: string, error: unknown): string {
  const message = error instanceof Error ? error.message : String(error);
  const match = /position (\d+)/i.exec(message);
  if (!match) {
    return `JSON error: ${message}`;
  }
  const position = Number(match[1]);
  const prefix = text.slice(0, position);
  const line = prefix.split("\n").length;
  const column = position - prefix.lastIndexOf("\n");
  return `JSON error at ${line}:${column}: ${message}`;
}

function diagnosticsFromError(error: unknown): Diagnostic[] {
  if (!(error instanceof RpcError) || typeof error.data !== "object" || error.data === null) {
    return [];
  }
  const diagnostics = (error.data as { diagnostics?: unknown }).diagnostics;
  return Array.isArray(diagnostics) ? (diagnostics as Diagnostic[]) : [];
}

function statusLabel(status: "connecting" | "online" | "offline"): string {
  return status === "connecting" ? "connecting" : status;
}

export default function App() {
  const token = useMemo(
    () => new URLSearchParams(window.location.search).get("token") ?? "",
    [],
  );
  const clientRef = useRef<EditorClient | null>(null);
  const openAssetRef = useRef<(targetPath?: string) => Promise<void>>(
    async () => {},
  );
  const saveAssetRef = useRef<() => Promise<void>>(async () => {});
  const validateSequence = useRef(0);
  const [initialPreferences] = useState(loadPreferences);
  const [status, setStatus] = useState<"connecting" | "online" | "offline">("connecting");
  const [serverVersion, setServerVersion] = useState("");
  const [path, setPath] = useState("");
  const [loadedPath, setLoadedPath] = useState("");
  const [revision, setRevision] = useState("");
  const [savedJson, setSavedJson] = useState("");
  const [asset, setAsset] = useState<AssetJson | null>(null);
  const [selection, setSelection] = useState<Selection>({ kind: "none" });
  const [undoStack, setUndoStack] = useState<EditCommand[]>([]);
  const [redoStack, setRedoStack] = useState<EditCommand[]>([]);
  const [clipboard, setClipboard] = useState<Clipboard | null>(null);
  const [catalog, setCatalog] = useState<EditorCatalog>(emptyCatalog);
  const [view, setView] = useState<View>(
    initialPreferences.view === "hid" ? "hid" : "inspector",
  );
  const [readOnly, setReadOnly] = useState(false);
  const [schemaVersion, setSchemaVersion] = useState(0);
  const [recentPaths, setRecentPaths] = useState(initialPreferences.recentPaths);
  const [treeWidth, setTreeWidth] = useState(initialPreferences.treeWidth);
  const [diagnosticsWidth, setDiagnosticsWidth] = useState(
    initialPreferences.diagnosticsWidth,
  );
  const [previewIntervalMs, setPreviewIntervalMs] = useState(
    initialPreferences.previewIntervalMs,
  );
  const [autoSave, setAutoSave] = useState(initialPreferences.autoSave);
  const [rawText, setRawText] = useState("");
  const [rawBase, setRawBase] = useState("");
  const [rawError, setRawError] = useState("");
  const [serverDiagnostics, setServerDiagnostics] = useState<Diagnostic[]>([]);
  const [busy, setBusy] = useState("");
  const [message, setMessage] = useState("");
  const [previewRunning, setPreviewRunning] = useState(false);
  const [previewGeneration, setPreviewGeneration] = useState(0);
  const [previewSnapshot, setPreviewSnapshot] =
    useState<PreviewSnapshot>(emptyPreviewSnapshot);
  const [previewError, setPreviewError] = useState("");
  const [previewDrive, setPreviewDrive] = useState(false);
  const [capture, setCapture] = useState<{
    expectedControlType: string;
    deviceFilter: string;
    label: string;
  } | null>(null);
  const previewGenerationRef = useRef(0);
  const previewJsonRef = useRef("");
  const previewRunningRef = useRef(false);
  const previewInputActiveRef = useRef(false);
  const captureRef = useRef<typeof capture>(null);
  const captureCallbackRef = useRef<((captured: CapturedControl) => void) | null>(null);
  const resizeRef = useRef<{
    kind: "tree" | "diagnostics";
    startX: number;
    startWidth: number;
  } | null>(null);
  const rpc = useCallback((method: string, params?: Record<string, unknown>) => {
    const client = clientRef.current;
    if (!client) {
      return Promise.reject(new Error("editor server is not connected"));
    }
    return client.request<unknown>(method, params);
  }, []);

  const rememberPath = useCallback((value: string) => {
    if (value.trim()) {
      setRecentPaths((previous) => addRecentPath(value, previous));
    }
  }, []);

  useEffect(() => {
    savePreferences({
      recentPaths,
      view,
      treeWidth,
      diagnosticsWidth,
      previewIntervalMs,
      autoSave,
    });
  }, [recentPaths, view, treeWidth, diagnosticsWidth, previewIntervalMs, autoSave]);

  useEffect(() => {
    const move = (event: PointerEvent) => {
      const resize = resizeRef.current;
      if (!resize) {
        return;
      }
      const delta = event.clientX - resize.startX;
      if (resize.kind === "tree") {
        setTreeWidth(clampTreeWidth(resize.startWidth + delta));
      } else {
        setDiagnosticsWidth(clampDiagnosticsWidth(resize.startWidth - delta));
      }
    };
    const up = () => {
      if (!resizeRef.current) {
        return;
      }
      resizeRef.current = null;
      document.body.classList.remove("panel-resizing");
    };
    window.addEventListener("pointermove", move);
    window.addEventListener("pointerup", up);
    window.addEventListener("pointercancel", up);
    return () => {
      window.removeEventListener("pointermove", move);
      window.removeEventListener("pointerup", up);
      window.removeEventListener("pointercancel", up);
      document.body.classList.remove("panel-resizing");
    };
  }, []);

  const currentJson = useMemo(() => (asset ? serializeAsset(asset) : ""), [asset]);
  const rawDirty = rawText !== rawBase;
  const dirty = Boolean(asset && currentJson !== savedJson);
  useEffect(() => {
    const onBeforeUnload = (event: BeforeUnloadEvent) => {
      if (!dirty && !rawDirty) {
        return;
      }
      event.preventDefault();
      event.returnValue = "";
    };
    window.addEventListener("beforeunload", onBeforeUnload);
    return () => window.removeEventListener("beforeunload", onBeforeUnload);
  }, [dirty, rawDirty]);
  const local = useMemo(() => localDiagnostics(asset, catalog), [asset, catalog]);
  const diagnostics = useMemo(
    () => mergeDiagnostics(local, serverDiagnostics),
    [local, serverDiagnostics],
  );
  const hasErrors = diagnostics.some((diagnostic) => diagnostic.severity === "error");

  useEffect(() => {
    const client = new EditorClient(token);
    clientRef.current = client;
    let active = true;
    client.onDisconnect = () => {
      if (active) {
        setStatus("offline");
        previewRunningRef.current = false;
        setPreviewRunning(false);
        setPreviewSnapshot(emptyPreviewSnapshot);
        captureRef.current = null;
        captureCallbackRef.current = null;
        setCapture(null);
        setMessage("editor server disconnected");
      }
    };

    client
      .connect()
      .then((hello) => {
        if (!active) {
          return;
        }
        setStatus("online");
        setServerVersion(hello.serverVersion);
        setMessage(`connected to protocol ${hello.protocolVersion}`);
        client
          .request<EditorCatalog>("catalog.get")
          .then((value) => {
            if (active) {
              setCatalog(value);
            }
          })
          .catch((error: unknown) => {
            if (active) {
              setMessage(
                `catalog unavailable: ${error instanceof Error ? error.message : String(error)}`,
              );
            }
          });
      })
      .catch((error: unknown) => {
        if (!active) {
          return;
        }
        setStatus("offline");
        setMessage(error instanceof Error ? error.message : String(error));
      });

    return () => {
      active = false;
      client.close();
      clientRef.current = null;
    };
  }, [token]);

  useEffect(() => {
    previewRunningRef.current = previewRunning;
    previewInputActiveRef.current =
      previewRunning && (previewDrive || capture !== null);
  }, [previewRunning, previewDrive, capture]);

  useEffect(() => {
    if (!previewRunning || status !== "online") {
      return;
    }
    const client = clientRef.current;
    if (!client) {
      return;
    }
    let active = true;
    let timer = 0;
    const interval =
      view === "preview" || capture !== null ? previewIntervalMs : 250;
    const poll = async () => {
      try {
        const next = await client.request<PreviewSnapshot>("preview.snapshot");
        if (!active) {
          return;
        }
        setPreviewSnapshot(next);
        setPreviewGeneration(next.generation);
        previewGenerationRef.current = next.generation;
        if (next.captured) {
          const callback = captureCallbackRef.current;
          captureCallbackRef.current = null;
          captureRef.current = null;
          setCapture(null);
          if (callback) {
            callback(next.captured);
            setMessage(`captured ${next.captured.path}`);
          }
          try {
            await client.request("capture.stop");
          } catch {
            // Capture already completed.
          }
        }
      } catch (error) {
        if (active) {
          setPreviewError(error instanceof Error ? error.message : String(error));
        }
      }
      if (active) {
        timer = window.setTimeout(() => void poll(), interval);
      }
    };
    void poll();
    return () => {
      active = false;
      window.clearTimeout(timer);
    };
  }, [previewRunning, status, view, capture, previewIntervalMs]);

  useEffect(() => {
    if (!previewRunning || status !== "online" || !asset) {
      return;
    }
    if (currentJson === previewJsonRef.current) {
      return;
    }
    const client = clientRef.current;
    if (!client) {
      return;
    }
    const timer = window.setTimeout(() => {
      client
        .request<{ generation: number }>("preview.update", {
          json: currentJson,
          generation: previewGenerationRef.current,
        })
        .then((result) => {
          previewGenerationRef.current = result.generation;
          previewJsonRef.current = currentJson;
          setPreviewGeneration(result.generation);
          setPreviewError("");
        })
        .catch((error: unknown) => {
          setPreviewError(error instanceof Error ? error.message : String(error));
        });
    }, 150);
    return () => window.clearTimeout(timer);
  }, [currentJson, previewRunning, status, asset]);

  useEffect(() => {
    if (!previewRunning || status !== "online") {
      return;
    }
    if (!previewDrive && capture === null) {
      return;
    }
    const client = clientRef.current;
    if (!client) {
      return;
    }
    const sendKey = (pressed: boolean, event: KeyboardEvent) => {
      client
        .request("preview.input.key", {
          code: event.code,
          pressed,
          repeat: event.repeat,
        })
        .catch((error: unknown) => {
          setPreviewError(error instanceof Error ? error.message : String(error));
        });
    };
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.code === "Escape" && captureRef.current) {
        event.preventDefault();
        event.stopPropagation();
        void cancelCapture();
        return;
      }
      event.preventDefault();
      event.stopPropagation();
      sendKey(true, event);
    };
    const onKeyUp = (event: KeyboardEvent) => {
      event.preventDefault();
      event.stopPropagation();
      sendKey(false, event);
    };
    window.addEventListener("keydown", onKeyDown, true);
    window.addEventListener("keyup", onKeyUp, true);
    return () => {
      window.removeEventListener("keydown", onKeyDown, true);
      window.removeEventListener("keyup", onKeyUp, true);
    };
  }, [previewRunning, previewDrive, capture, status]);

  function syncRaw(next: AssetJson | null): void {
    const text = next ? serializeAsset(next) : "";
    setRawText(text);
    setRawBase(text);
    setRawError("");
  }

  async function validateJson(text: string): Promise<void> {
    const client = clientRef.current;
    if (!client) {
      return;
    }
    const sequence = ++validateSequence.current;
    try {
      const result = await client.request<{ diagnostics: Diagnostic[] }>("asset.validate", {
        json: text,
      });
      if (sequence === validateSequence.current) {
        setServerDiagnostics(result.diagnostics);
      }
    } catch (error) {
      if (sequence === validateSequence.current) {
        const fromServer = diagnosticsFromError(error);
        setServerDiagnostics(
          fromServer.length > 0
            ? fromServer
            : [
                {
                  severity: "error",
                  code: "asset.validate",
                  message: error instanceof Error ? error.message : String(error),
                },
              ],
        );
      }
    }
  }

  function resetHistory(): void {
    setUndoStack([]);
    setRedoStack([]);
  }

  function applyLoaded(
    text: string,
    nextRevision: string,
    saved: string | null,
    nextSelection: Selection = { kind: "none" },
  ): boolean {
    try {
      const parsed = parseAsset(text);
      setAsset(parsed);
      setSelection(normalizeSelection(parsed, nextSelection));
      setRevision(nextRevision);
      setSavedJson(saved ?? serializeAsset(parsed));
      resetHistory();
      syncRaw(parsed);
      setView("inspector");
      void validateJson(serializeAsset(parsed));
      return true;
    } catch (error) {
      setAsset(null);
      setSelection({ kind: "none" });
      setRevision(nextRevision);
      setSavedJson(saved ?? text);
      resetHistory();
      setRawText(text);
      setRawBase(text);
      setRawError(jsonError(text, error));
      setView("json");
      void validateJson(text);
      return false;
    }
  }

  function commit(
    label: string,
    next: AssetJson,
    nextSelection: Selection = selection,
    allowRawDirty = false,
  ): void {
    if (!asset) {
      return;
    }
    if (rawDirty && !allowRawDirty) {
      setMessage("apply or revert JSON changes before editing");
      return;
    }
    const normalized = normalizeSelection(next, nextSelection);
    if (serializeAsset(asset) === serializeAsset(next)) {
      setSelection(normalized);
      return;
    }
    const before = { asset, selection };
    const after = { asset: next, selection: normalized };
    setUndoStack((previous) => [
      ...previous.slice(-(HISTORY_LIMIT - 1)),
      { label, before, after },
    ]);
    setRedoStack([]);
    setAsset(next);
    setSelection(normalized);
    syncRaw(next);
    setView("inspector");
    setMessage(label);
    void validateJson(serializeAsset(next));
  }

  function commitResult(label: string, result: { asset: AssetJson; selection: Selection }): void {
    commit(label, result.asset, result.selection);
  }

  function undo(): void {
    if (rawDirty || !asset || undoStack.length === 0) {
      if (rawDirty) {
        setMessage("apply or revert JSON changes before undo");
      }
      return;
    }
    const command = undoStack[undoStack.length - 1];
    setUndoStack(undoStack.slice(0, -1));
    setRedoStack((previous) => [
      ...previous.slice(-(HISTORY_LIMIT - 1)),
      command,
    ]);
    setAsset(command.before.asset);
    setSelection(normalizeSelection(command.before.asset, command.before.selection));
    syncRaw(command.before.asset);
    setMessage(`undo ${command.label}`);
    void validateJson(serializeAsset(command.before.asset));
  }

  function redo(): void {
    if (rawDirty || !asset || redoStack.length === 0) {
      if (rawDirty) {
        setMessage("apply or revert JSON changes before redo");
      }
      return;
    }
    const command = redoStack[redoStack.length - 1];
    setRedoStack(redoStack.slice(0, -1));
    setUndoStack((previous) => [
      ...previous.slice(-(HISTORY_LIMIT - 1)),
      command,
    ]);
    setAsset(command.after.asset);
    setSelection(normalizeSelection(command.after.asset, command.after.selection));
    syncRaw(command.after.asset);
    setMessage(`redo ${command.label}`);
    void validateJson(serializeAsset(command.after.asset));
  }

  function beginResize(
    kind: "tree" | "diagnostics",
    event: React.PointerEvent<HTMLDivElement>,
  ): void {
    event.preventDefault();
    resizeRef.current = {
      kind,
      startX: event.clientX,
      startWidth: kind === "tree" ? treeWidth : diagnosticsWidth,
    };
    document.body.classList.add("panel-resizing");
  }

  function confirmDiscard(): boolean {
    return (!dirty && !rawDirty) || window.confirm("Discard unsaved changes?");
  }

  async function openAsset(targetPath = path): Promise<void> {
    const client = clientRef.current;
    const destination = targetPath.trim();
    if (!client || !destination) {
      setMessage("enter an asset path first");
      return;
    }
    if (!confirmDiscard()) {
      return;
    }
    setPath(destination);
    setBusy("open");
    setMessage("");
    try {
      const opened = await client.request<OpenResult>("asset.open", {
        path: destination,
      });
      setPath(
        opened.readOnly ? suggestedJsonPath(opened.path) : opened.path,
      );
      setLoadedPath(opened.path);
      setReadOnly(opened.readOnly);
      setSchemaVersion(opened.schemaVersion ?? 0);
      rememberPath(opened.path);
      const loaded = applyLoaded(opened.json, opened.revision, null, { kind: "none" });
      setMessage(
        loaded
          ? opened.readOnly
            ? `imported ${opened.path} read-only; save as JSON or bake a new .insysb`
            : `opened ${opened.path}`
          : `opened ${opened.path} but JSON is invalid`,
      );
    } catch (error) {
      setMessage(error instanceof Error ? error.message : String(error));
    } finally {
      setBusy("");
    }
  }

  openAssetRef.current = openAsset;

  useEffect(() => {
    const host = (
      window as Window & {
        chrome?: {
          webview?: {
            addEventListener(
              type: "message",
              handler: (event: MessageEvent<unknown>) => void,
            ): void;
            removeEventListener(
              type: "message",
              handler: (event: MessageEvent<unknown>) => void,
            ): void;
          };
        };
      }
    ).chrome?.webview;
    if (!host) {
      return;
    }
    const handler = (event: MessageEvent<unknown>) => {
      if (typeof event.data === "string" && event.data.trim()) {
        void openAssetRef.current(event.data);
      }
    };
    host.addEventListener("message", handler);
    return () => host.removeEventListener("message", handler);
  }, []);

  async function newAsset(): Promise<void> {
    const client = clientRef.current;
    if (!client || !confirmDiscard()) {
      return;
    }
    setBusy("new");
    setMessage("");
    try {
      const created = await client.request<{ json: string }>("asset.new", {});
      applyLoaded(created.json, "", "", { kind: "none" });
      setPath("");
      setLoadedPath("");
      setReadOnly(false);
      setSchemaVersion(1);
      setMessage("created a new asset; add maps and actions, then save");
    } catch (error) {
      setMessage(error instanceof Error ? error.message : String(error));
    } finally {
      setBusy("");
    }
  }

  async function reloadAsset(): Promise<void> {
    const client = clientRef.current;
    if (!client || !loadedPath || !confirmDiscard()) {
      return;
    }
    setBusy("reload");
    setMessage("");
    try {
      const opened = await client.request<OpenResult>("asset.open", {
        path: loadedPath,
      });
      if (opened.format === "json") {
        setPath(opened.path);
      } else if (!path.trim() || path.trim() === loadedPath) {
        setPath(suggestedJsonPath(opened.path));
      }
      setLoadedPath(opened.path);
      setReadOnly(opened.readOnly);
      setSchemaVersion(opened.schemaVersion ?? 0);
      rememberPath(opened.path);
      const loaded = applyLoaded(opened.json, opened.revision, null);
      setMessage(
        loaded ? `reloaded ${opened.path}` : `reloaded ${opened.path} but JSON is invalid`,
      );
    } catch (error) {
      setMessage(error instanceof Error ? error.message : String(error));
    } finally {
      setBusy("");
    }
  }

  async function saveAsset(): Promise<void> {
    const client = clientRef.current;
    if (!client || !asset || busy) {
      return;
    }
    if (rawDirty) {
      setMessage("apply or revert JSON changes before saving");
      return;
    }
    const destination = path.trim();
    if (!destination) {
      setMessage("enter a destination path before saving");
      return;
    }
    if (readOnly && destination === loadedPath) {
      setMessage("imported .insysb assets are read-only; choose a .json path");
      return;
    }
    if (!destination.toLowerCase().endsWith(".json")) {
      setMessage("save destination must end in .json");
      return;
    }
    if (hasErrors) {
      setMessage("fix validation errors before saving");
      return;
    }
    setBusy("save");
    setMessage("");
    try {
      const requestSave = (expectedRevision: string) =>
        client.request<SaveResult>("asset.save", {
          path: destination,
          json: currentJson,
          expectedRevision,
          createBackup: true,
        });
      let saved: SaveResult;
      try {
        saved = await requestSave(destination === loadedPath ? revision : "");
      } catch (error) {
        if (
          !(error instanceof RpcError) ||
          error.code !== -32011 ||
          destination === loadedPath ||
          !window.confirm(`Overwrite existing file ${destination}?`)
        ) {
          throw error;
        }
        const existing = await client.request<OpenResult>("asset.open", {
          path: destination,
        });
        saved = await requestSave(existing.revision);
      }
      const savedPath = saved.path || destination;
      setPath(savedPath);
      setRevision(saved.revision);
      setLoadedPath(savedPath);
      setReadOnly(false);
      setSchemaVersion(1);
      setSavedJson(currentJson);
      rememberPath(savedPath);
      setMessage(saved.changed ? `saved ${savedPath}` : "asset was already up to date");
    } catch (error) {
      const fromServer = diagnosticsFromError(error);
      if (fromServer.length > 0) {
        setServerDiagnostics(fromServer);
      }
      setMessage(
        error instanceof RpcError && error.code === -32011
          ? "save conflict: the file changed on disk; use Reload"
          : error instanceof Error
            ? error.message
            : String(error),
      );
    } finally {
      setBusy("");
    }
  }

  saveAssetRef.current = saveAsset;

  useEffect(() => {
    const hostWindow = window as Window & {
      __insysEditorHost?: {
        openAsset(path: string): void;
        state(): { dirty: boolean; rawDirty: boolean; autoSave: boolean };
        save(): void;
      };
    };
    const api = {
      openAsset(path: string): void {
        void openAssetRef.current(path);
      },
      state() {
        return { dirty, rawDirty, autoSave };
      },
      save(): void {
        void saveAssetRef.current();
      },
    };
    hostWindow.__insysEditorHost = api;
    return () => {
      if (hostWindow.__insysEditorHost === api) {
        delete hostWindow.__insysEditorHost;
      }
    };
  }, [dirty, rawDirty, autoSave]);

  useEffect(() => {
    const onBlur = () => {
      if (!autoSave || !dirty || rawDirty || busy || hasErrors || !asset) {
        return;
      }
      const destination = path.trim();
      if (!destination || (readOnly && destination === loadedPath)) {
        return;
      }
      void saveAssetRef.current();
    };
    window.addEventListener("blur", onBlur);
    return () => window.removeEventListener("blur", onBlur);
  }, [
    autoSave,
    dirty,
    rawDirty,
    busy,
    hasErrors,
    asset,
    path,
    readOnly,
    loadedPath,
  ]);

  function changeAutoSave(next: boolean): void {
    setAutoSave(next);
    if (next && dirty && !rawDirty && !busy && !hasErrors) {
      void saveAsset();
    }
  }

  async function bakeAsset(): Promise<void> {
    const client = clientRef.current;
    if (!client || !asset || busy) {
      return;
    }
    if (rawDirty) {
      setMessage("apply or revert JSON changes before baking");
      return;
    }
    if (hasErrors) {
      setMessage("fix validation errors before baking");
      return;
    }
    const source = loadedPath || path.trim();
    const suggested = suggestedBinaryPath(source) || "asset.insysb";
    const destination = window.prompt("Bake to .insysb path", suggested)?.trim();
    if (!destination) {
      return;
    }
    setBusy("bake");
    setMessage("");
    try {
      const requestBake = (expectedRevision: string) =>
        client.request<BakeResult>("asset.bake", {
          path: destination,
          json: currentJson,
          expectedRevision,
          createBackup: true,
        });
      let baked: BakeResult;
      try {
        baked = await requestBake(destination === loadedPath ? revision : "");
      } catch (error) {
        if (
          !(error instanceof RpcError) ||
          error.code !== -32011 ||
          destination === loadedPath ||
          !window.confirm(`Overwrite existing binary ${destination}?`)
        ) {
          throw error;
        }
        const existing = await client.request<OpenResult>("asset.open", {
          path: destination,
        });
        baked = await requestBake(existing.revision);
      }
      rememberPath(baked.path);
      setMessage(
        baked.changed
          ? `baked ${baked.path} (${baked.bytes} bytes)`
          : "baked file was already up to date",
      );
    } catch (error) {
      setMessage(
        error instanceof RpcError && error.code === -32011
          ? "bake conflict: the .insysb file exists or changed; choose a new path"
          : error instanceof Error
            ? error.message
            : String(error),
      );
    } finally {
      setBusy("");
    }
  }

  async function validateAsset(): Promise<void> {
    if (!asset) {
      setMessage("open a valid asset first");
      return;
    }
    setBusy("validate");
    setMessage(rawDirty ? "validating the last applied document" : "");
    try {
      await validateJson(currentJson);
      setMessage("validation updated");
    } finally {
      setBusy("");
    }
  }

  function clearCapture(): void {
    captureRef.current = null;
    captureCallbackRef.current = null;
    setCapture(null);
  }

  async function startPreview(): Promise<boolean> {
    const client = clientRef.current;
    if (!client || !asset) {
      setPreviewError("open a valid asset first");
      return false;
    }
    setBusy("preview");
    setPreviewError("");
    try {
      const started = await client.request<{ generation: number }>("preview.start", {
        json: currentJson,
      });
      previewRunningRef.current = true;
      previewGenerationRef.current = started.generation;
      previewJsonRef.current = currentJson;
      setPreviewRunning(true);
      setPreviewGeneration(started.generation);
      setPreviewDrive(false);
      setMessage(`preview started at generation ${started.generation}`);
      return true;
    } catch (error) {
      const text = error instanceof Error ? error.message : String(error);
      setPreviewError(text);
      setMessage(`preview failed: ${text}`);
      return false;
    } finally {
      setBusy("");
    }
  }

  async function stopPreview(): Promise<void> {
    const client = clientRef.current;
    clearCapture();
    setPreviewDrive(false);
    previewRunningRef.current = false;
    previewInputActiveRef.current = false;
    setPreviewRunning(false);
    setPreviewGeneration(0);
    previewGenerationRef.current = 0;
    previewJsonRef.current = "";
    setPreviewSnapshot(emptyPreviewSnapshot);
    setPreviewError("");
    if (client) {
      try {
        await client.request("preview.stop");
      } catch {
        // The socket may already be gone; local state is stopped either way.
      }
    }
    setMessage("preview stopped");
  }

  async function cancelCapture(): Promise<void> {
    const client = clientRef.current;
    clearCapture();
    if (client) {
      try {
        await client.request("capture.stop");
      } catch {
        // Ignore capture cleanup failures.
      }
    }
    setMessage("capture canceled");
  }

  async function beginCapture(
    request: CaptureRequest,
    apply: (path: string) => void,
  ): Promise<void> {
    const client = clientRef.current;
    if (!client || !asset) {
      setMessage("open a valid asset first");
      return;
    }
    if (!previewRunningRef.current) {
      const started = await startPreview();
      if (!started) {
        return;
      }
    }
    captureCallbackRef.current = (captured: CapturedControl) => apply(captured.path);
    captureRef.current = {
      expectedControlType: request.expectedControlType,
      deviceFilter: request.deviceFilter,
      label: request.label,
    };
    setCapture({ ...captureRef.current });
    setPreviewDrive(true);
    setPreviewError("");
    try {
      await client.request("capture.start", {
        expectedControlType: request.expectedControlType,
        deviceFilter: request.deviceFilter,
      });
      setMessage(
        `listening for ${request.expectedControlType || "any control"}; press Esc to cancel (use Preview mouse pad for mouse)`,
      );
    } catch (error) {
      clearCapture();
      setPreviewError(error instanceof Error ? error.message : String(error));
    }
  }

  async function sendPreviewMouse(input: PreviewMouseInput): Promise<void> {
    const client = clientRef.current;
    if (!client || !previewRunningRef.current) {
      return;
    }
    try {
      await client.request("preview.input.mouse", input as Record<string, unknown>);
    } catch (error) {
      setPreviewError(error instanceof Error ? error.message : String(error));
    }
  }

  function onTreeCommand(command: TreeCommand): void {
    if (!asset) {
      return;
    }
    switch (command.type) {
      case "addMap":
        commitResult("Add action map", addActionMap(asset));
        break;
      case "duplicateMap":
        commitResult("Duplicate action map", duplicateActionMap(asset, command.mapIndex));
        break;
      case "deleteMap":
        commitResult("Delete action map", deleteActionMap(asset, command.mapIndex));
        break;
      case "moveMap":
        commitResult("Move action map", moveActionMap(asset, command.from, command.to));
        break;
      case "addAction":
        commitResult("Add action", addAction(asset, command.mapIndex));
        break;
      case "duplicateAction":
        commitResult(
          "Duplicate action",
          duplicateAction(asset, command.mapIndex, command.actionIndex),
        );
        break;
      case "deleteAction":
        commitResult(
          "Delete action",
          deleteAction(asset, command.mapIndex, command.actionIndex),
        );
        break;
      case "moveAction":
        commitResult(
          "Move action",
          moveAction(asset, command.mapIndex, command.from, command.to),
        );
        break;
      case "addBinding":
        commitResult(
          "Add binding",
          addBinding(asset, command.mapIndex, command.actionName),
        );
        break;
      case "addComposite": {
        const composite = catalog.composites[0];
        if (!composite) {
          setMessage("catalog is still loading");
          return;
        }
        commitResult(
          "Add composite binding",
          addCompositeBinding(
            asset,
            command.mapIndex,
            command.actionName,
            composite.name,
            defaultCompositeParts(catalog, composite.name),
          ),
        );
        break;
      }
      case "duplicateBinding":
        commitResult(
          "Duplicate binding",
          duplicateBinding(asset, command.mapIndex, command.bindingIndex),
        );
        break;
      case "deleteBinding":
        commitResult(
          "Delete binding",
          deleteBinding(asset, command.mapIndex, command.bindingIndex),
        );
        break;
      case "moveBinding":
        commitResult(
          "Move binding",
          moveBinding(asset, command.mapIndex, command.from, command.to),
        );
        break;
      case "addScheme":
        commitResult("Add control scheme", addControlScheme(asset));
        break;
      case "duplicateScheme":
        commitResult(
          "Duplicate control scheme",
          duplicateControlScheme(asset, command.schemeIndex),
        );
        break;
      case "deleteScheme":
        commitResult(
          "Delete control scheme",
          deleteControlScheme(asset, command.schemeIndex),
        );
        break;
      case "moveScheme":
        commitResult(
          "Move control scheme",
          moveControlScheme(asset, command.from, command.to),
        );
        break;
    }
  }

  function selectedMapIndex(): number | null {
    if (selection.kind === "map" || selection.kind === "action" || selection.kind === "binding") {
      return selection.mapIndex;
    }
    return asset && asset.maps.length > 0 ? 0 : null;
  }

  function selectedActionName(mapIndex: number): string | null {
    if (!asset) {
      return null;
    }
    if (selection.kind === "action" && selection.mapIndex === mapIndex) {
      return asset.maps[mapIndex]?.actions[selection.actionIndex]?.name ?? null;
    }
    if (selection.kind === "binding" && selection.mapIndex === mapIndex) {
      const binding = asset.maps[mapIndex]?.bindings[selection.bindingIndex];
      return binding ? bindingActionName(binding) : null;
    }
    return asset.maps[mapIndex]?.actions[0]?.name ?? null;
  }

  function copySelection(): void {
    if (!asset) {
      return;
    }
    if (selection.kind === "map") {
      const map = asset.maps[selection.mapIndex];
      if (map) {
        setClipboard({ kind: "map", map: JSON.parse(JSON.stringify(map)) as ActionMapJson });
        setMessage(`copied action map ${map.name}`);
      }
    } else if (selection.kind === "action") {
      const map = asset.maps[selection.mapIndex];
      const action = map?.actions[selection.actionIndex];
      if (map && action) {
        const bindings = map.bindings
          .filter(
            (binding) =>
              binding.action.split("/").pop()?.toLowerCase() === action.name.toLowerCase(),
          )
          .map((binding) => JSON.parse(JSON.stringify(binding)) as BindingJson);
        setClipboard({
          kind: "action",
          action: JSON.parse(JSON.stringify(action)) as ActionJson,
          bindings,
        });
        setMessage(`copied action ${action.name}`);
      }
    } else if (selection.kind === "binding") {
      const binding = asset.maps[selection.mapIndex]?.bindings[selection.bindingIndex];
      if (binding) {
        setClipboard({
          kind: "binding",
          binding: JSON.parse(JSON.stringify(binding)) as BindingJson,
        });
        setMessage("copied binding");
      }
    } else if (selection.kind === "scheme") {
      const scheme = asset.schemes?.[selection.schemeIndex];
      if (scheme) {
        setClipboard({
          kind: "scheme",
          scheme: JSON.parse(JSON.stringify(scheme)) as ControlSchemeJson,
        });
        setMessage(`copied control scheme ${scheme.name}`);
      }
    } else {
      setMessage("select an item first");
    }
  }

  function pasteSelection(): void {
    if (!asset || !clipboard) {
      setMessage("clipboard is empty");
      return;
    }
    if (clipboard.kind === "map") {
      commitResult("Paste action map", pasteActionMap(asset, clipboard.map));
      return;
    }
    if (clipboard.kind === "scheme") {
      commitResult("Paste control scheme", pasteControlScheme(asset, clipboard.scheme));
      return;
    }
    const mapIndex = selectedMapIndex();
    if (mapIndex === null || !asset.maps[mapIndex]) {
      setMessage("select an action map first");
      return;
    }
    if (clipboard.kind === "action") {
      commitResult(
        "Paste action",
        pasteAction(asset, mapIndex, clipboard.action, clipboard.bindings),
      );
      return;
    }
    const actionName = selectedActionName(mapIndex);
    if (!actionName) {
      setMessage("add an action before pasting a binding");
      return;
    }
    commitResult(
      "Paste binding",
      pasteBinding(asset, mapIndex, actionName, clipboard.binding),
    );
  }

  function duplicateSelection(): void {
    if (!asset) {
      return;
    }
    if (selection.kind === "map") {
      commitResult("Duplicate action map", duplicateActionMap(asset, selection.mapIndex));
    } else if (selection.kind === "action") {
      commitResult(
        "Duplicate action",
        duplicateAction(asset, selection.mapIndex, selection.actionIndex),
      );
    } else if (selection.kind === "binding") {
      commitResult(
        "Duplicate binding",
        duplicateBinding(asset, selection.mapIndex, selection.bindingIndex),
      );
    } else if (selection.kind === "scheme") {
      commitResult(
        "Duplicate control scheme",
        duplicateControlScheme(asset, selection.schemeIndex),
      );
    } else {
      setMessage("select an item first");
    }
  }

  function deleteSelection(): void {
    if (!asset) {
      return;
    }
    if (selection.kind === "map") {
      commitResult("Delete action map", deleteActionMap(asset, selection.mapIndex));
    } else if (selection.kind === "action") {
      commitResult(
        "Delete action",
        deleteAction(asset, selection.mapIndex, selection.actionIndex),
      );
    } else if (selection.kind === "binding") {
      commitResult(
        "Delete binding",
        deleteBinding(asset, selection.mapIndex, selection.bindingIndex),
      );
    } else if (selection.kind === "scheme") {
      commitResult(
        "Delete control scheme",
        deleteControlScheme(asset, selection.schemeIndex),
      );
    }
  }

  function applyRaw(): void {
    try {
      const parsed = parseAsset(rawText);
      if (!asset) {
        setAsset(parsed);
        setSelection(normalizeSelection(parsed, { kind: "none" }));
        syncRaw(parsed);
        void validateJson(serializeAsset(parsed));
        setMessage("applied JSON");
        return;
      }
      commit("Apply JSON", parsed, { kind: "none" }, true);
    } catch (error) {
      setRawError(jsonError(rawText, error));
    }
  }

  function formatRaw(): void {
    try {
      const parsed = parseAsset(rawText);
      setRawText(serializeAsset(parsed));
      setRawError("");
      setMessage("formatted JSON");
    } catch (error) {
      setRawError(jsonError(rawText, error));
    }
  }

  function revertRaw(): void {
    setRawText(rawBase);
    setRawError("");
  }

  function selectDiagnostic(diagnostic: Diagnostic): void {
    if (diagnostic.jsonPointer && asset) {
      const next = selectionForPointer(asset, diagnostic.jsonPointer);
      if (next) {
        setSelection(next);
        setView("inspector");
      }
    }
    if (diagnostic.line) {
      setView("json");
    }
  }

  useEffect(() => {
    const handler = (event: KeyboardEvent) => {
      if (previewInputActiveRef.current) {
        return;
      }
      const target = event.target as HTMLElement | null;
      const editing =
        target !== null &&
        (target.tagName === "INPUT" ||
          target.tagName === "TEXTAREA" ||
          target.tagName === "SELECT" ||
          target.isContentEditable);
      const modifier = event.ctrlKey || event.metaKey;
      if (modifier && event.key.toLowerCase() === "s") {
        event.preventDefault();
        void saveAsset();
        return;
      }
      if (editing) {
        return;
      }
      if (modifier && event.key.toLowerCase() === "z") {
        event.preventDefault();
        if (event.shiftKey) {
          redo();
        } else {
          undo();
        }
      } else if (modifier && event.key.toLowerCase() === "y") {
        event.preventDefault();
        redo();
      } else if (modifier && event.key.toLowerCase() === "c") {
        event.preventDefault();
        copySelection();
      } else if (modifier && event.key.toLowerCase() === "v") {
        event.preventDefault();
        pasteSelection();
      } else if (modifier && event.key.toLowerCase() === "d") {
        event.preventDefault();
        duplicateSelection();
      } else if (event.key === "Delete" || event.key === "Backspace") {
        event.preventDefault();
        deleteSelection();
      }
    };
    window.addEventListener("keydown", handler);
    return () => window.removeEventListener("keydown", handler);
  }, [asset, selection, clipboard, rawDirty, undoStack, redoStack, currentJson, path, revision, busy, savedJson, hasErrors, status, view, loadedPath]);

  return (
    <div
      className="app"
      data-dirty={String(dirty)}
      data-raw-dirty={String(rawDirty)}
      data-auto-save={String(autoSave)}
    >
      <header className="toolbar">
        <div className="brand">
          <span className="brand-mark">insys</span>
          <span className="brand-subtitle">asset editor</span>
          <span className="phase">E4</span>
          {schemaVersion > 0 ? (
            <span className={`badge ${readOnly ? "badge-readonly" : ""}`}>
              {readOnly ? "read-only" : `schema v${schemaVersion}`}
            </span>
          ) : null}
        </div>
        <label className="path-field">
          <span>{readOnly ? "Save as" : "Asset"}</span>
          <input
            value={path}
            onChange={(event) => setPath(event.target.value)}
            placeholder="assets/player.json"
            spellCheck={false}
            list="recent-asset-paths"
          />
          <datalist id="recent-asset-paths">
            {recentPaths.map((recent) => (
              <option key={recent} value={recent} />
            ))}
          </datalist>
        </label>
        <div className="toolbar-actions">
          <button type="button" onClick={() => void newAsset()} disabled={status !== "online" || busy !== ""}>
            New
          </button>
          <button type="button" onClick={() => void openAsset()} disabled={status !== "online" || busy !== ""}>
            Open
          </button>
          <button
            type="button"
            onClick={() => void reloadAsset()}
            disabled={status !== "online" || busy !== "" || !loadedPath}
          >
            Reload
          </button>
          <button
            type="button"
            onClick={undo}
            disabled={!asset || rawDirty || undoStack.length === 0}
            title="Undo (Ctrl+Z)"
          >
            Undo
          </button>
          <button
            type="button"
            onClick={redo}
            disabled={!asset || rawDirty || redoStack.length === 0}
            title="Redo (Ctrl+Shift+Z)"
          >
            Redo
          </button>
          <button
            type="button"
            onClick={() => void validateAsset()}
            disabled={status !== "online" || busy !== "" || !asset}
          >
            Validate
          </button>
          <button
            type="button"
            onClick={() => void bakeAsset()}
            disabled={
              status !== "online" || busy !== "" || !asset || rawDirty || hasErrors
            }
            title="Export an .insysb binary"
          >
            Bake
          </button>
          <button
            type="button"
            className="primary"
            onClick={() => void saveAsset()}
            disabled={
              status !== "online" ||
              busy !== "" ||
              !asset ||
              rawDirty ||
              hasErrors ||
              !path ||
              (readOnly && path.trim() === loadedPath)
            }
          >
            Save
          </button>
          <label
            className="auto-save-toggle"
            title="Save automatically on focus loss and window close"
          >
            <input
              type="checkbox"
              checked={autoSave}
              onChange={(event) => changeAutoSave(event.target.checked)}
            />
            Auto save
          </label>
        </div>
        <div className={`status status-${status}`}>
          <span className="status-dot" />
          {statusLabel(status)}
          {serverVersion ? ` ${serverVersion}` : ""}
        </div>
      </header>

      <main
        className="workspace"
        style={
          {
            "--tree-width": `${treeWidth}px`,
            "--diagnostics-width": `${diagnosticsWidth}px`,
          } as CSSProperties
        }
      >
        <TreePanel
          asset={asset}
          selection={selection}
          onSelect={(next) => {
            setSelection(next);
            setView("inspector");
          }}
          onCommand={onTreeCommand}
        />
        <div
          className="splitter splitter-tree"
          role="separator"
          aria-orientation="vertical"
          aria-label="Resize asset tree"
          onPointerDown={(event) => beginResize("tree", event)}
        />

        <section className="panel editor-panel">
          <div className="panel-heading">
            <div className="view-tabs">
              <button
                type="button"
                className={view === "inspector" ? "tab active" : "tab"}
                onClick={() => setView("inspector")}
                disabled={!asset}
              >
                Inspector
              </button>
              <button
                type="button"
                className={view === "json" ? "tab active" : "tab"}
                onClick={() => {
                  if (!rawDirty && asset) {
                    syncRaw(asset);
                  }
                  setView("json");
                }}
              >
                Raw JSON
              </button>
              <button
                type="button"
                className={view === "preview" ? "tab active" : "tab"}
                disabled={!asset}
                onClick={() => {
                  setView("preview");
                  if (!previewRunning && status === "online") {
                    void startPreview();
                  }
                }}
              >
                Preview
              </button>
              <button
                type="button"
                className={view === "hid" ? "tab active" : "tab"}
                disabled={status !== "online"}
                onClick={() => setView("hid")}
              >
                HID Tools
              </button>
            </div>
            {capture ? (
              <span className="capture-indicator">
                listening for {capture.expectedControlType || "any control"}
                <button
                  type="button"
                  className="small-button"
                  onClick={() => void cancelCapture()}
                >
                  Cancel
                </button>
              </span>
            ) : null}
            <span className="meta">
              {view === "preview"
                ? previewRunning
                  ? `preview gen ${previewGeneration}`
                  : "preview stopped"
                : view === "hid"
                  ? "HID tools"
                : asset
                  ? `${undoStack.length} undo · ${redoStack.length} redo`
                  : "no asset"}
            </span>
          </div>
          {view === "preview" ? (
            <PreviewPage
              snapshot={previewSnapshot}
              running={previewRunning}
              generation={previewGeneration}
              busy={busy !== ""}
              error={previewError}
              drive={previewDrive}
              capture={capture}
              onStart={() => void startPreview()}
              onStop={() => void stopPreview()}
              onToggleDrive={setPreviewDrive}
              onCancelCapture={() => void cancelCapture()}
              onMouse={(input) => void sendPreviewMouse(input)}
              pollIntervalMs={previewIntervalMs}
              onPollIntervalChange={setPreviewIntervalMs}
            />
          ) : view === "hid" ? (
            <HidPage online={status === "online"} request={rpc} />
          ) : asset && view === "inspector" ? (
            <Inspector
              asset={asset}
              selection={selection}
              catalog={catalog}
              onChange={commit}
              onListen={(request, apply) => void beginCapture(request, apply)}
            />
          ) : (
            <RawJsonPanel
              value={rawText}
              error={rawError}
              dirty={rawDirty}
              onValueChange={(value) => {
                setRawText(value);
                setRawError("");
              }}
              onApply={applyRaw}
              onFormat={formatRaw}
              onRevert={revertRaw}
            />
          )}
        </section>
        <div
          className="splitter splitter-diagnostics"
          role="separator"
          aria-orientation="vertical"
          aria-label="Resize diagnostics"
          onPointerDown={(event) => beginResize("diagnostics", event)}
        />

        <DiagnosticsPanel diagnostics={diagnostics} onSelect={selectDiagnostic} />
      </main>

      <footer className="statusbar">
        <span>{busy ? `${busy}...` : message || "ready"}</span>
        <span className="statusbar-right">
          {rawDirty
            ? "JSON changes not applied"
            : dirty
              ? "unsaved changes"
              : readOnly
                ? "binary import · read-only"
                : "saved"}
          {path ? ` · ${path}` : ""}
          {loadedPath && loadedPath !== path ? ` · source ${loadedPath}` : ""}
          {schemaVersion > 0 ? ` · schema v${schemaVersion}` : ""}
        </span>
      </footer>
    </div>
  );
}
