import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// Served statically by the Drogon API (same origin → no CORS) from web/dist.
// In dev, `npm run dev` proxies /api and /openapi.json to the local API.
export default defineConfig({
  plugins: [react()],
  base: "/",
  build: { outDir: "dist", emptyOutDir: true },
  server: {
    proxy: {
      "/api": "http://127.0.0.1:8080",
      "/openapi.json": "http://127.0.0.1:8080",
    },
  },
});
