import { PREVIEW_INTERVAL_OPTIONS } from "./preferences";
import type { PreviewMouseInput, PreviewSnapshot } from "./preview";

export interface CaptureState {
  expectedControlType: string;
  deviceFilter: string;
  label: string;
}

interface PreviewPageProps {
  snapshot: PreviewSnapshot;
  running: boolean;
  generation: number;
  busy: boolean;
  error: string;
  drive: boolean;
  capture: CaptureState | null;
  onStart: () => void;
  onStop: () => void;
  onToggleDrive: (enabled: boolean) => void;
  onCancelCapture: () => void;
  onMouse: (input: PreviewMouseInput) => void;
  pollIntervalMs: number;
  onPollIntervalChange: (value: number) => void;
}

function formatNumber(value: number): string {
  return Number.isFinite(value) ? value.toFixed(3) : "-";
}

function phaseClass(phase: string): string {
  return `phase-${phase.toLowerCase()}`;
}

function mouseButton(button: number): number {
  if (button === 1) {
    return 2;
  }
  if (button === 2) {
    return 1;
  }
  if (button === 3) {
    return 4;
  }
  if (button === 4) {
    return 3;
  }
  return button;
}

export default function PreviewPage({
  snapshot,
  running,
  generation,
  busy,
  error,
  drive,
  capture,
  onStart,
  onStop,
  onToggleDrive,
  onCancelCapture,
  onMouse,
  pollIntervalMs,
  onPollIntervalChange,
}: PreviewPageProps) {
  function mousePosition(target: HTMLElement, clientX: number, clientY: number) {
    const rect = target.getBoundingClientRect();
    return {
      x: ((clientX - rect.left) / Math.max(1, rect.width)) * 2 - 1,
      y: 1 - ((clientY - rect.top) / Math.max(1, rect.height)) * 2,
    };
  }

  return (
    <div className="preview-panel">
      <div className="preview-toolbar">
        <div className="preview-actions">
          <button
            type="button"
            className={running ? "small-button" : "small-button primary-small"}
            onClick={running ? onStop : onStart}
            disabled={busy}
          >
            {running ? "Stop preview" : "Start preview"}
          </button>
          <label className="check-field">
            <input
              type="checkbox"
              checked={drive}
              onChange={(event) => onToggleDrive(event.target.checked)}
              disabled={!running}
            />
            <span>Drive input</span>
          </label>
          <label className="check-field">
            <span>Poll</span>
            <select
              value={pollIntervalMs}
              onChange={(event) => onPollIntervalChange(Number(event.target.value))}
            >
              {PREVIEW_INTERVAL_OPTIONS.map((interval) => (
                <option key={interval} value={interval}>
                  {Math.round(1000 / interval)} Hz
                </option>
              ))}
            </select>
          </label>
        </div>
        <div className="preview-status">
          <span className={`preview-dot ${running ? "online" : ""}`} />
          {running ? `running · generation ${generation || snapshot.generation}` : "stopped"}
          {snapshot.hid.enabled
            ? ` · HID ${snapshot.hid.platform ? "connected" : "unavailable"}`
            : " · HID off"}
        </div>
      </div>

      {capture ? (
        <div className="capture-banner">
          <span>
            Listening for {capture.expectedControlType || "any control"}
            {capture.deviceFilter ? ` on ${capture.deviceFilter}` : ""} · {capture.label}
          </span>
          <button type="button" className="small-button" onClick={onCancelCapture}>
            Cancel (Esc)
          </button>
        </div>
      ) : null}

      {error ? <p className="inline-warning">{error}</p> : null}

      <div className="preview-grid">
        <section className="preview-card device-tree">
          <header>
            <h4>Devices</h4>
            <span className="count">{snapshot.devices.length}</span>
          </header>
          {snapshot.devices.length === 0 ? (
            <p className="empty">Start the preview to create keyboard and mouse devices.</p>
          ) : (
            snapshot.devices.map((device) => {
              const active = device.controls.filter((control) => control.value !== "0.00" &&
                control.value !== "(0.00, 0.00)");
              const controls =
                device.layout.toLowerCase() === "keyboard" ? active : device.controls;
              return (
                <details
                  className="device-entry"
                  key={device.deviceId}
                >
                  <summary>
                    <span className="device-name">{device.name}</span>
                    <span className="badge">{device.layout}</span>
                    <span className="count">#{device.deviceId}</span>
                  </summary>
                  <div className="control-list">
                    {controls.length === 0 ? (
                      <p className="empty">No active controls</p>
                    ) : (
                      controls.map((control) => (
                        <div className="control-row" key={`${device.deviceId}-${control.path}`}>
                          <div className="control-path" title={control.path}>
                            {control.path}
                          </div>
                          <span className="control-layout">{control.layout}</span>
                          <span className="control-value">{control.value}</span>
                        </div>
                      ))
                    )}
                  </div>
                </details>
              );
            })
          )}
        </section>

        <section className="preview-card action-states">
          <header>
            <h4>Actions</h4>
            <span className="count">{snapshot.actions.length}</span>
          </header>
          {snapshot.actions.length === 0 ? (
            <p className="empty">No actions in the preview asset.</p>
          ) : (
            <div className="action-table">
              {snapshot.actions.map((action) => (
                <div className="action-state" key={`${action.map}/${action.name}`}>
                  <div className="action-state-heading">
                    <span className="action-map">{action.map}</span>
                    <strong>{action.name}</strong>
                    <span className={`phase-pill ${phaseClass(action.phase)}`}>
                      {action.phase}
                    </span>
                  </div>
                  <div className="action-state-detail">
                    <span>value {action.value}</span>
                    <span>
                      {action.drivingControl || "no driving control"}
                    </span>
                    <span>performed {formatNumber(action.lastPerformedTime)}s</span>
                  </div>
                </div>
              ))}
            </div>
          )}
        </section>

        <section className="preview-card event-stream">
          <header>
            <h4>Events</h4>
            <span className="count">{snapshot.events.length}</span>
          </header>
          {snapshot.events.length === 0 ? (
            <p className="empty">Input events appear here.</p>
          ) : (
            <div className="event-list">
              {snapshot.events.slice(-30).reverse().map((event, index) => (
                <div className="event-row" key={`${event.time}-${event.type}-${index}`}>
                  <span className="event-time">{formatNumber(event.time)}</span>
                  <span className="event-type">{event.type}</span>
                  <span>device {event.deviceId}</span>
                  <span className="count">{event.size} B</span>
                </div>
              ))}
            </div>
          )}
        </section>
      </div>

      <div className="drive-pad-wrap">
        <div
          className={`drive-pad ${drive ? "active" : ""}`}
          onPointerMove={(event) => {
            if (!drive) {
              return;
            }
            const position = mousePosition(event.currentTarget, event.clientX, event.clientY);
            onMouse({ ...position });
          }}
          onPointerDown={(event) => {
            if (!drive) {
              return;
            }
            event.currentTarget.setPointerCapture(event.pointerId);
            const position = mousePosition(event.currentTarget, event.clientX, event.clientY);
            onMouse({ ...position, button: mouseButton(event.button), pressed: true });
          }}
          onPointerUp={(event) => {
            if (!drive) {
              return;
            }
            const position = mousePosition(event.currentTarget, event.clientX, event.clientY);
            onMouse({ ...position, button: mouseButton(event.button), pressed: false });
          }}
          onWheel={(event) => {
            if (!drive) {
              return;
            }
            onMouse({ wheelY: -event.deltaY / 100 });
          }}
          onContextMenu={(event) => {
            if (drive) {
              event.preventDefault();
            }
          }}
        >
          <span>Mouse drive pad</span>
          <small>pointer move · left/right/middle click · wheel</small>
        </div>
        <p className="inspector-help">
          Enable Drive input to send browser keyboard and mouse events to the preview. Use a
          Listen button on a binding or composite part to capture a control; Esc cancels.
        </p>
      </div>
    </div>
  );
}
