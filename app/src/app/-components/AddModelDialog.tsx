import { open as openFileDialog } from "@tauri-apps/plugin-dialog";
import { Loader2, Plus } from "lucide-react";
import { useState } from "react";
import { toast } from "sonner";
import { cmd } from "@/commands";
import { Button } from "@/components/ui/button";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
  DialogTrigger,
} from "@/components/ui/dialog";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs";
import type {
  AntiSpoofArchitecture,
  Model,
  ModelDownloadProgress,
  ModelTask,
} from "@/types/config";
import {
  type ModelSource,
  modelNameFromPath,
  validateAddModelForm,
} from "./addModelForm";

const TASK_OPTIONS: { value: ModelTask; label: string }[] = [
  { value: "detection", label: "Detection" },
  { value: "recognition", label: "Recognition" },
  { value: "anti_spoofing", label: "Anti-Spoofing" },
];

const ARCHITECTURE_OPTIONS: { value: AntiSpoofArchitecture; label: string }[] =
  [
    { value: "mobilenetv3", label: "MobileNetV3" },
    { value: "minifasv2", label: "MiniFASNet V2" },
  ];

interface AddModelDialogProps {
  onAdded: (model: Model) => void;
}

export function AddModelDialog({ onAdded }: AddModelDialogProps) {
  const [open, setOpen] = useState(false);
  const [source, setSource] = useState<ModelSource>("url");
  const [name, setName] = useState("");
  const [task, setTask] = useState<ModelTask>("detection");
  const [architecture, setArchitecture] =
    useState<AntiSpoofArchitecture>("mobilenetv3");
  const [url, setUrl] = useState("");
  const [filePath, setFilePath] = useState("");
  const [submitting, setSubmitting] = useState(false);
  const [progress, setProgress] = useState<ModelDownloadProgress | null>(null);

  function reset() {
    setName("");
    setTask("detection");
    setArchitecture("mobilenetv3");
    setUrl("");
    setFilePath("");
    setSubmitting(false);
    setProgress(null);
  }

  async function handlePickFile() {
    const selected = await openFileDialog({
      multiple: false,
      filters: [{ name: "ONNX model", extensions: ["onnx"] }],
    });
    if (typeof selected === "string") {
      setFilePath(selected);
      if (!name) {
        setName(modelNameFromPath(selected));
      }
    }
  }

  async function handleSubmit() {
    const error = validateAddModelForm({ name, source, url, filePath });
    if (error) {
      toast.error(error);
      return;
    }

    const arch = task === "anti_spoofing" ? architecture : null;
    setSubmitting(true);
    let unlisten: (() => void) | undefined;
    try {
      if (source === "url") {
        unlisten = await cmd.models.onDownloadProgress((p) => setProgress(p));
        const model = await cmd.models.addFromUrl(
          name.trim(),
          task,
          arch,
          url.trim(),
        );
        toast.success("Model added");
        onAdded(model);
      } else {
        const model = await cmd.models.addFromFile(
          name.trim(),
          task,
          arch,
          filePath,
        );
        toast.success("Model added");
        onAdded(model);
      }
      setOpen(false);
      reset();
    } catch (err) {
      toast.error(`Failed to add model: ${err}`);
    } finally {
      unlisten?.();
      setSubmitting(false);
      setProgress(null);
    }
  }

  const progressPct =
    progress?.total && progress.total > 0
      ? Math.min(100, Math.round((progress.downloaded / progress.total) * 100))
      : null;

  return (
    <Dialog
      open={open}
      onOpenChange={(next) => {
        if (!submitting) {
          setOpen(next);
          if (!next) reset();
        }
      }}
    >
      <DialogTrigger asChild>
        <Button type="button">
          <Plus className="w-4 h-4" />
          Add model
        </Button>
      </DialogTrigger>
      <DialogContent>
        <DialogHeader>
          <DialogTitle>Add model</DialogTitle>
          <DialogDescription>
            Add an ONNX model by pasting a download URL or picking a local file.
          </DialogDescription>
        </DialogHeader>

        <div className="flex flex-col gap-4">
          <div className="flex flex-col gap-2">
            <Label htmlFor="model-name">Name</Label>
            <Input
              id="model-name"
              value={name}
              onChange={(e) => setName(e.target.value)}
              placeholder="My custom detector"
              disabled={submitting}
            />
          </div>

          <div className="flex flex-col gap-2">
            <Label>Type</Label>
            <Select
              value={task}
              onValueChange={(v) => setTask(v as ModelTask)}
              disabled={submitting}
            >
              <SelectTrigger>
                <SelectValue />
              </SelectTrigger>
              <SelectContent>
                {TASK_OPTIONS.map((opt) => (
                  <SelectItem key={opt.value} value={opt.value}>
                    {opt.label}
                  </SelectItem>
                ))}
              </SelectContent>
            </Select>
          </div>

          {task === "anti_spoofing" && (
            <div className="flex flex-col gap-2">
              <Label>Architecture</Label>
              <Select
                value={architecture}
                onValueChange={(v) =>
                  setArchitecture(v as AntiSpoofArchitecture)
                }
                disabled={submitting}
              >
                <SelectTrigger>
                  <SelectValue />
                </SelectTrigger>
                <SelectContent>
                  {ARCHITECTURE_OPTIONS.map((opt) => (
                    <SelectItem key={opt.value} value={opt.value}>
                      {opt.label}
                    </SelectItem>
                  ))}
                </SelectContent>
              </Select>
            </div>
          )}

          <Tabs
            value={source}
            onValueChange={(v) => setSource(v as ModelSource)}
          >
            <TabsList className="w-full">
              <TabsTrigger value="url" className="flex-1" disabled={submitting}>
                From URL
              </TabsTrigger>
              <TabsTrigger
                value="file"
                className="flex-1"
                disabled={submitting}
              >
                From file
              </TabsTrigger>
            </TabsList>
            <TabsContent value="url" className="flex flex-col gap-2">
              <Label htmlFor="model-url">Download URL</Label>
              <Input
                id="model-url"
                value={url}
                onChange={(e) => setUrl(e.target.value)}
                placeholder="https://example.com/model.onnx"
                disabled={submitting}
              />
              {progress && (
                <div className="flex flex-col gap-1 mt-1">
                  <div className="h-1.5 w-full rounded-full bg-muted overflow-hidden">
                    <div
                      className="h-full bg-primary transition-all"
                      style={{
                        width:
                          progressPct !== null ? `${progressPct}%` : "100%",
                      }}
                    />
                  </div>
                  <p className="text-xs text-muted-foreground">
                    {progressPct !== null
                      ? `${progressPct}%`
                      : `${(progress.downloaded / 1_000_000).toFixed(1)} MB`}
                  </p>
                </div>
              )}
            </TabsContent>
            <TabsContent value="file" className="flex flex-col gap-2">
              <Label>Local file</Label>
              <div className="flex gap-2">
                <Input
                  value={filePath}
                  readOnly
                  placeholder="No file selected"
                />
                <Button
                  type="button"
                  variant="outline"
                  onClick={handlePickFile}
                  disabled={submitting}
                >
                  Browse
                </Button>
              </div>
            </TabsContent>
          </Tabs>
        </div>

        <DialogFooter>
          <Button type="button" onClick={handleSubmit} disabled={submitting}>
            {submitting && <Loader2 className="w-4 h-4 mr-2 animate-spin" />}
            {submitting ? "Adding..." : "Add model"}
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}
