import { configDefaults, defineConfig } from "vitest/config";
import react from "@vitejs/plugin-react";

export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
    proxy: {
      "/ws": {
        target: "ws://127.0.0.1:17800",
        ws: true,
      },
    },
  },
  test: {
    exclude: [...configDefaults.exclude, "e2e/**"],
  },
  build: {
    outDir: "dist",
    emptyOutDir: true,
  },
});
