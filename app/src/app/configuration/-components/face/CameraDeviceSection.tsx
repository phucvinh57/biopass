import { useFormContext, useWatch } from "react-hook-form";
import { Label } from "@/components/ui/label";
import type { BiopassConfig, VideoDeviceInfo } from "@/types/config";
import { VideoDeviceSelect } from "./VideoDeviceSelect";

interface Props {
  videoDevices: VideoDeviceInfo[];
}

export function CameraDeviceSection({ videoDevices }: Props) {
  const { setValue } = useFormContext<BiopassConfig>();
  const camera = useWatch<BiopassConfig, "methods.face.camera">({
    name: "methods.face.camera",
  });
  const profiles = useWatch<BiopassConfig, "methods.face.camera_profiles">({
    name: "methods.face.camera_profiles",
  });
  const overridden = (profiles?.length ?? 0) > 0;

  return (
    <div className="p-4 rounded-lg bg-muted/50 border border-border/50">
      <div className="grid gap-2">
        <Label
          htmlFor="camera-device"
          className="text-sm font-medium text-muted-foreground"
        >
          Camera Device
        </Label>
        <VideoDeviceSelect
          id="camera-device"
          value={camera}
          onChange={(value) =>
            setValue("methods.face.camera", value, {
              shouldDirty: true,
              shouldValidate: true,
            })
          }
          videoDevices={videoDevices}
          offLabel="Auto-select (first device)"
          unavailableLabel="Selected camera unavailable"
          disabled={overridden}
        />
        {overridden && (
          <p className="text-xs text-muted-foreground">
            Camera profiles below override this setting.
          </p>
        )}
      </div>
    </div>
  );
}
