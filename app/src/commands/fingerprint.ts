import { commands } from "@/bindings";

function listEnrolled(username: string) {
  return commands.listEnrolledFingerprints(username);
}

async function enroll(username: string, fingerName: string): Promise<void> {
  await commands.enrollFingerprint(username, fingerName);
}

async function remove(username: string, fingerName: string): Promise<void> {
  await commands.removeFingerprint(username, fingerName);
}

export const fingerprint = {
  listEnrolled,
  enroll,
  remove,
};
