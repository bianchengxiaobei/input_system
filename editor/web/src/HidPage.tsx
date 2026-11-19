import { useEffect, useMemo, useRef, useState } from "react";

import {
  bytesToHex,
  descriptorSummary,
  hidErrorCodeLabel,
  parseHexBytes,
  reportRate,
  type HidDescriptor,
  type HidDeviceView,
  type HidMonitorPoll,
  type HidMonitorReport,
  type HidReportKind,
  type HidToolError,
} from "./hid";

export type HidRpc = (method: string, params?: Record<string, unknown>) => Promise<unknown>;

interface Props {
  online: boolean;
  request: HidRpc;
}

interface HidMonitorState {
  id: string;
  deviceId: string;
  paused: boolean;
}

interface PendingWrite {
  method: string;
  params: Record<string, unknown>;
  label: string;
}

function numberValue(value: string, fallback: number): number {
  const parsed = Number(value);
  return Number.isFinite(parsed) ? parsed : fallback;
}

function reportTypeLabel(type: HidReportKind): string {
  return type.charAt(0).toUpperCase() + type.slice(1);
}

export default function HidPage({ online, request }: Props) {
  const [devices, setDevices] = useState<HidDeviceView[]>([]);
  const [selectedId, setSelectedId] = useState("");
  const [descriptor, setDescriptor] = useState<HidDescriptor | null>(null);
  const [descriptorLoading, setDescriptorLoading] = useState(false);
  const [showRaw, setShowRaw] = useState(false);
  const [errors, setErrors] = useState<HidToolError[]>([]);
  const [toolError, setToolError] = useState("");
  const [toolMessage, setToolMessage] = useState("");
  const [monitor, setMonitor] = useState<HidMonitorState | null>(null);
  const monitorRef = useRef<HidMonitorState | null>(null);
  const [reports, setReports] = useState<HidMonitorReport[]>([]);
  const [dropped, setDropped] = useState(0);
  const [monitorTypes, setMonitorTypes] = useState<HidReportKind[]>(["input"]);
  const [monitorError, setMonitorError] = useState("");
  const [pendingWrite, setPendingWrite] = useState<PendingWrite | null>(null);
  const [writeBusy, setWriteBusy] = useState(false);

  const [featureReportId, setFeatureReportId] = useState(0);
  const [featureReadSize, setFeatureReadSize] = useState(64);
  const [featureRead, setFeatureRead] = useState<number[]>([]);
  const [featureWriteHex, setFeatureWriteHex] = useState("");
  const [outputHex, setOutputHex] = useState("");

  const [hapticLeft, setHapticLeft] = useState(0.5);
  const [hapticRight, setHapticRight] = useState(0.5);
  const [lightRed, setLightRed] = useState(0);
  const [lightGreen, setLightGreen] = useState(0);
  const [lightBlue, setLightBlue] = useState(0);
  const [adaptiveSide, setAdaptiveSide] = useState<"left" | "right">("right");
  const [adaptiveMode, setAdaptiveMode] = useState(0x21);
  const [adaptiveParamsHex, setAdaptiveParamsHex] = useState("80 C8");
  const [switchSubcommand, setSwitchSubcommand] = useState(0x03);
  const [switchPayloadHex, setSwitchPayloadHex] = useState("30");

  const selected = useMemo(
    () => devices.find((device) => device.id === selectedId) ?? null,
    [devices, selectedId],
  );

  useEffect(() => {
    if (!online) {
      return;
    }
    let active = true;
    const refresh = async () => {
      try {
        const next = (await request("hid.devices")) as HidDeviceView[];
        const nextErrors = (await request("hid.errors")) as { errors?: HidToolError[] };
        if (!active) {
          return;
        }
        setDevices(next);
        setErrors(nextErrors.errors ?? []);
        setSelectedId((current) => {
          if (next.some((device) => device.id === current)) {
            return current;
          }
          return next[0]?.id ?? "";
        });
      } catch (error) {
        if (active) {
          setToolError(error instanceof Error ? error.message : String(error));
        }
      }
    };
    void refresh();
    const timer = window.setInterval(() => void refresh(), 1000);
    return () => {
      active = false;
      window.clearInterval(timer);
    };
  }, [online, request]);

  useEffect(() => {
    if (!online || !selectedId) {
      setDescriptor(null);
      return;
    }
    let active = true;
    setDescriptorLoading(true);
    request("hid.descriptor", { deviceId: selectedId })
      .then((result) => {
        if (active) {
          setDescriptor(result as HidDescriptor);
          setToolError("");
        }
      })
      .catch((error: unknown) => {
        if (active) {
          setDescriptor(null);
          setToolError(error instanceof Error ? error.message : String(error));
        }
      })
      .finally(() => {
        if (active) {
          setDescriptorLoading(false);
        }
      });
    return () => {
      active = false;
    };
  }, [online, request, selectedId]);

  const selectedFeatureSize = selected?.featureReportSize ?? 0;
  const selectedDeviceId = selected?.id ?? "";

  useEffect(() => {
    if (!selectedDeviceId) {
      return;
    }
    setFeatureReadSize(Math.max(1, selectedFeatureSize || 64));
    setFeatureRead([]);
    setFeatureWriteHex("");
    setOutputHex("");
  }, [selectedDeviceId, selectedFeatureSize]);

  useEffect(() => {
    monitorRef.current = monitor;
  }, [monitor]);

  useEffect(() => {
    return () => {
      const current = monitorRef.current;
      if (current) {
        void request("hid.reports.stop", { monitorId: current.id }).catch(() => {});
      }
    };
  }, [request]);

  const monitorId = monitor?.id ?? "";
  const monitorPaused = monitor?.paused ?? false;

  useEffect(() => {
    if (!online || !monitor || monitor.paused) {
      return;
    }
    let active = true;
    let timer = 0;
    const poll = async () => {
      try {
        const result = (await request("hid.reports.poll", {
          monitorId: monitor.id,
          max: 200,
        })) as HidMonitorPoll;
        if (!active) {
          return;
        }
        if (result.reports.length > 0) {
          setReports((current) => [...current, ...result.reports].slice(-2000));
        }
        if (result.dropped > 0) {
          setDropped((current) => current + result.dropped);
        }
        setMonitorError("");
        setMonitor((current) =>
          current && current.id === monitor.id
            ? { ...current, paused: result.paused }
            : current,
        );
      } catch (error) {
        if (active) {
          setMonitorError(error instanceof Error ? error.message : String(error));
        }
      }
      if (active) {
        timer = window.setTimeout(() => void poll(), 100);
      }
    };
    void poll();
    return () => {
      active = false;
      window.clearTimeout(timer);
    };
  }, [monitorId, monitorPaused, online, request]);

  async function stopMonitor(): Promise<void> {
    const current = monitor;
    setMonitor(null);
    if (!current) {
      return;
    }
    try {
      await request("hid.reports.stop", { monitorId: current.id });
    } catch (error) {
      setMonitorError(error instanceof Error ? error.message : String(error));
    }
  }

  async function startMonitor(): Promise<void> {
    if (!selected) {
      setMonitorError("select a HID device first");
      return;
    }
    await stopMonitor();
    try {
      const result = (await request("hid.reports.start", {
        deviceId: selected.id,
        types: monitorTypes,
      })) as { monitorId: string };
      setReports([]);
      setDropped(0);
      setMonitorError("");
      setMonitor({
        id: result.monitorId,
        deviceId: selected.id,
        paused: false,
      });
    } catch (error) {
      setMonitorError(error instanceof Error ? error.message : String(error));
    }
  }

  async function toggleMonitorPause(): Promise<void> {
    if (!monitor) {
      return;
    }
    try {
      const result = (await request("hid.reports.pause", {
        monitorId: monitor.id,
        paused: !monitor.paused,
      })) as { paused?: boolean };
      setMonitor({ ...monitor, paused: result.paused ?? !monitor.paused });
      setMonitorError("");
    } catch (error) {
      setMonitorError(error instanceof Error ? error.message : String(error));
    }
  }

  async function clearMonitor(): Promise<void> {
    if (!monitor) {
      setReports([]);
      setDropped(0);
      return;
    }
    try {
      await request("hid.reports.clear", { monitorId: monitor.id });
      setReports([]);
      setDropped(0);
      setMonitorError("");
    } catch (error) {
      setMonitorError(error instanceof Error ? error.message : String(error));
    }
  }

  function exportReports(): void {
    if (!monitor) {
      return;
    }
    const lines = reports.map((report) =>
      JSON.stringify({
        deviceId: monitor.deviceId,
        sequence: report.sequence,
        type: report.type,
        time: report.time,
        data: report.data,
      }),
    );
    const blob = new Blob([`${lines.join("\n")}\n`], { type: "application/jsonl" });
    const url = URL.createObjectURL(blob);
    const anchor = document.createElement("a");
    anchor.href = url;
    anchor.download = `hid-${monitor.deviceId}.jsonl`;
    anchor.click();
    URL.revokeObjectURL(url);
  }

  function toggleMonitorType(type: HidReportKind): void {
    setMonitorTypes((current) =>
      current.includes(type) ? current.filter((item) => item !== type) : [...current, type],
    );
  }

  async function selectDevice(id: string): Promise<void> {
    if (id === selectedId) {
      return;
    }
    if (monitor && monitor.deviceId !== id) {
      await stopMonitor();
    }
    setSelectedId(id);
  }

  async function readFeature(): Promise<void> {
    if (!selected) {
      return;
    }
    try {
      const result = (await request("hid.feature.read", {
        deviceId: selected.id,
        reportId: featureReportId,
        size: featureReadSize,
      })) as { bytes: number[] };
      setFeatureRead(result.bytes);
      setToolMessage(`read feature report ${featureReportId} (${result.bytes.length} bytes)`);
      setToolError("");
    } catch (error) {
      setToolError(error instanceof Error ? error.message : String(error));
    }
  }

  function queueWrite(method: string, params: Record<string, unknown>, label: string): void {
    setPendingWrite({ method, params, label });
    setToolError("");
    setToolMessage("");
  }

  async function confirmWrite(): Promise<void> {
    if (!pendingWrite) {
      return;
    }
    setWriteBusy(true);
    try {
      await request(pendingWrite.method, { ...pendingWrite.params, confirmed: true });
      setToolMessage(`sent ${pendingWrite.label}`);
      setToolError("");
      setPendingWrite(null);
    } catch (error) {
      setToolError(error instanceof Error ? error.message : String(error));
    } finally {
      setWriteBusy(false);
    }
  }

  function queueFeatureWrite(): void {
    if (!selected) {
      return;
    }
    const parsed = parseHexBytes(featureWriteHex);
    if (parsed.error) {
      setToolError(parsed.error);
      return;
    }
    if (parsed.bytes.length === 0) {
      setToolError("enter feature report bytes");
      return;
    }
    queueWrite(
      "hid.feature.write",
      { deviceId: selected.id, bytes: parsed.bytes },
      `feature write (${parsed.bytes.length} bytes)`,
    );
  }

  function queueOutputWrite(): void {
    if (!selected) {
      return;
    }
    const parsed = parseHexBytes(outputHex);
    if (parsed.error) {
      setToolError(parsed.error);
      return;
    }
    if (parsed.bytes.length === 0) {
      setToolError("enter output report bytes");
      return;
    }
    queueWrite(
      "hid.output.write",
      { deviceId: selected.id, bytes: parsed.bytes },
      `output write (${parsed.bytes.length} bytes)`,
    );
  }

  function queueHaptics(): void {
    if (!selected) {
      return;
    }
    queueWrite(
      "hid.haptics",
      { deviceId: selected.id, left: hapticLeft, right: hapticRight },
      `rumble left ${hapticLeft.toFixed(2)} right ${hapticRight.toFixed(2)}`,
    );
  }

  function queueLightbar(): void {
    if (!selected) {
      return;
    }
    queueWrite(
      "hid.lightbar",
      { deviceId: selected.id, r: lightRed / 255, g: lightGreen / 255, b: lightBlue / 255 },
      `lightbar rgb(${lightRed}, ${lightGreen}, ${lightBlue})`,
    );
  }

  function queueAdaptiveTrigger(): void {
    if (!selected) {
      return;
    }
    const parsed = parseHexBytes(adaptiveParamsHex);
    if (parsed.error) {
      setToolError(parsed.error);
      return;
    }
    queueWrite(
      "hid.adaptiveTrigger",
      {
        deviceId: selected.id,
        side: adaptiveSide,
        mode: adaptiveMode,
        params: parsed.bytes,
      },
      `${adaptiveSide} adaptive trigger mode 0x${adaptiveMode.toString(16)}`,
    );
  }

  function queueSwitchSubcommand(): void {
    if (!selected) {
      return;
    }
    const parsed = parseHexBytes(switchPayloadHex);
    if (parsed.error) {
      setToolError(parsed.error);
      return;
    }
    queueWrite(
      "hid.switch.subcommand",
      {
        deviceId: selected.id,
        subcommand: switchSubcommand,
        payload: parsed.bytes,
      },
      `Switch subcommand 0x${switchSubcommand.toString(16)}`,
    );
  }

  async function clearErrors(): Promise<void> {
    try {
      await request("hid.errors.clear");
      setErrors([]);
      setToolError("");
    } catch (error) {
      setToolError(error instanceof Error ? error.message : String(error));
    }
  }

  const capabilities = selected?.capabilities;
  const visibleReports = reports.slice(-200).reverse();
  const currentRate = reportRate(reports);

  return (
    <div className="hid-page">
      <div className="hid-toolbar">
        <div>
          <strong>HID tools</strong>
          <span className="muted"> · local devices, descriptors, raw reports and output tests</span>
        </div>
        <div className="hid-toolbar-actions">
          <span className={`hid-status ${online ? "online" : "offline"}`}>
            {online ? `${devices.length} device${devices.length === 1 ? "" : "s"}` : "offline"}
          </span>
          {monitor ? (
            <span className="hid-monitor-badge">
              monitor {monitor.id} · {reports.length} reports
              {currentRate > 0 ? ` · ${currentRate.toFixed(1)} Hz` : ""}
            </span>
          ) : null}
        </div>
      </div>

      {toolError ? <div className="hid-error-banner">{toolError}</div> : null}
      {toolMessage ? <div className="hid-message-banner">{toolMessage}</div> : null}

      <div className="hid-layout">
        <section className="hid-device-panel">
          <div className="hid-section-heading">
            <span>Devices</span>
            <button
              type="button"
              className="small-button"
              onClick={() => {
                setToolError("");
                void request("hid.devices")
                  .then((result) => setDevices(result as HidDeviceView[]))
                  .catch((error: unknown) =>
                    setToolError(error instanceof Error ? error.message : String(error)),
                  );
              }}
            >
              Refresh
            </button>
          </div>
          <div className="hid-device-list">
            {devices.length === 0 ? (
              <div className="hid-empty">
                No HID devices found. Connect a controller and check device permissions.
              </div>
            ) : (
              devices.map((device) => (
                <button
                  type="button"
                  key={device.id}
                  className={device.id === selectedId ? "hid-device-row selected" : "hid-device-row"}
                  onClick={() => void selectDevice(device.id)}
                >
                  <span className="hid-device-name">
                    {device.product || device.id}
                    <small>
                      {device.kind} · {device.transport}
                    </small>
                  </span>
                  <span className="hid-device-meta">
                    {device.vendorId.toString(16).padStart(4, "0")}:
                    {device.productId.toString(16).padStart(4, "0")}
                  </span>
                </button>
              ))
            )}
          </div>
          {errors.length > 0 ? (
            <div className="hid-errors">
              <div className="hid-section-heading">
                <span>HID errors</span>
                <button type="button" className="small-button" onClick={() => void clearErrors()}>
                  Clear
                </button>
              </div>
              {errors.slice(-8).reverse().map((error, index) => (
                <div className="hid-error-row" key={`${error.time}-${index}`}>
                  <span className="hid-error-code">{hidErrorCodeLabel(error.code)}</span>
                  <span>
                    {error.deviceId ? `${error.deviceId}: ` : ""}
                    {error.message}
                  </span>
                </div>
              ))}
            </div>
          ) : null}
        </section>

        <section className="hid-detail-panel">
          {!selected ? (
            <div className="hid-empty">Select a HID device to inspect its descriptor.</div>
          ) : (
            <>
              <div className="hid-device-summary">
                <div>
                  <h3>{selected.product || selected.id}</h3>
                  <div className="muted">
                    {selected.manufacturer || "Unknown manufacturer"} · {selected.id}
                  </div>
                </div>
                <div className="hid-summary-grid">
                  <span>Usage</span>
                  <strong>
                    0x{selected.usagePage.toString(16)}:0x{selected.usage.toString(16)}
                  </strong>
                  <span>Reports</span>
                  <strong>
                    in {selected.inputReportSize} / out {selected.outputReportSize} / feature{" "}
                    {selected.featureReportSize}
                  </strong>
                  <span>Layout</span>
                  <strong>{selected.layoutName || "none"}</strong>
                </div>
              </div>

              <div className="hid-two-columns">
                <div className="hid-card">
                  <div className="hid-section-heading">
                    <span>Descriptor</span>
                    <div className="hid-inline-actions">
                      <button
                        type="button"
                        className="small-button"
                        onClick={() => setShowRaw((current) => !current)}
                        disabled={!descriptor}
                      >
                        {showRaw ? "Hide raw" : "Raw"}
                      </button>
                      <span className="muted">{descriptorLoading ? "loading..." : descriptorSummary(descriptor)}</span>
                    </div>
                  </div>
                  {descriptor?.error ? <div className="hid-warning">{descriptor.error}</div> : null}
                  {showRaw && descriptor ? (
                    <pre className="hid-raw">{bytesToHex(descriptor.raw)}</pre>
                  ) : (
                    <div className="hid-descriptor">
                      <details open>
                        <summary>Elements ({descriptor?.elements.length ?? 0})</summary>
                        <div className="hid-table-scroll">
                          <table className="hid-table">
                            <thead>
                              <tr>
                                <th>Usage</th>
                                <th>Type</th>
                                <th>Id</th>
                                <th>Bits</th>
                                <th>Offset</th>
                                <th>Logical</th>
                                <th>Flags</th>
                              </tr>
                            </thead>
                            <tbody>
                              {(descriptor?.elements ?? []).map((element, index) => (
                                <tr key={`${element.reportType}-${element.reportId}-${index}`}>
                                  <td>
                                    0x{element.usagePage.toString(16)}:0x{element.usage.toString(16)}
                                  </td>
                                  <td>{element.reportType}</td>
                                  <td>{element.reportId}</td>
                                  <td>{element.reportSizeInBits}</td>
                                  <td>{element.reportOffsetInBits}</td>
                                  <td>
                                    {element.logicalMin}..{element.logicalMax}
                                  </td>
                                  <td className="hid-flags">
                                    {element.isConstant ? "const " : ""}
                                    {element.isArray ? "array " : ""}
                                    {element.isRelative ? "rel " : ""}
                                    {element.isButton ? "button " : ""}
                                    {element.hasNullState ? "null" : ""}
                                  </td>
                                </tr>
                              ))}
                            </tbody>
                          </table>
                        </div>
                      </details>
                      <details>
                        <summary>Reports ({descriptor?.reports.length ?? 0})</summary>
                        <div className="hid-pills">
                          {(descriptor?.reports ?? []).map((report) => (
                            <span className="hid-pill" key={`${report.type}-${report.id}`}>
                              {reportLabel(report)}
                            </span>
                          ))}
                        </div>
                      </details>
                      <details>
                        <summary>Collections ({descriptor?.collections.length ?? 0})</summary>
                        <div className="hid-table-scroll">
                          <table className="hid-table">
                            <thead>
                              <tr>
                                <th>Type</th>
                                <th>Usage</th>
                                <th>Parent</th>
                                <th>First child</th>
                                <th>Children</th>
                              </tr>
                            </thead>
                            <tbody>
                              {(descriptor?.collections ?? []).map((collection, index) => (
                                <tr key={`${collection.type}-${index}`}>
                                  <td>{collection.type}</td>
                                  <td>
                                    0x{collection.usagePage.toString(16)}:0x
                                    {collection.usage.toString(16)}
                                  </td>
                                  <td>{collection.parent}</td>
                                  <td>{collection.firstChild}</td>
                                  <td>{collection.childCount}</td>
                                </tr>
                              ))}
                            </tbody>
                          </table>
                        </div>
                      </details>
                      <details>
                        <summary>Generic layout ({descriptor?.genericLayout.length ?? 0})</summary>
                        <div className="hid-table-scroll">
                          <table className="hid-table">
                            <thead>
                              <tr>
                                <th>Path</th>
                                <th>Layout</th>
                                <th>Byte</th>
                                <th>Bit</th>
                                <th>Bits</th>
                                <th>Value</th>
                              </tr>
                            </thead>
                            <tbody>
                              {(descriptor?.genericLayout ?? []).map((control) => (
                                <tr key={control.path}>
                                  <td className="hid-path">{control.path}</td>
                                  <td>{control.layout}</td>
                                  <td>{control.byteOffset}</td>
                                  <td>{control.bitOffset}</td>
                                  <td>{control.sizeInBits}</td>
                                  <td>{control.value.toFixed(3)}</td>
                                </tr>
                              ))}
                            </tbody>
                          </table>
                        </div>
                      </details>
                    </div>
                  )}
                </div>

                <div className="hid-card">
                  <div className="hid-section-heading">
                    <span>Raw report monitor</span>
                    {monitor ? (
                      <span className="muted">
                        {monitor.paused ? "paused" : "live"} · dropped {dropped}
                      </span>
                    ) : null}
                  </div>
                  <div className="hid-monitor-controls">
                    <label>
                      <input
                        type="checkbox"
                        checked={monitorTypes.includes("input")}
                        disabled={monitor !== null}
                        onChange={() => toggleMonitorType("input")}
                      />
                      Input
                    </label>
                    <label>
                      <input
                        type="checkbox"
                        checked={monitorTypes.includes("output")}
                        disabled={monitor !== null}
                        onChange={() => toggleMonitorType("output")}
                      />
                      Output
                    </label>
                    <label>
                      <input
                        type="checkbox"
                        checked={monitorTypes.includes("feature")}
                        disabled={monitor !== null}
                        onChange={() => toggleMonitorType("feature")}
                      />
                      Feature
                    </label>
                    {monitor ? (
                      <>
                        <button type="button" className="small-button" onClick={() => void toggleMonitorPause()}>
                          {monitor.paused ? "Resume" : "Pause"}
                        </button>
                        <button type="button" className="small-button" onClick={() => void clearMonitor()}>
                          Clear
                        </button>
                        <button type="button" className="small-button" onClick={exportReports}>
                          Export JSONL
                        </button>
                        <button type="button" className="small-button" onClick={() => void stopMonitor()}>
                          Stop
                        </button>
                      </>
                    ) : (
                      <button
                        type="button"
                        className="small-button primary"
                        onClick={() => void startMonitor()}
                      >
                        Start monitor
                      </button>
                    )}
                  </div>
                  {monitorError ? <div className="hid-error-inline">{monitorError}</div> : null}
                  <div className="hid-report-scroll">
                    {visibleReports.length === 0 ? (
                      <div className="hid-empty small">No reports captured.</div>
                    ) : (
                      <table className="hid-table hid-report-table">
                        <thead>
                          <tr>
                            <th>#</th>
                            <th>Type</th>
                            <th>Time</th>
                            <th>Bytes</th>
                          </tr>
                        </thead>
                        <tbody>
                          {visibleReports.map((report) => (
                            <tr key={`${report.sequence}-${report.time}`}>
                              <td>{report.sequence}</td>
                              <td>{reportTypeLabel(report.type)}</td>
                              <td>{report.time.toFixed(3)}</td>
                              <td className="hid-hex">{bytesToHex(report.data)}</td>
                            </tr>
                          ))}
                        </tbody>
                      </table>
                    )}
                  </div>
                </div>
              </div>

              <div className="hid-card">
                <div className="hid-section-heading">
                  <span>Feature and output reports</span>
                  <span className="muted">writes require confirmation</span>
                </div>
                <div className="hid-forms-grid">
                  <div className="hid-form">
                    <h4>Feature read</h4>
                    <label>
                      Report id
                      <input
                        type="number"
                        min={0}
                        max={255}
                        value={featureReportId}
                        onChange={(event) => setFeatureReportId(numberValue(event.target.value, 0))}
                      />
                    </label>
                    <label>
                      Size
                      <input
                        type="number"
                        min={1}
                        max={4096}
                        value={featureReadSize}
                        onChange={(event) => setFeatureReadSize(numberValue(event.target.value, 1))}
                      />
                    </label>
                    <button type="button" className="small-button" onClick={() => void readFeature()}>
                      Read feature
                    </button>
                    {featureRead.length > 0 ? (
                      <div className="hid-result-hex">{bytesToHex(featureRead)}</div>
                    ) : null}
                  </div>

                  <div className="hid-form">
                    <h4>Feature write</h4>
                    <label>
                      Hex bytes
                      <textarea
                        value={featureWriteHex}
                        onChange={(event) => setFeatureWriteHex(event.target.value)}
                        placeholder="report id and data, e.g. 05 00 00"
                        rows={3}
                      />
                    </label>
                    <button type="button" className="small-button" onClick={queueFeatureWrite}>
                      Review feature write
                    </button>
                  </div>

                  <div className="hid-form">
                    <h4>Output write</h4>
                    <label>
                      Hex bytes
                      <textarea
                        value={outputHex}
                        onChange={(event) => setOutputHex(event.target.value)}
                        placeholder="full output report including report id"
                        rows={3}
                      />
                    </label>
                    <button type="button" className="small-button" onClick={queueOutputWrite}>
                      Review output write
                    </button>
                  </div>
                </div>
              </div>

              <div className="hid-card">
                <div className="hid-section-heading">
                  <span>Device test panel</span>
                  <span className="muted">{selected.kind}</span>
                </div>
                <div className="hid-forms-grid">
                  <div className="hid-form">
                    <h4>Rumble</h4>
                    <label>
                      Left {hapticLeft.toFixed(2)}
                      <input
                        type="range"
                        min={0}
                        max={1}
                        step={0.05}
                        value={hapticLeft}
                        onChange={(event) => setHapticLeft(numberValue(event.target.value, 0))}
                        disabled={!capabilities?.haptics}
                      />
                    </label>
                    <label>
                      Right {hapticRight.toFixed(2)}
                      <input
                        type="range"
                        min={0}
                        max={1}
                        step={0.05}
                        value={hapticRight}
                        onChange={(event) => setHapticRight(numberValue(event.target.value, 0))}
                        disabled={!capabilities?.haptics}
                      />
                    </label>
                    <button
                      type="button"
                      className="small-button"
                      onClick={queueHaptics}
                      disabled={!capabilities?.haptics}
                    >
                      Review rumble
                    </button>
                  </div>

                  <div className="hid-form">
                    <h4>Lightbar</h4>
                    <label>
                      R {lightRed}
                      <input
                        type="range"
                        min={0}
                        max={255}
                        value={lightRed}
                        onChange={(event) => setLightRed(numberValue(event.target.value, 0))}
                        disabled={!capabilities?.lightbar}
                      />
                    </label>
                    <label>
                      G {lightGreen}
                      <input
                        type="range"
                        min={0}
                        max={255}
                        value={lightGreen}
                        onChange={(event) => setLightGreen(numberValue(event.target.value, 0))}
                        disabled={!capabilities?.lightbar}
                      />
                    </label>
                    <label>
                      B {lightBlue}
                      <input
                        type="range"
                        min={0}
                        max={255}
                        value={lightBlue}
                        onChange={(event) => setLightBlue(numberValue(event.target.value, 0))}
                        disabled={!capabilities?.lightbar}
                      />
                    </label>
                    <button
                      type="button"
                      className="small-button"
                      onClick={queueLightbar}
                      disabled={!capabilities?.lightbar}
                    >
                      Review lightbar
                    </button>
                  </div>

                  <div className="hid-form">
                    <h4>Adaptive trigger</h4>
                    <label>
                      Side
                      <select
                        value={adaptiveSide}
                        onChange={(event) => setAdaptiveSide(event.target.value as "left" | "right")}
                        disabled={!capabilities?.adaptiveTrigger}
                      >
                        <option value="right">Right</option>
                        <option value="left">Left</option>
                      </select>
                    </label>
                    <label>
                      Mode
                      <input
                        type="number"
                        min={0}
                        max={255}
                        value={adaptiveMode}
                        onChange={(event) => setAdaptiveMode(numberValue(event.target.value, 0))}
                        disabled={!capabilities?.adaptiveTrigger}
                      />
                    </label>
                    <label>
                      Params hex
                      <input
                        value={adaptiveParamsHex}
                        onChange={(event) => setAdaptiveParamsHex(event.target.value)}
                        disabled={!capabilities?.adaptiveTrigger}
                      />
                    </label>
                    <button
                      type="button"
                      className="small-button"
                      onClick={queueAdaptiveTrigger}
                      disabled={!capabilities?.adaptiveTrigger}
                    >
                      Review trigger
                    </button>
                  </div>

                  <div className="hid-form">
                    <h4>Switch subcommand</h4>
                    <label>
                      Subcommand
                      <input
                        type="number"
                        min={0}
                        max={255}
                        value={switchSubcommand}
                        onChange={(event) => setSwitchSubcommand(numberValue(event.target.value, 0))}
                        disabled={!capabilities?.switchSubcommand}
                      />
                    </label>
                    <label>
                      Payload hex
                      <input
                        value={switchPayloadHex}
                        onChange={(event) => setSwitchPayloadHex(event.target.value)}
                        disabled={!capabilities?.switchSubcommand}
                      />
                    </label>
                    <button
                      type="button"
                      className="small-button"
                      onClick={queueSwitchSubcommand}
                      disabled={!capabilities?.switchSubcommand}
                    >
                      Review subcommand
                    </button>
                  </div>
                </div>
              </div>

              {pendingWrite ? (
                <div className="hid-confirm-bar">
                  <span>
                    Confirm {pendingWrite.label} on <strong>{selected.id}</strong>?
                  </span>
                  <div>
                    <button
                      type="button"
                      className="small-button"
                      onClick={() => setPendingWrite(null)}
                      disabled={writeBusy}
                    >
                      Cancel
                    </button>
                    <button
                      type="button"
                      className="small-button primary"
                      onClick={() => void confirmWrite()}
                      disabled={writeBusy}
                    >
                      {writeBusy ? "Sending..." : "Confirm send"}
                    </button>
                  </div>
                </div>
              ) : null}
            </>
          )}
        </section>
      </div>
    </div>
  );
}

function reportLabel(report: { id: number; type: string; sizeInBytes: number }): string {
  return `${report.type} ${report.id} · ${report.sizeInBytes} bytes`;
}
