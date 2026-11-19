export type EditorView = "inspector" | "json" | "preview" | "hid";

export interface EditorPreferences {
  recentPaths: string[];
  view: EditorView;
  treeWidth: number;
  diagnosticsWidth: number;
  previewIntervalMs: number;
  autoSave: boolean;
}

export interface PreferenceStorage {
  getItem(key: string): string | null;
  setItem(key: string, value: string): void;
}

export const DEFAULT_TREE_WIDTH = 300;
export const DEFAULT_DIAGNOSTICS_WIDTH = 340;
export const DEFAULT_PREVIEW_INTERVAL_MS = 33;
export const PREVIEW_INTERVAL_OPTIONS = [33, 100, 250] as const;
export const RECENT_PATH_LIMIT = 10;

const STORAGE_KEY = "insys.editor.preferences.v1";

const DEFAULT_PREFERENCES: EditorPreferences = {
  recentPaths: [],
  view: "inspector",
  treeWidth: DEFAULT_TREE_WIDTH,
  diagnosticsWidth: DEFAULT_DIAGNOSTICS_WIDTH,
  previewIntervalMs: DEFAULT_PREVIEW_INTERVAL_MS,
  autoSave: false,
};

export function clampTreeWidth(value: number): number {
  return Math.min(520, Math.max(200, Math.round(value)));
}

export function clampDiagnosticsWidth(value: number): number {
  return Math.min(560, Math.max(220, Math.round(value)));
}

function validView(value: unknown): value is EditorView {
  return value === "inspector" || value === "json" || value === "preview" || value === "hid";
}

function validInterval(value: unknown): value is number {
  return (
    typeof value === "number" &&
    PREVIEW_INTERVAL_OPTIONS.some((option) => option === value)
  );
}

function validRecentPaths(value: unknown): value is string[] {
  return Array.isArray(value) && value.every((item) => typeof item === "string");
}

function normalizePreferences(value: unknown): EditorPreferences {
  if (typeof value !== "object" || value === null) {
    return { ...DEFAULT_PREFERENCES };
  }
  const parsed = value as Partial<EditorPreferences>;
  return {
    recentPaths: validRecentPaths(parsed.recentPaths)
      ? parsed.recentPaths.filter((path) => path.trim() !== "").slice(0, RECENT_PATH_LIMIT)
      : [],
    view: validView(parsed.view) ? parsed.view : DEFAULT_PREFERENCES.view,
    treeWidth:
      typeof parsed.treeWidth === "number"
        ? clampTreeWidth(parsed.treeWidth)
        : DEFAULT_PREFERENCES.treeWidth,
    diagnosticsWidth:
      typeof parsed.diagnosticsWidth === "number"
        ? clampDiagnosticsWidth(parsed.diagnosticsWidth)
        : DEFAULT_PREFERENCES.diagnosticsWidth,
    previewIntervalMs: validInterval(parsed.previewIntervalMs)
      ? parsed.previewIntervalMs
      : DEFAULT_PREFERENCES.previewIntervalMs,
    autoSave:
      typeof parsed.autoSave === "boolean"
        ? parsed.autoSave
        : DEFAULT_PREFERENCES.autoSave,
  };
}

function defaultStorage(): PreferenceStorage | undefined {
  if (typeof window === "undefined") {
    return undefined;
  }
  try {
    return window.localStorage;
  } catch {
    return undefined;
  }
}

export function loadPreferences(storage?: PreferenceStorage): EditorPreferences {
  const target = storage ?? defaultStorage();
  if (!target) {
    return { ...DEFAULT_PREFERENCES };
  }
  try {
    const raw = target.getItem(STORAGE_KEY);
    return raw ? normalizePreferences(JSON.parse(raw)) : { ...DEFAULT_PREFERENCES };
  } catch {
    return { ...DEFAULT_PREFERENCES };
  }
}

export function savePreferences(
  preferences: EditorPreferences,
  storage?: PreferenceStorage,
): void {
  const target = storage ?? defaultStorage();
  if (!target) {
    return;
  }
  try {
    target.setItem(STORAGE_KEY, JSON.stringify(normalizePreferences(preferences)));
  } catch {
    // Storage can be unavailable in restricted WebView contexts.
  }
}

export function addRecentPath(path: string, recentPaths: string[]): string[] {
  const normalized = path.trim();
  if (!normalized) {
    return recentPaths;
  }
  return [
    normalized,
    ...recentPaths.filter((item) => item !== normalized),
  ].slice(0, RECENT_PATH_LIMIT);
}

export function suggestedPathWithExtension(
  path: string,
  extension: string,
): string {
  const trimmed = path.trim();
  if (!trimmed) {
    return "";
  }
  const dot = trimmed.lastIndexOf(".");
  const slash = Math.max(trimmed.lastIndexOf("/"), trimmed.lastIndexOf("\\"));
  const stem = dot > slash ? trimmed.slice(0, dot) : trimmed;
  return `${stem}${extension.startsWith(".") ? extension : `.${extension}`}`;
}

export function suggestedJsonPath(path: string): string {
  return suggestedPathWithExtension(path, ".json");
}

export function suggestedBinaryPath(path: string): string {
  return suggestedPathWithExtension(path, ".insysb");
}
