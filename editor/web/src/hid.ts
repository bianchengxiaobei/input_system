export type HidReportKind = "input" | "output" | "feature";

export interface HidCapabilities {
  haptics: boolean;
  lightbar: boolean;
  adaptiveTrigger: boolean;
  switchSubcommand: boolean;
}

export interface HidDeviceView {
  id: string;
  product: string;
  manufacturer: string;
  serial: string;
  vendorId: number;
  productId: number;
  usagePage: number;
  usage: number;
  transport: string;
  kind: string;
  layoutName: string;
  inputReportSize: number;
  outputReportSize: number;
  featureReportSize: number;
  hasDescriptor: boolean;
  capabilities: HidCapabilities;
}

export interface HidElement {
  usage: number;
  usagePage: number;
  logicalMin: number;
  logicalMax: number;
  physicalMin: number;
  physicalMax: number;
  unit: number;
  unitExponent: number;
  reportType: string;
  collectionIndex: number;
  reportId: number;
  reportSizeInBits: number;
  reportOffsetInBits: number;
  flags: number;
  isButton: boolean;
  isAxis: boolean;
  isArray: boolean;
  isConstant: boolean;
  isRelative: boolean;
  hasNullState: boolean;
}

export interface HidReportInfo {
  id: number;
  hasId: boolean;
  type: string;
  sizeInBits: number;
  sizeInBytes: number;
}

export interface HidCollection {
  type: number;
  usage: number;
  usagePage: number;
  parent: number;
  firstChild: number;
  childCount: number;
}

export interface HidControlView {
  path: string;
  name: string;
  layout: string;
  byteOffset: number;
  bitOffset: number;
  sizeInBits: number;
  isButton: boolean;
  isAxis: boolean;
  value: number;
}

export interface HidDescriptor {
  parsed: boolean;
  error?: string;
  raw: number[];
  elements: HidElement[];
  reports: HidReportInfo[];
  collections: HidCollection[];
  genericLayout: HidControlView[];
  device?: HidDeviceView;
}

export interface HidMonitorReport {
  sequence: number;
  type: HidReportKind;
  time: number;
  data: number[];
}

export interface HidMonitorPoll {
  reports: HidMonitorReport[];
  dropped: number;
  paused: boolean;
}

export interface HidToolError {
  code: number;
  deviceId: string;
  message: string;
  time: number;
}

export function bytesToHex(bytes: readonly number[]): string {
  return bytes
    .map((byte) => Math.max(0, Math.min(255, Math.round(byte))).toString(16).padStart(2, "0"))
    .join(" ");
}

export function parseHexBytes(text: string): { bytes: number[]; error: string } {
  const normalized = text.replace(/0x/gi, "").replace(/[\s,;:_-]/g, "");
  if (/[^0-9a-f]/i.test(normalized)) {
    return { bytes: [], error: "hex byte string contains an invalid character" };
  }
  if (normalized.length % 2 !== 0) {
    return { bytes: [], error: "hex byte string needs an even number of digits" };
  }
  const bytes: number[] = [];
  for (let index = 0; index < normalized.length; index += 2) {
    bytes.push(Number.parseInt(normalized.slice(index, index + 2), 16));
  }
  return { bytes, error: "" };
}

export function reportRate(reports: readonly HidMonitorReport[]): number {
  if (reports.length < 2) {
    return 0;
  }
  const sample = reports.slice(-64);
  const first = sample[0]?.time ?? 0;
  const last = sample[sample.length - 1]?.time ?? 0;
  const duration = last - first;
  if (duration <= 0) {
    return 0;
  }
  return (sample.length - 1) / duration;
}

export function hidErrorCodeLabel(code: number): string {
  const labels = [
    "Device open failed",
    "Permission denied (check OS HID access)",
    "Read failed",
    "Write failed",
    "Feature read failed",
    "Report queue overflow",
    "Duplicate device",
    "Invalid report",
  ];
  return labels[code] ?? `HID error ${code}`;
}

export function descriptorSummary(descriptor: HidDescriptor | null): string {
  if (!descriptor) {
    return "no descriptor loaded";
  }
  if (!descriptor.parsed) {
    return descriptor.error || "descriptor unavailable";
  }
  return `${descriptor.elements.length} elements · ${descriptor.reports.length} reports · ${descriptor.genericLayout.length} controls`;
}
