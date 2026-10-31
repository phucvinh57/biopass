import type { BiopassConfig } from "@/types/config";
import { invokeCommand } from "./core";

function load() {
  return invokeCommand<BiopassConfig>("load_config");
}

function save(config: BiopassConfig) {
  return invokeCommand<void>("save_config", { config });
}

// Changes only `appearance` in config.yaml; every other value is kept.
function setAppearance(appearance: string) {
  return invokeCommand<void>("set_appearance", { appearance });
}

export const config = {
  load,
  save,
  setAppearance,
};
