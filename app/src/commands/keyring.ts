import { commands } from "@/bindings";

function status() {
  return commands.getKeyringStatus();
}

async function save(password: string, bindPcr7: boolean): Promise<void> {
  await commands.saveKeyringPassword(password, bindPcr7);
}

async function forget(): Promise<void> {
  await commands.forgetKeyringPassword();
}

export const keyring = {
  status,
  save,
  forget,
};
