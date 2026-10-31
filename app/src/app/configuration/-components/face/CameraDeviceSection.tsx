import { useFormContext, useWatch } from "react-hook-form";
import { Label } from "@/components/ui/label";
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select";
import type { BiopassConfig, VideoDeviceInfo } from "@/types/config";
import { DISABLED_OPTION, selectValueFor } from "./selectValue";

const UNAVAILABLE_CAMERA_DEVICE_OPTION = "__unavailable_camera_device__";

interface Props {
  videoDevices: VideoDeviceInfo[];
}

export function CameraDeviceSection({ videoDevices }: Props) {
  const { setValue } = useFormContext<BiopassConfig>();
  const camera = useWatch<BiopassConfig, "methods.face.camera">({
    name: "methods.face.camera",
  });

  const cameraValue = selectValueFor(
    camera || null,
    videoDevices.map((device) => device.path),
    UNAVAILABLE_CAMERA_DEVICE_OPTION,
  );

  return (
    <div className="p-4 rounded-lg bg-muted/50 border border-border/50">
      <div className="grid gap-2">
        <Label
          htmlFor="camera-device"
          className="text-sm font-medium text-muted-foreground"
        >
          Camera Device
        </Label>
        <Select
          value={cameraValue}
          onValueChange={(value) => {
            setValue(
              "methods.face.camera",
              value === DISABLED_OPTION ? null : value,
              {
                shouldDirty: true,
                shouldValidate: true,
              },
            );
          }}
        >
          <SelectTrigger id="camera-device" className="h-10 w-full">
            <SelectValue placeholder="Auto-select (first device)" />
          </SelectTrigger>
          <SelectContent>
            <SelectItem value={DISABLED_OPTION}>
              Auto-select (first device)
            </SelectItem>
            {cameraValue === UNAVAILABLE_CAMERA_DEVICE_OPTION && (
              <SelectItem value={UNAVAILABLE_CAMERA_DEVICE_OPTION} disabled>
                Selected camera unavailable
              </SelectItem>
            )}
            {videoDevices.length > 0 ? (
              videoDevices.map((device) => (
                <SelectItem key={device.path} value={device.path}>
                  {device.display_name}
                </SelectItem>
              ))
            ) : (
              <SelectItem value="__no_devices__" disabled>
                No video devices found
              </SelectItem>
            )}
          </SelectContent>
        </Select>
      </div>
    </div>
  );
}
