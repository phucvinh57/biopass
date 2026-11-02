import {
  ChevronDown,
  ChevronUp,
  Plus,
  Trash2,
  TriangleAlert,
} from "lucide-react";
import { useFieldArray, useFormContext, useWatch } from "react-hook-form";
import { Button } from "@/components/ui/button";
import { Label } from "@/components/ui/label";
import { Switch } from "@/components/ui/switch";
import type { BiopassConfig, VideoDeviceInfo } from "@/types/config";
import { VideoDeviceSelect } from "./VideoDeviceSelect";

interface Props {
  videoDevices: VideoDeviceInfo[];
}

const OPTIONS = { shouldDirty: true, shouldValidate: true } as const;

/**
 * Ordered camera + IR camera pairs (methods.face.camera_profiles). Auth uses
 * the first whose camera is connected; see pickCameraProfile in
 * auth/core/camera_profile.h.
 */
export function CameraProfilesSection({ videoDevices }: Props) {
  const { control, getValues, setValue } = useFormContext<BiopassConfig>();
  const { fields, append, remove, move } = useFieldArray({
    control,
    name: "methods.face.camera_profiles",
  });
  const profiles = useWatch<BiopassConfig, "methods.face.camera_profiles">({
    name: "methods.face.camera_profiles",
  });

  function addProfile() {
    // The first profile starts as the current single-camera setup, when a
    // camera is set. Its IR camera is copied too; without one, the legacy IR
    // camera still applies as the fallback.
    const [camera, irCamera] = getValues([
      "methods.face.camera",
      "methods.face.anti_spoofing.ir_camera",
    ]);
    const seed = fields.length === 0 && Boolean(camera);
    append({
      camera: seed ? camera : null,
      ir_camera: seed ? irCamera || null : null,
      allow_without_ir: false,
    });
  }

  return (
    <div className="p-4 rounded-lg bg-muted/50 border border-border/50 space-y-3">
      <div>
        <h4 className="font-medium text-sm">Camera Profiles</h4>
        <p className="text-xs text-muted-foreground">
          Tried in order: the first profile whose camera is connected is used. A
          profile without an IR camera uses the fallback IR camera. If no
          profile's camera is connected, the first device is used with the
          fallback IR camera. With no profiles, the Camera Device and IR Camera
          settings are used.
        </p>
      </div>

      {fields.map((field, index) => {
        const profile = profiles?.[index];
        const hasIr = Boolean(profile?.ir_camera);
        const autoSelect = !profile?.camera;
        return (
          <div key={field.id} className="space-y-2">
            <div className="flex items-end gap-2">
              <div className="grid min-w-0 flex-1 gap-1">
                <Label
                  htmlFor={`profile-camera-${index}`}
                  className="text-xs text-muted-foreground"
                >
                  {index + 1}. Camera
                </Label>
                <VideoDeviceSelect
                  id={`profile-camera-${index}`}
                  value={profile?.camera}
                  onChange={(value) =>
                    setValue(
                      `methods.face.camera_profiles.${index}.camera`,
                      value,
                      OPTIONS,
                    )
                  }
                  videoDevices={videoDevices}
                  offLabel="Auto-select (first device)"
                  unavailableLabel="Camera not connected"
                />
              </div>
              <div className="grid min-w-0 flex-1 gap-1">
                <Label
                  htmlFor={`profile-ir-${index}`}
                  className="text-xs text-muted-foreground"
                >
                  IR Camera
                </Label>
                <VideoDeviceSelect
                  id={`profile-ir-${index}`}
                  value={profile?.ir_camera}
                  onChange={(value) => {
                    setValue(
                      `methods.face.camera_profiles.${index}.ir_camera`,
                      value,
                      OPTIONS,
                    );
                    // Only meaningful without an IR camera.
                    if (value) {
                      setValue(
                        `methods.face.camera_profiles.${index}.allow_without_ir`,
                        false,
                        OPTIONS,
                      );
                    }
                  }}
                  videoDevices={videoDevices}
                  offLabel="Fallback IR camera"
                  unavailableLabel="IR camera not connected"
                />
              </div>
              <Button
                type="button"
                variant="ghost"
                size="icon"
                aria-label="Move up"
                disabled={index === 0}
                onClick={() => move(index, index - 1)}
              >
                <ChevronUp />
              </Button>
              <Button
                type="button"
                variant="ghost"
                size="icon"
                aria-label="Move down"
                disabled={index === fields.length - 1}
                onClick={() => move(index, index + 1)}
              >
                <ChevronDown />
              </Button>
              <Button
                type="button"
                variant="ghost"
                size="icon"
                aria-label="Remove profile"
                onClick={() => remove(index)}
              >
                <Trash2 />
              </Button>
            </div>
            <div className="flex items-center gap-2">
              <Switch
                id={`profile-allow-without-ir-${index}`}
                size="sm"
                checked={!hasIr && Boolean(profile?.allow_without_ir)}
                disabled={hasIr}
                onCheckedChange={(checked) =>
                  setValue(
                    `methods.face.camera_profiles.${index}.allow_without_ir`,
                    checked,
                    OPTIONS,
                  )
                }
              />
              <Label
                htmlFor={`profile-allow-without-ir-${index}`}
                className="text-xs text-muted-foreground"
              >
                Allow without IR
              </Label>
            </div>
            {!hasIr && profile?.allow_without_ir && (
              <p className="flex items-center gap-1 text-xs text-destructive">
                <TriangleAlert className="size-3 shrink-0" />
                {autoSelect
                  ? "IR check is skipped whenever this profile is used."
                  : "Anyone who plugs in a device matching this camera skips the IR check."}
              </p>
            )}
            {autoSelect && index < fields.length - 1 && (
              <p className="text-xs text-muted-foreground">
                Profiles below an Auto-select profile are never used.
              </p>
            )}
          </div>
        );
      })}

      <Button type="button" variant="outline" size="sm" onClick={addProfile}>
        <Plus />
        Add profile
      </Button>
    </div>
  );
}
