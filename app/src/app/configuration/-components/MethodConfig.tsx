import { Fingerprint, ScanFace, ShieldCheck } from "lucide-react";
import { useState } from "react";
import { useFormContext, useWatch } from "react-hook-form";
import type { BiopassConfig, MethodKind } from "@/types/config";
import { FingerprintSetting } from "./FingerprintSetting";
import { FaceSetting } from "./face/FaceSetting";
import { MethodCard } from "./MethodCard";
import { NumberField } from "./NumberField";

export function MethodConfig() {
  const { setValue } = useFormContext<BiopassConfig>();
  const faceConfig = useWatch<BiopassConfig, "methods.face">({
    name: "methods.face",
  });
  const fingerprintConfig = useWatch<BiopassConfig, "methods.fingerprint">({
    name: "methods.fingerprint",
  });
  const [expandedMethod, setExpandedMethod] = useState<MethodKind | null>(
    "face",
  );

  const methodIcons: Record<MethodKind, React.ReactNode> = {
    face: <ScanFace className="w-5 h-5 text-white" />,
    fingerprint: <Fingerprint className="w-5 h-5 text-white" />,
  };

  const methodColors: Record<MethodKind, string> = {
    face: "from-violet-500 to-purple-500",
    fingerprint: "from-emerald-500 to-teal-500",
  };

  return (
    <div className="rounded-xl border border-border/50 bg-card/50 backdrop-blur-sm p-6 shadow-lg">
      <h2 className="text-2xl sm:text-3xl font-bold tracking-tight mb-6 flex items-center gap-3">
        <span className="w-10 h-10 rounded-xl bg-linear-to-br from-purple-500 to-pink-500 flex items-center justify-center shadow-sm">
          <ShieldCheck className="w-5 h-5 text-white" />
        </span>
        Authentication Methods
      </h2>

      <div className="grid gap-4">
        <MethodCard
          title="Face Recognition"
          icon={methodIcons.face}
          color={methodColors.face}
          enabled={faceConfig.enable}
          onToggle={(enable) =>
            setValue("methods.face.enable", enable, {
              shouldDirty: true,
              shouldValidate: true,
            })
          }
          expanded={expandedMethod === "face"}
          onExpand={() =>
            setExpandedMethod(expandedMethod === "face" ? null : "face")
          }
        >
          <FaceSetting />
        </MethodCard>

        <MethodCard
          title="Fingerprint"
          icon={methodIcons.fingerprint}
          color={methodColors.fingerprint}
          enabled={fingerprintConfig.enable}
          onToggle={(enable) =>
            setValue("methods.fingerprint.enable", enable, {
              shouldDirty: true,
              shouldValidate: true,
            })
          }
          expanded={expandedMethod === "fingerprint"}
          onExpand={() =>
            setExpandedMethod(
              expandedMethod === "fingerprint" ? null : "fingerprint",
            )
          }
        >
          <div className="grid gap-4 pt-4">
            <div className="grid grid-cols-2 gap-6 p-4 rounded-lg bg-muted/50 border border-border/50">
              <NumberField
                name="methods.fingerprint.retries"
                id="fingerprint-max-retries"
                label="Max Retries"
                min={1}
                max={5}
              />
              {/* min stays 0, not the schema's 1: the browser counts step from min,
                so min 1 would mark the default 5000 ms invalid on submit. */}
              <NumberField
                name="methods.fingerprint.timeout"
                id="fingerprint-timeout"
                label="Timeout (ms)"
                min={0}
                max={10000}
                step={100}
              />
            </div>
            <div className="overflow-hidden">
              <FingerprintSetting />
            </div>
          </div>
        </MethodCard>
      </div>
    </div>
  );
}
