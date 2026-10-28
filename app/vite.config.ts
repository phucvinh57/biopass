import path from "node:path";
import babel from "@rolldown/plugin-babel";
import tailwindcss from "@tailwindcss/vite";
import { tanstackRouter } from "@tanstack/router-plugin/vite";
import react, { reactCompilerPreset } from "@vitejs/plugin-react";
import { defineConfig } from "vite";

const host = process.env.TAURI_DEV_HOST;

// https://vite.dev/config/
export default defineConfig(async () => ({
  plugins: [
    tanstackRouter({
      target: "react",
      autoCodeSplitting: true,
      routesDirectory: "./src/app",
      indexToken: "page",
      routeToken: "layout",
    }),
    babel({ presets: [reactCompilerPreset()] }),
    react(),
    tailwindcss(),
  ],
  resolve: {
    alias: {
      "@": path.resolve(import.meta.dirname, "./src"),
    },
  },

  build: {
    rollupOptions: {
      output: {
        manualChunks: (id: string | string[]) => {
          if (id.includes("node_modules")) {
            // Group Tauri-related packages (usually independent)
            if (id.includes("@tauri-apps")) {
              return "vendor-tauri";
            }
            // Group Dnd-kit (can be large and is relatively independent)
            if (id.includes("@dnd-kit")) {
              return "vendor-dnd";
            }
            if (
              id.includes("/react/") ||
              id.includes("/react-dom/") ||
              id.includes("/scheduler/")
            ) {
              return "vendor-react";
            }
            if (id.includes("@tanstack")) {
              return "vendor-router";
            }
            if (id.includes("lucide-react")) {
              return "vendor-icons";
            }
            if (
              id.includes("@radix-ui") ||
              id.includes("class-variance-authority") ||
              id.includes("clsx") ||
              id.includes("tailwind-merge") ||
              id.includes("next-themes") ||
              id.includes("sonner")
            ) {
              return "vendor-ui";
            }
            if (
              id.includes("react-hook-form") ||
              id.includes("@hookform") ||
              id.includes("zod")
            ) {
              return "vendor-forms";
            }
            // Keep any remaining third-party dependencies in a fallback vendor chunk.
            return "vendor";
          }
          // Group configuration UI components for better organization
          if (id.includes("src/components/config")) {
            return "config-ui";
          }
        },
      },
    },
  },

  // Vite options tailored for Tauri development and only applied in `tauri dev` or `tauri build`
  //
  // 1. prevent Vite from obscuring rust errors
  clearScreen: false,
  // 2. tauri expects a fixed port, fail if that port is not available
  server: {
    port: 1420,
    strictPort: true,
    host: host || false,
    hmr: host
      ? {
          protocol: "ws",
          host,
          port: 1421,
        }
      : undefined,
    watch: {
      // 3. tell Vite to ignore watching `src-tauri`
      ignored: ["**/src-tauri/**"],
    },
  },
}));
