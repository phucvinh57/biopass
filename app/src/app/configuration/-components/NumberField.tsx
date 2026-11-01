import { Controller, type FieldPathByValue } from "react-hook-form";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import type { BiopassConfig } from "@/types/config";

/** An empty input becomes NaN so zod reports "must be a number". */
function parseNumberInput(value: string): number {
  return value === "" ? Number.NaN : Number(value);
}

interface Props {
  name: FieldPathByValue<BiopassConfig, number>;
  id: string;
  label: string;
  min: number;
  max: number;
  step?: number;
}

/**
 * A numeric config field. biopassConfigSchema (validation.ts) owns the real
 * limits and messages; `min`/`max`/`step` also feed the browser's own check
 * on submit, so keep them no stricter than the schema.
 */
export function NumberField({ name, id, label, min, max, step }: Props) {
  return (
    <div className="grid gap-2">
      <Label htmlFor={id} className="text-sm font-medium text-muted-foreground">
        {label}
      </Label>
      <Controller<BiopassConfig, typeof name>
        name={name}
        render={({ field, fieldState }) => (
          <>
            <Input
              id={id}
              type="number"
              min={min}
              max={max}
              step={step}
              value={Number.isNaN(field.value) ? "" : field.value}
              onChange={(e) => field.onChange(parseNumberInput(e.target.value))}
              aria-invalid={fieldState.invalid}
              className="h-10"
            />
            {fieldState.error && (
              <p className="text-xs text-destructive">
                {fieldState.error.message}
              </p>
            )}
          </>
        )}
      />
    </div>
  );
}
