import { Info } from "lucide-react";
import { useFormContext, useWatch } from "react-hook-form";
import { Button } from "@/components/ui/button";
import type { BiopassConfig, VideoDeviceInfo } from "@/types/config";
import { stablePathUpdates } from "./selectValue";

interface Props {
  videoDevices: VideoDeviceInfo[];
}

/**
 * Lists camera settings saved as /dev/videoN and offers to switch them to
 * stable paths. Never applied silently: the number may now belong to
 * another device (e.g. a dock), so the user checks the mapping and saves.
 */
export function StablePathNotice({ videoDevices }: Props) {
  const { setValue } = useFormContext<BiopassConfig>();
  const face = useWatch<BiopassConfig, "methods.face">({
    name: "methods.face",
  });
  const updates = face ? stablePathUpdates(face, videoDevices) : [];
  if (updates.length === 0) return null;

  return (
    <div className="p-4 rounded-lg border border-border/50 bg-muted/50 space-y-2 text-sm">
      <p className="flex items-center gap-2 font-medium">
        <Info className="w-4 h-4 shrink-0" />
        Camera settings use /dev/videoN numbers, which can change
      </p>
      <ul className="space-y-1 text-xs text-muted-foreground">
        {updates.map((update) => (
          <li key={update.name} className="break-all">
            {update.label}: current {update.from} → {update.deviceName} (
            {update.value})
          </li>
        ))}
      </ul>
      <p className="text-xs text-muted-foreground">
        Check each device is the one you meant before saving.
      </p>
      <Button
        type="button"
        variant="outline"
        size="sm"
        onClick={() => {
          for (const { name, value } of updates) {
            setValue(name, value, { shouldDirty: true, shouldValidate: true });
          }
        }}
      >
        Use stable paths
      </Button>
    </div>
  );
}
