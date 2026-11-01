import { createFileRoute } from "@tanstack/react-router";
import { revealItemInDir } from "@tauri-apps/plugin-opener";
import { Cpu, Trash2 } from "lucide-react";
import { toast } from "sonner";
import { cmd } from "@/commands";
import { Button } from "@/components/ui/button";
import {
  Tooltip,
  TooltipContent,
  TooltipProvider,
  TooltipTrigger,
} from "@/components/ui/tooltip";
import type { Model } from "@/types/config";
import { AddModelDialog } from "./-components/AddModelDialog";
import { ModelStatus, type ModelStatusType } from "./-components/ModelStatus";
import { RenameModelDialog } from "./-components/RenameModelDialog";
import { useModelStatuses } from "./-components/useModelStatuses";

interface ModelCardProps {
  model: Model;
  status: ModelStatusType;
  onRenamed: (model: Model) => void;
  onDelete: (model: Model) => void;
}

function ModelFileFolderButton({ path }: { path: string }) {
  const handleOpenFileFolder = async (path: string) => {
    try {
      await revealItemInDir(path);
    } catch (err) {
      console.error("Failed to open file location:", err);
      toast.error(`Failed to open file location: ${err}`);
    }
  };
  return (
    <TooltipProvider>
      <Tooltip delayDuration={300}>
        <TooltipTrigger asChild>
          <button
            type="button"
            onClick={() => handleOpenFileFolder(path)}
            className="text-[10px] font-mono text-muted-foreground opacity-60 truncate max-w-37.5 bg-muted/50 px-1.5 py-0.5 rounded hover:opacity-100 hover:bg-primary/10 hover:text-primary transition-all cursor-pointer"
          >
            {path.split(/[/]/).pop()}
          </button>
        </TooltipTrigger>
        <TooltipContent
          side="bottom"
          align="end"
          className="max-w-75 break-all"
        >
          <p className="font-mono text-xs">{path}</p>
        </TooltipContent>
      </Tooltip>
    </TooltipProvider>
  );
}

function ModelCard({ model, status, onRenamed, onDelete }: ModelCardProps) {
  const isDefault = model.source === "builtin";
  const deleteDisabled = status === "inuse" || status === "checking";
  const deleteDisabledReason =
    status === "inuse" ? "Model is currently in use" : undefined;

  return (
    <div className="group relative flex flex-col gap-4 p-5 rounded-xl border border-border bg-linear-to-b from-card to-muted/20 shadow-sm hover:border-primary/30 hover:shadow-md transition-all duration-300">
      <div className="flex sm:flex-row sm:items-start justify-between gap-4">
        <div className="flex items-center gap-3">
          <div className="w-10 h-10 rounded-lg bg-linear-to-br from-blue-500/10 to-indigo-500/10 flex items-center justify-center border border-blue-500/10 group-hover:border-blue-500/30 transition-colors">
            <Cpu className="w-5 h-5 text-blue-600 dark:text-blue-400" />
          </div>
          <div>
            <div className="flex items-center gap-4">
              <h3
                className="font-semibold leading-none truncate max-w-100"
                title={model.name}
              >
                {model.name}
              </h3>
              <p className="text-xs text-muted-foreground capitalize mt-1 block">
                {model.task.replace(/_/g, " ")}
                {isDefault ? " · Default" : ""}
              </p>
            </div>
            <ModelFileFolderButton path={model.path} />
          </div>
        </div>

        <div className="flex items-center gap-1">
          <ModelStatus status={status} />
          <RenameModelDialog model={model} onRenamed={onRenamed} />
          {!isDefault && (
            <TooltipProvider>
              <Tooltip delayDuration={300}>
                <TooltipTrigger asChild>
                  <span>
                    <Button
                      type="button"
                      variant="ghost"
                      size="icon"
                      disabled={deleteDisabled}
                      onClick={() => onDelete(model)}
                      className="text-destructive hover:text-destructive"
                    >
                      <Trash2 className="w-4 h-4" />
                    </Button>
                  </span>
                </TooltipTrigger>
                {deleteDisabledReason && (
                  <TooltipContent side="bottom">
                    <p className="text-xs">{deleteDisabledReason}</p>
                  </TooltipContent>
                )}
              </Tooltip>
            </TooltipProvider>
          )}
        </div>
      </div>
    </div>
  );
}

function ModelsRouteComponent() {
  const { models, setModels, loading, statusOf } = useModelStatuses();

  function handleModelUpdated(updated: Model) {
    setModels((prev) => prev.map((m) => (m.id === updated.id ? updated : m)));
  }

  function handleModelAdded(added: Model) {
    setModels((prev) => [...prev, added]);
  }

  async function handleDelete(model: Model) {
    try {
      await cmd.models.remove(model.id);
      toast.success("Model deleted");
      setModels((prev) => prev.filter((m) => m.id !== model.id));
    } catch (err) {
      toast.error(`Failed to delete model: ${err}`);
    }
  }

  if (loading) {
    return (
      <div className="flex items-center justify-center p-8">
        <div className="animate-spin rounded-full h-8 w-8 border-b-2 border-primary" />
      </div>
    );
  }

  return (
    <div className="flex flex-col gap-6 w-full max-width-4xl mx-auto p-6">
      <div className="flex justify-between items-center">
        <div>
          <h1 className="text-3xl font-bold bg-linear-to-r from-primary to-purple-500 bg-clip-text text-transparent">
            AI Model Management
          </h1>
          <p className="text-sm text-muted-foreground mt-1">
            View, add, rename, and remove AI models used for authentication.
          </p>
        </div>
        <AddModelDialog onAdded={handleModelAdded} />
      </div>

      <div className="flex flex-col gap-4">
        {models.length === 0 ? (
          <div className="col-span-full flex flex-col items-center justify-center p-12 text-center rounded-xl border border-dashed border-border/50 bg-card/50">
            <div className="w-12 h-12 rounded-full bg-muted flex items-center justify-center mb-4">
              <Cpu className="w-6 h-6 text-muted-foreground" />
            </div>
            <h3 className="font-semibold text-lg">No models registered</h3>
          </div>
        ) : (
          models.map((model) => (
            <ModelCard
              key={model.id}
              model={model}
              status={statusOf(model)}
              onRenamed={handleModelUpdated}
              onDelete={handleDelete}
            />
          ))
        )}
      </div>
    </div>
  );
}

export const Route = createFileRoute("/models")({
  component: ModelsRouteComponent,
});
