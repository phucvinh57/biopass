import { describe, expect, test } from "bun:test";
import { areServicesEqual, parseIgnoredServicesInput } from "./ignoredServices";

describe("parseIgnoredServicesInput", () => {
  test("splits on commas and trims each name", () => {
    expect(parseIgnoredServicesInput(" sudo ,polkit-1,\tlogin\n")).toEqual([
      "sudo",
      "polkit-1",
      "login",
    ]);
  });

  test("drops empty entries", () => {
    expect(parseIgnoredServicesInput("")).toEqual([]);
    expect(parseIgnoredServicesInput(" , ,, ")).toEqual([]);
    expect(parseIgnoredServicesInput(",sudo,")).toEqual(["sudo"]);
  });

  test("keeps the first of repeated names, case-sensitively", () => {
    expect(parseIgnoredServicesInput("sudo, login, sudo ,login")).toEqual([
      "sudo",
      "login",
    ]);
    expect(parseIgnoredServicesInput("Sudo,sudo")).toEqual(["Sudo", "sudo"]);
  });

  test("only commas separate: inner spaces stay", () => {
    expect(parseIgnoredServicesInput("gdm password; sudo")).toEqual([
      "gdm password; sudo",
    ]);
  });
});

describe("areServicesEqual", () => {
  test("same names in the same order", () => {
    expect(areServicesEqual([], [])).toBe(true);
    expect(areServicesEqual(["sudo", "login"], ["sudo", "login"])).toBe(true);
  });

  test("order and length matter", () => {
    expect(areServicesEqual(["sudo", "login"], ["login", "sudo"])).toBe(false);
    expect(areServicesEqual(["sudo"], ["sudo", "login"])).toBe(false);
    expect(areServicesEqual(["sudo", "login"], ["sudo"])).toBe(false);
  });
});
