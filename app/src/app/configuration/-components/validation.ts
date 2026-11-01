import { toast } from "sonner";
import { z } from "zod";
import { CONFIG_LIMITS } from "@/bindings";
import { cmd } from "@/commands";
import { referencedModelIds } from "@/lib/models";
import type { BiopassConfig } from "@/types/config";

// Ranges come from CONFIG_LIMITS in app/src-tauri/src/config.rs, which the
// config_golden ctest checks against auth/core/auth_config.h. The helper
// clamps retries, delays and timeouts above the maxima, and turns face off
// for a threshold outside its range.
const limits = CONFIG_LIMITS;

const thresholdSchema = z
  .number("Threshold must be a number")
  .min(
    limits.threshold.min,
    `Threshold must be at least ${limits.threshold.min * 100}%`,
  )
  .max(
    limits.threshold.max,
    `Threshold must be at most ${limits.threshold.max * 100}%`,
  );

// A whole number in `range`; `unit` follows the bounds in messages.
function intSchema(
  label: string,
  range: { readonly min: number; readonly max: number },
  unit = "",
) {
  return z
    .number(`${label} must be a number`)
    .int(`${label} must be a whole number`)
    .min(range.min, `${label} must be at least ${range.min}${unit}`)
    .max(range.max, `${label} must be at most ${range.max}${unit}`);
}

export const biopassConfigSchema = z.object({
  schema_version: z.number(),
  strategy: z.object({
    debug: z.boolean(),
    execution_mode: z.enum(["sequential", "parallel"]),
    order: z.array(z.string()),
    ignore_services: z.array(z.string()),
  }),
  methods: z.object({
    face: z.object({
      enable: z.boolean(),
      camera: z.string().nullable(),
      retries: intSchema("Max retries", limits.face_retries),
      retry_delay: intSchema("Retry delay", limits.retry_delay_ms, " ms"),
      detection: z.object({
        model_id: z.string(),
        threshold: thresholdSchema,
      }),
      recognition: z.object({
        model_id: z.string(),
        threshold: thresholdSchema,
      }),
      anti_spoofing: z.object({
        enable: z.boolean(),
        model: z.object({
          model_id: z.string(),
          threshold: thresholdSchema,
        }),
        ir_camera: z.string().nullable(),
        ir_warmup_delay_ms: intSchema(
          "IR warmup delay",
          limits.ir_warmup_delay_ms,
          " ms",
        ),
        ir_presence_timeout_ms: intSchema(
          "IR presence timeout",
          limits.ir_presence_timeout_ms,
          " ms",
        ),
      }),
    }),
    fingerprint: z.object({
      enable: z.boolean(),
      retries: intSchema("Max retries", limits.fingerprint_retries),
      timeout: intSchema("Timeout", limits.fingerprint_timeout_ms, " ms"),
    }),
  }),
  appearance: z.string(),
});

// Model checks need the live SQLite registry, so they run as a separate
// async pass instead of in the zod schema's synchronous superRefine.
export async function validateConfig(config: BiopassConfig): Promise<boolean> {
  const models = await cmd.models.list();
  const modelsById = new Map(models.map((m) => [m.id, m]));

  if (config.methods.face.enable) {
    if (
      !config.methods.face.detection.model_id ||
      !modelsById.has(config.methods.face.detection.model_id)
    ) {
      toast.error("Valid Face Detection model is required");
      return false;
    }
    if (
      !config.methods.face.recognition.model_id ||
      !modelsById.has(config.methods.face.recognition.model_id)
    ) {
      toast.error("Valid Face Recognition model is required");
      return false;
    }
    if (
      config.methods.face.anti_spoofing.enable &&
      (!config.methods.face.anti_spoofing.model.model_id ||
        !modelsById.has(config.methods.face.anti_spoofing.model.model_id))
    ) {
      toast.error("Valid Anti-Spoofing model is required when enabled");
      return false;
    }

    try {
      const samples = await cmd.face.listImages();
      if (samples.length === 0) {
        toast.error(
          "At least one face sample must be captured before enabling Face method",
        );
        return false;
      }
    } catch (err) {
      console.error("Failed to check face samples:", err);
    }
  }

  // Only the models authentication would load: a missing file under a
  // disabled method must not block saving.
  for (const modelId of referencedModelIds(config, {
    includeDisabled: false,
  })) {
    const model = modelsById.get(modelId);
    if (!model) continue;
    try {
      const exists = await cmd.file.exists(model.path);
      if (!exists) {
        toast.error(
          `Model file not found: ${model.name}. Please check AI Models.`,
        );
        return false;
      }
    } catch (err) {
      console.error(`Failed to check model file at ${model.path}:`, err);
      toast.error(
        err instanceof Error
          ? err.message
          : "Unknown error occurred while validating models",
      );
      return false;
    }
  }

  return true;
}
