import { useFormContext, useWatch } from "react-hook-form";
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
import type { BiopassConfig, Model, VideoDeviceInfo } from "@/types/config";
import { Threshold } from "../methods/shared/Threshold";
import { DISABLED_OPTION, selectValueFor } from "./selectValue";

const UNAVAILABLE_AI_MODEL_OPTION = "__unavailable_ai_model__";
const UNAVAILABLE_IR_DEVICE_OPTION = "__unavailable_ir_device__";

interface Props {
  /** Memoized: useModelFileStatus rechecks files when its identity changes. */
  antiSpoofModels: Model[];
  videoDevices: VideoDeviceInfo[];
}

/** AI anti-spoofing model, its threshold, and the IR camera. */
export function AntiSpoofingSection({ antiSpoofModels, videoDevices }: Props) {
  const {
    formState: { errors },
    setValue,
  } = useFormContext<BiopassConfig>();
  const antiSpoofing = useWatch<BiopassConfig, "methods.face.anti_spoofing">({
    name: "methods.face.anti_spoofing",
  });
  const antiSpoofStatusMap = useModelFileStatus(antiSpoofModels);

  const aiModelValue = selectValueFor(
    antiSpoofing.enable ? antiSpoofing.model.model_id : null,
    antiSpoofModels.map((model) => model.id),
    UNAVAILABLE_AI_MODEL_OPTION,
  );
  const irCameraValue = selectValueFor(
    antiSpoofing.ir_camera || null,
    videoDevices.map((device) => device.path),
    UNAVAILABLE_IR_DEVICE_OPTION,
  );

  return (
    <div className="p-4 rounded-lg bg-muted/50 border border-border/50 space-y-3">
      <h4 className="font-medium text-sm">Anti-Spoofing</h4>
      <div className="grid gap-2">
        <Label
          htmlFor="anti-spoofing-method"
          className="text-xs text-muted-foreground"
        >
          AI Model
        </Label>
        <Select
          value={aiModelValue}
          onValueChange={(value) => {
            if (value === DISABLED_OPTION) {
              setValue("methods.face.anti_spoofing.enable", false, {
                shouldDirty: true,
                shouldValidate: true,
              });
              return;
            }

            setValue("methods.face.anti_spoofing.enable", true, {
              shouldDirty: true,
              shouldValidate: true,
            });
            setValue("methods.face.anti_spoofing.model.model_id", value, {
              shouldDirty: true,
              shouldValidate: true,
            });
          }}
        >
          <SelectTrigger id="anti-spoofing-method" className="h-10 w-full">
            <SelectValue placeholder="Select AI anti-spoofing method" />
          </SelectTrigger>
          <SelectContent>
            <SelectItem value={DISABLED_OPTION}>Disable</SelectItem>
            {aiModelValue === UNAVAILABLE_AI_MODEL_OPTION && (
              <SelectItem value={UNAVAILABLE_AI_MODEL_OPTION} disabled>
                Selected anti-spoofing model unavailable
              </SelectItem>
            )}
            {antiSpoofModels.length > 0 ? (
              antiSpoofModels.map((model) => (
                <SelectItem key={model.id} value={model.id}>
                  <div className="flex items-center gap-3 pr-6">
                    <span className="truncate">{model.name}</span>
                    <ModelStatus
                      status={antiSpoofStatusMap[model.id]}
                      size="sm"
                      className="h-4"
                    />
                  </div>
                </SelectItem>
              ))
            ) : (
              <SelectItem value="__no_ai_models__" disabled>
                No anti-spoofing models available
              </SelectItem>
            )}
          </SelectContent>
        </Select>
        {errors.methods?.face?.anti_spoofing?.model?.model_id && (
          <p className="text-xs text-destructive">
            {errors.methods.face.anti_spoofing.model.model_id.message}
          </p>
        )}
      </div>

      {antiSpoofing.enable && (
        <div className="w-48">
          <Threshold
            label="Threshold"
            value={antiSpoofing.model.threshold}
            onChange={(threshold) =>
              setValue(
                "methods.face.anti_spoofing.model.threshold",
                threshold,
                {
                  shouldDirty: true,
                  shouldValidate: true,
                },
              )
            }
          />
        </div>
      )}

      <div className="grid gap-2">
        <Label htmlFor="ir-device" className="text-xs text-muted-foreground">
          IR Camera
        </Label>
        <Select
          value={irCameraValue}
          onValueChange={(value) => {
            setValue(
              "methods.face.anti_spoofing.ir_camera",
              value === DISABLED_OPTION ? null : value,
              {
                shouldDirty: true,
                shouldValidate: true,
              },
            );
          }}
        >
          <SelectTrigger id="ir-device" className="h-10 w-full">
            <SelectValue placeholder="Select IR Camera Device" />
          </SelectTrigger>
          <SelectContent>
            <SelectItem value={DISABLED_OPTION}>Disable</SelectItem>
            {irCameraValue === UNAVAILABLE_IR_DEVICE_OPTION && (
              <SelectItem value={UNAVAILABLE_IR_DEVICE_OPTION} disabled>
                Selected IR camera unavailable
              </SelectItem>
            )}
            {videoDevices.length > 0 ? (
              videoDevices.map((device) => (
                <SelectItem key={device.path} value={device.path}>
                  {device.display_name}
                </SelectItem>
              ))
            ) : (
              <SelectItem value="__no_ir_devices__" disabled>
                No video devices found
              </SelectItem>
            )}
          </SelectContent>
        </Select>
      </div>
    </div>
  );
}
