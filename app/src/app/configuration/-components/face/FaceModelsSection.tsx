import { useFormContext, useWatch } from "react-hook-form";
import type { BiopassConfig, Model } from "@/types/config";
import { ModelSelect } from "../methods/shared/ModelSelect";
import { Threshold } from "../methods/shared/Threshold";

interface Props {
  detectionModels: Model[];
  recognitionModels: Model[];
}

/** Detection and recognition model + threshold pickers. */
export function FaceModelsSection({
  detectionModels,
  recognitionModels,
}: Props) {
  const {
    formState: { errors },
    setValue,
  } = useFormContext<BiopassConfig>();
  const config = useWatch<BiopassConfig, "methods.face">({
    name: "methods.face",
  });

  return (
    <div className="p-4 rounded-lg bg-muted/50 border border-border/50">
      <h4 className="font-medium mb-3 text-sm">Detection</h4>
      <div className="flex gap-6 items-end">
        <div className="flex-1 min-w-0">
          <ModelSelect
            label="Model"
            value={config.detection.model_id}
            models={detectionModels}
            error={config.enable || !!errors.methods?.face?.detection?.model_id}
            errorMessage={errors.methods?.face?.detection?.model_id?.message}
            onChange={(modelId) =>
              setValue("methods.face.detection.model_id", modelId, {
                shouldDirty: true,
                shouldValidate: true,
              })
            }
          />
        </div>
        <div className="w-48 shrink-0">
          <Threshold
            label="Threshold"
            value={config.detection.threshold}
            onChange={(threshold) =>
              setValue("methods.face.detection.threshold", threshold, {
                shouldDirty: true,
                shouldValidate: true,
              })
            }
          />
        </div>
      </div>
      <h4 className="font-medium my-3 text-sm">Recognition</h4>
      <div className="flex gap-6 items-end">
        <div className="flex-1 min-w-0">
          <ModelSelect
            label="Model"
            value={config.recognition.model_id}
            models={recognitionModels}
            error={
              config.enable || !!errors.methods?.face?.recognition?.model_id
            }
            errorMessage={errors.methods?.face?.recognition?.model_id?.message}
            onChange={(modelId) =>
              setValue("methods.face.recognition.model_id", modelId, {
                shouldDirty: true,
                shouldValidate: true,
              })
            }
          />
        </div>
        <div className="w-48 shrink-0">
          <Threshold
            label="Threshold"
            value={config.recognition.threshold}
            onChange={(threshold) =>
              setValue("methods.face.recognition.threshold", threshold, {
                shouldDirty: true,
                shouldValidate: true,
              })
            }
          />
        </div>
      </div>
    </div>
  );
}
