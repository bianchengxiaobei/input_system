import { describe, expect, it } from "vitest";

import { bytesToHex, parseHexBytes, reportRate, descriptorSummary, type HidDescriptor, type HidMonitorReport } from "./hid";

describe("HID helpers", () => {
  it("parses hex bytes with separators and 0x prefixes", () => {
    expect(parseHexBytes("05 01 0x30,ff")).toEqual({
      bytes: [0x05, 0x01, 0x30, 0xff],
      error: "",
    });
    expect(parseHexBytes("abc").error).not.toBe("");
    expect(parseHexBytes("zz").error).not.toBe("");
  });

  it("formats bytes as lower-case hex", () => {
    expect(bytesToHex([0, 5, 255])).toBe("00 05 ff");
  });

  it("calculates report rate from timestamps", () => {
    const reports: HidMonitorReport[] = [
      { sequence: 1, type: "input", time: 1.0, data: [] },
      { sequence: 2, type: "input", time: 1.01, data: [] },
      { sequence: 3, type: "input", time: 1.02, data: [] },
    ];
    expect(reportRate(reports)).toBeCloseTo(100, 0);
    expect(reportRate(reports.slice(0, 1))).toBe(0);
  });

  it("summarizes parsed and unparsed descriptors", () => {
    expect(descriptorSummary(null)).toBe("no descriptor loaded");
    const descriptor = {
      parsed: true,
      raw: [],
      elements: [],
      reports: [],
      collections: [],
      genericLayout: [],
    } as HidDescriptor;
    expect(descriptorSummary(descriptor)).toContain("0 elements");
  });
});
