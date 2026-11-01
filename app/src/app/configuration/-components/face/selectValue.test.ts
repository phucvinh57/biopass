import { describe, expect, test } from "bun:test";
import { DISABLED_OPTION, selectValueFor } from "./selectValue";

const UNAVAILABLE = "__unavailable__";
const options = ["/dev/video0", "/dev/video2"];

describe("selectValueFor", () => {
  test("null (setting off) selects the disabled option", () => {
    expect(selectValueFor(null, options, UNAVAILABLE)).toBe(DISABLED_OPTION);
    expect(selectValueFor(null, [], UNAVAILABLE)).toBe(DISABLED_OPTION);
  });

  test("a value among the options selects itself", () => {
    expect(selectValueFor("/dev/video2", options, UNAVAILABLE)).toBe(
      "/dev/video2",
    );
  });

  test("a value missing from the options shows as unavailable", () => {
    expect(selectValueFor("/dev/video9", options, UNAVAILABLE)).toBe(
      UNAVAILABLE,
    );
    expect(selectValueFor("", options, UNAVAILABLE)).toBe(UNAVAILABLE);
    expect(selectValueFor(DISABLED_OPTION, options, UNAVAILABLE)).toBe(
      UNAVAILABLE,
    );
  });
});
