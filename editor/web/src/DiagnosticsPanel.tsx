import type { Diagnostic } from "./api";

interface DiagnosticsPanelProps {
  diagnostics: Diagnostic[];
  onSelect: (diagnostic: Diagnostic) => void;
}

function locationOf(diagnostic: Diagnostic): string {
  if (diagnostic.line && diagnostic.column) {
    return `${diagnostic.line}:${diagnostic.column}`;
  }
  return diagnostic.jsonPointer ?? "";
}

export default function DiagnosticsPanel({
  diagnostics,
  onSelect,
}: DiagnosticsPanelProps) {
  const errorCount = diagnostics.filter(
    (diagnostic) => diagnostic.severity === "error",
  ).length;
  return (
    <aside className="panel diagnostics-panel">
      <div className="panel-heading">
        <h2>Diagnostics</h2>
        <span className={errorCount > 0 ? "badge badge-error" : "badge"}>
          {errorCount} error{errorCount === 1 ? "" : "s"}
        </span>
      </div>
      {diagnostics.length === 0 ? (
        <p className="empty">No diagnostics.</p>
      ) : (
        <ul className="diagnostic-list">
          {diagnostics.map((diagnostic, index) => (
            <li
              key={`${diagnostic.code}-${diagnostic.jsonPointer ?? ""}-${index}`}
              className={`diagnostic ${diagnostic.severity}`}
            >
              <button
                type="button"
                className="diagnostic-button"
                onClick={() => onSelect(diagnostic)}
              >
                <span className="diagnostic-heading">
                  <span className="diagnostic-severity">{diagnostic.severity}</span>
                  <span className="diagnostic-code">{diagnostic.code}</span>
                  {locationOf(diagnostic) ? (
                    <span className="diagnostic-location">{locationOf(diagnostic)}</span>
                  ) : null}
                </span>
                <span className="diagnostic-message">{diagnostic.message}</span>
              </button>
            </li>
          ))}
        </ul>
      )}
    </aside>
  );
}
