import { ModelStatus } from "@/app/-components/ModelStatus";
import { Label } from "@/components/ui/label";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import { useModelFileStatus } from "@/hooks/useModelFileStatus";
import { cn } from "@/lib/utils";
import type { Model } from "@/types/config";

interface Props {
  label: string;
  value: string;
  models: Model[];
  error: boolean;
  errorMessage?: string;
  onChange: (modelId: string) => void;
}

export function ModelSelect({
  label,
  value,
  models,
  error,
  errorMessage,
  onChange,
}: Props) {
  const selectedModel = models.find((m) => m.id === value);
  const statusMap = useModelFileStatus(models);

  const isValid = selectedModel && statusMap[selectedModel.id];

  return (
    <div className="grid gap-2">
      <div className="flex items-center justify-between">
        <Label className="text-xs text-muted-foreground">{label}</Label>
      </div>
      <Select value={value} onValueChange={onChange}>
        <SelectTrigger
          className={cn(
            "h-9 transition-colors text-sm",
            error && !isValid && "border-destructive ring-destructive/20",
          )}
        >
          <SelectValue placeholder="Select a model" />
        </SelectTrigger>
        <SelectContent>
          {models.length > 0 ? (
            models.map((model) => (
              <SelectItem key={model.id} value={model.id}>
                <div className="flex items-center gap-3 pr-6">
                  <span className="truncate">{model.name}</span>
                  <ModelStatus
                    status={statusMap[model.id]}
                    size="sm"
                    className="h-4"
                  />
                </div>
              </SelectItem>
            ))
          ) : (
            <SelectItem value="none" disabled>
              No models available
            </SelectItem>
          )}
        </SelectContent>
      </Select>
      {errorMessage && (
        <p className="text-xs text-destructive">{errorMessage}</p>
      )}
    </div>
  );
}
