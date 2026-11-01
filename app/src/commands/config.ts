import { commands } from "@/bindings";
import type { BiopassConfig } from "@/types/config";

function load(): Promise<BiopassConfig> {
  return commands.loadConfig();
}

async function save(config: BiopassConfig): Promise<void> {
  await commands.saveConfig(config);
}

// Changes only `appearance` in config.yaml; every other value is kept.
async function setAppearance(appearance: string): Promise<void> {
  await commands.setAppearance(appearance);
}

export const config = {
  load,
  save,
  setAppearance,
};
