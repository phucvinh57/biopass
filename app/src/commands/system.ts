import { commands } from "@/bindings";

function getCurrentUsername() {
  return commands.getCurrentUsername();
}

export const system = {
  getCurrentUsername,
};
