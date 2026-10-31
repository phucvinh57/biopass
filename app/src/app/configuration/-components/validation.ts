import { toast } from "sonner";
import { z } from "zod";
import { cmd } from "@/commands";
import type { BiopassConfig } from "@/types/config";

const thresholdSchema = z
  .number("Threshold must be a number")
  .min(0, "Threshold must be at least 0%")
  .max(1, "Threshold must be at most 100%");

// Retry, delay and timeout maxima mirror the kMax* clamps in
// auth/core/auth_config.h; the helper clamps anything above them anyway.
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
      retries: z
        .number("Max retries must be a number")
        .int("Max retries must be a whole number")
        .min(1, "Max retries must be at least 1")
        .max(10, "Max retries must be at most 10"),
      retry_delay: z
        .number("Retry delay must be a number")
        .int("Retry delay must be a whole number")
        .min(0, "Retry delay must be at least 0 ms")
        .max(5000, "Retry delay must be at most 5000 ms"),
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
        ir_warmup_delay_ms: z
          .number("IR warmup delay must be a number")
          .int("IR warmup delay must be a whole number")
          .min(0, "IR warmup delay must be at least 0 ms")
          .max(5000, "IR warmup delay must be at most 5000 ms"),
        ir_presence_timeout_ms: z
          .number("IR presence timeout must be a number")
          .int("IR presence timeout must be a whole number")
          .min(0, "IR presence timeout must be at least 0 ms")
          .max(10000, "IR presence timeout must be at most 10000 ms"),
      }),
    }),
    fingerprint: z.object({
      enable: z.boolean(),
      retries: z
        .number("Max retries must be a number")
        .int("Max retries must be a whole number")
        .min(1, "Max retries must be at least 1")
        .max(5, "Max retries must be at most 5"),
      timeout: z
        .number("Timeout must be a number")
        .int("Timeout must be a whole number")
        .min(1, "Timeout must be at least 1 ms")
        .max(10000, "Timeout must be at most 10000 ms"),
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

  const modelIdsToCheck: string[] = [];
  if (config.methods.face.enable) {
    if (config.methods.face.detection.model_id)
      modelIdsToCheck.push(config.methods.face.detection.model_id);
    if (config.methods.face.recognition.model_id)
      modelIdsToCheck.push(config.methods.face.recognition.model_id);
    if (
      config.methods.face.anti_spoofing.enable &&
      config.methods.face.anti_spoofing.model.model_id
    ) {
      modelIdsToCheck.push(config.methods.face.anti_spoofing.model.model_id);
    }
  }
  for (const modelId of modelIdsToCheck) {
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
