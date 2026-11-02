import { ChevronDown, ChevronUp, Plus, Trash2 } from "lucide-react";
import { useState } from "react";
import { useFieldArray, useFormContext, useWatch } from "react-hook-form";
import { Button } from "@/components/ui/button";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog";
import { Label } from "@/components/ui/label";
import type { BiopassConfig, VideoDeviceInfo } from "@/types/config";
import { VideoDeviceSelect } from "./VideoDeviceSelect";

interface Props {
  videoDevices: VideoDeviceInfo[];
}

const OPTIONS = { shouldDirty: true, shouldValidate: true } as const;

/**
 * Ordered camera + IR camera pairs (methods.face.camera_profiles), the only
 * camera setting; face login needs at least one. Auth uses the first whose
 * camera is connected; see pickCameraProfile in auth/core/camera_profile.h.
 */
export function CameraProfilesSection({ videoDevices }: Props) {
  const {
    control,
    formState: { errors },
    setValue,
  } = useFormContext<BiopassConfig>();
  const { fields, append, remove, move } = useFieldArray({
    control,
    name: "methods.face.camera_profiles",
  });
  const profiles = useWatch<BiopassConfig, "methods.face.camera_profiles">({
    name: "methods.face.camera_profiles",
  });

  // Profile index whose "None" IR choice awaits confirmation.
  const [confirmNoIr, setConfirmNoIr] = useState<number | null>(null);
  const profilesError = errors.methods?.face?.camera_profiles;
  const profilesErrorMessage =
    profilesError?.root?.message ?? profilesError?.message;

  return (
    <div className="p-4 rounded-lg bg-muted/50 border border-border/50 space-y-3">
      <div>
        <h4 className="font-medium text-sm">Camera Profiles</h4>
        <p className="text-xs text-muted-foreground">
          The first profile whose camera is connected is used. Add at least one.
        </p>
      </div>

      {fields.map((field, index) => {
        const profile = profiles?.[index];
        const hasIr = Boolean(profile?.ir_camera);
        const cameraError = profilesError?.[index]?.camera?.message;
        const irError = profilesError?.[index]?.ir_camera?.message;
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
                  unavailableLabel="Camera not connected"
                  placeholder="Select camera"
                />
                {cameraError && (
                  <p className="text-xs text-destructive">{cameraError}</p>
                )}
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
                    if (!value) {
                      setConfirmNoIr(index);
                      return;
                    }
                    setValue(
                      `methods.face.camera_profiles.${index}.ir_camera`,
                      value,
                      OPTIONS,
                    );
                    setValue(
                      `methods.face.camera_profiles.${index}.allow_without_ir`,
                      false,
                      OPTIONS,
                    );
                  }}
                  videoDevices={videoDevices}
                  offLabel="None"
                  unavailableLabel="IR camera not connected"
                  placeholder="Select IR camera"
                  unset={!profile?.allow_without_ir}
                />
                {irError && (
                  <p className="text-xs text-destructive">{irError}</p>
                )}
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
            {hasIr && profile?.camera === profile?.ir_camera && (
              <p className="text-xs text-muted-foreground">
                IR camera is also the main camera: enrol and sign in with IR
                only.
              </p>
            )}
          </div>
        );
      })}

      <Dialog
        open={confirmNoIr !== null}
        onOpenChange={(open) => {
          if (!open) setConfirmNoIr(null);
        }}
      >
        <DialogContent>
          <DialogHeader>
            <DialogTitle>Use this profile without IR?</DialogTitle>
            <DialogDescription>
              Face login with profile {(confirmNoIr ?? 0) + 1} won't check the
              IR camera, so a photo or screen may pass. Anyone who plugs in a
              camera matching this profile gets the same.
            </DialogDescription>
          </DialogHeader>
          <DialogFooter>
            <Button
              type="button"
              variant="outline"
              onClick={() => setConfirmNoIr(null)}
            >
              Cancel
            </Button>
            <Button
              type="button"
              variant="destructive"
              onClick={() => {
                if (confirmNoIr !== null) {
                  const base =
                    `methods.face.camera_profiles.${confirmNoIr}` as const;
                  setValue(`${base}.ir_camera`, null, OPTIONS);
                  setValue(`${base}.allow_without_ir`, true, OPTIONS);
                }
                setConfirmNoIr(null);
              }}
            >
              Use without IR
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>

      {profilesErrorMessage && (
        <p className="text-xs text-destructive">{profilesErrorMessage}</p>
      )}

      <Button
        type="button"
        variant="outline"
        size="sm"
        onClick={() =>
          append({ camera: null, ir_camera: null, allow_without_ir: false })
        }
      >
        <Plus />
        Add profile
      </Button>
    </div>
  );
}
