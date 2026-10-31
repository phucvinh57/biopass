import { getCurrentWindow } from "@tauri-apps/api/window";
import { useTheme } from "next-themes";
import { useCallback, useEffect, useRef } from "react";
import { toast } from "sonner";
import { cmd } from "@/commands";
import type { BiopassConfig } from "@/types/config";

export const APPEARANCES = ["light", "dark", "system"] as const;
export type Appearance = (typeof APPEARANCES)[number];

/** The theme to apply for a stored `appearance`, or null to keep the current one. */
export function parseAppearance(value: unknown): Appearance | null {
  return APPEARANCES.find((a) => a === value) ?? null;
}

/**
 * A copy of `config` whose `appearance` is the theme in use, so saving a form
 * loaded before the user changed the theme doesn't revert it.
 */
export function withCurrentAppearance(
  config: BiopassConfig,
  theme: string | undefined,
): BiopassConfig {
  const copy = structuredClone(config);
  copy.appearance = parseAppearance(theme) ?? copy.appearance;
  return copy;
}

/**
 * Applies the theme saved in config.yaml on start, and saves the user's
 * choice when they pick one. Only `appearance` is written (set_appearance),
 * and nothing is written on load, so a config the app can't read is never
 * overwritten.
 */
export function useThemeSync() {
  const { theme, setTheme } = useTheme();
  const userChose = useRef(false);

  useEffect(() => {
    let canceled = false;
    cmd.config
      .load()
      .then((config) => {
        const appearance = parseAppearance(config.appearance);
        if (!canceled && !userChose.current && appearance) setTheme(appearance);
      })
      .catch((err) => console.error("Failed to load theme from config:", err));
    return () => {
      canceled = true;
    };
  }, [setTheme]);

  useEffect(() => {
    // Follow the OS theme through Tauri (more reliable on Linux).
    if (theme !== "system") return;
    const window = getCurrentWindow();
    const apply = (osTheme: string | null) => {
      if (osTheme)
        document.documentElement.classList.toggle("dark", osTheme === "dark");
    };

    window
      .theme()
      .then(apply)
      .catch((err) => console.error("Failed to read OS theme:", err));
    const unlisten = window.onThemeChanged(({ payload }) => apply(payload));
    return () => {
      unlisten.then((fn) => fn());
    };
  }, [theme]);

  const chooseTheme = useCallback(
    (appearance: Appearance) => {
      userChose.current = true;
      setTheme(appearance);
      cmd.config
        .setAppearance(appearance)
        .catch((err) => toast.error(`Failed to save theme: ${err}`));
    },
    [setTheme],
  );

  return { theme, chooseTheme };
}
