export type ModelSource = "url" | "file";

export interface AddModelForm {
  name: string;
  source: ModelSource;
  url: string;
  filePath: string;
}

/** The first problem with the form, as a user-facing message, or null. */
export function validateAddModelForm(form: AddModelForm): string | null {
  if (!form.name.trim()) return "Please enter a model name";
  if (form.source === "url" && !form.url.trim())
    return "Please enter a download URL";
  if (form.source === "file" && !form.filePath)
    return "Please pick a .onnx file";
  return null;
}

/** Default model name for a picked file: its basename without `.onnx`. */
export function modelNameFromPath(path: string): string {
  const base = path.split(/[/\\]/).pop() || path;
  return base.replace(/\.onnx$/i, "");
}
