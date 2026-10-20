import { StrictMode } from "react";
import { createRoot } from "react-dom/client";

import { App } from "./App";
import "./App.css";

// The bridge's address (pipeline/bridge.py listens on the loopback at 8765 by
// default); ?bridge=ws://host:port points the page at one elsewhere.
const params = new URLSearchParams(window.location.search);
const url = params.get("bridge") ?? "ws://127.0.0.1:8765";

createRoot(document.getElementById("root")!).render(
  <StrictMode>
    <App url={url} />
  </StrictMode>,
);
