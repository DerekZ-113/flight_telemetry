/// <reference types="vitest/config" />
import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// One configuration serves the build, the dev server and the tests (Vitest
// is Vite's own test runner). The dev server binds to the loopback for the
// same reason the bridge does (REQ-DISP-005): the browser is on this machine.
export default defineConfig({
  plugins: [react()],
  server: { host: "127.0.0.1", port: 5173 },
  test: {
    environment: "jsdom",
    include: ["src/**/*.test.{ts,tsx}"],
    coverage: {
      provider: "v8",
      include: ["src/**/*.{ts,tsx}"],
      exclude: ["src/main.tsx", "src/**/*.test.*", "src/fakeSocket.ts"],
      reporter: ["text", "json-summary"],
    },
  },
});
