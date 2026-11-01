import { describe, expect, test } from "bun:test";
import {
  type AddModelForm,
  modelNameFromPath,
  validateAddModelForm,
} from "./addModelForm";

const fromUrl: AddModelForm = {
  name: "My model",
  source: "url",
  url: "https://example.com/m.onnx",
  filePath: "",
};
const fromFile: AddModelForm = {
  name: "My model",
  source: "file",
  url: "",
  filePath: "/home/u/m.onnx",
};

describe("validateAddModelForm", () => {
  test("a filled form is valid", () => {
    expect(validateAddModelForm(fromUrl)).toBeNull();
    expect(validateAddModelForm(fromFile)).toBeNull();
  });

  test("a blank name comes first", () => {
    for (const name of ["", "  \t"]) {
      expect(validateAddModelForm({ ...fromUrl, name, url: "" })).toBe(
        "Please enter a model name",
      );
      expect(validateAddModelForm({ ...fromFile, name, filePath: "" })).toBe(
        "Please enter a model name",
      );
    }
  });

  test("the URL source needs a non-blank URL; the file path is ignored", () => {
    for (const url of ["", "   "]) {
      expect(validateAddModelForm({ ...fromUrl, url })).toBe(
        "Please enter a download URL",
      );
    }
    expect(validateAddModelForm({ ...fromUrl, filePath: "" })).toBeNull();
  });

  test("the file source needs a picked file; the URL is ignored", () => {
    expect(validateAddModelForm({ ...fromFile, filePath: "" })).toBe(
      "Please pick a .onnx file",
    );
    expect(validateAddModelForm({ ...fromFile, url: "" })).toBeNull();
    // The path comes from the file picker and is not trimmed.
    expect(validateAddModelForm({ ...fromFile, filePath: " " })).toBeNull();
  });
});

describe("modelNameFromPath", () => {
  test("basename without a trailing .onnx, any case", () => {
    expect(modelNameFromPath("/home/u/models/yolov8n-face.onnx")).toBe(
      "yolov8n-face",
    );
    expect(modelNameFromPath("C:\\models\\Edge.ONNX")).toBe("Edge");
    expect(modelNameFromPath("m.onnx")).toBe("m");
  });

  test("only the last .onnx is removed; other extensions stay", () => {
    expect(modelNameFromPath("/m/a.onnx.onnx")).toBe("a.onnx");
    expect(modelNameFromPath("/m/a.onnx.bak")).toBe("a.onnx.bak");
    expect(modelNameFromPath("/m/model.bin")).toBe("model.bin");
  });

  test("a path ending in a separator falls back to the whole path", () => {
    expect(modelNameFromPath("/m/dir/")).toBe("/m/dir/");
    expect(modelNameFromPath("")).toBe("");
  });

  test("a bare .onnx file name gives an empty name", () => {
    expect(modelNameFromPath("/m/.onnx")).toBe("");
  });
});
