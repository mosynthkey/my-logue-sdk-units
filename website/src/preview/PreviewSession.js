import { previewDebugLog } from "../composables/usePreviewDebugLog.js";

const HIDDEN_FRAME_STYLE = [
  "position:fixed",
  "left:0",
  "top:0",
  "width:1px",
  "height:1px",
  "opacity:0",
  "border:0",
  "pointer-events:none",
  "z-index:-1",
].join(";");

const CAPTURE_FRAME_STYLE = [
  "position:absolute",
  "inset:0",
  "width:100%",
  "height:100%",
  "opacity:0",
  "border:0",
  "pointer-events:auto",
  "z-index:1000",
  "background:transparent",
].join(";");

function assetUrl(relativePath) {
  return new URL(relativePath, window.location.href).href;
}

function runtimeBootstrapHtml() {
  const runtimeUrl = assetUrl("preview-runtime.js");
  // Do not register coi-serviceworker here — the parent page owns COI and nested
  // registration breaks iOS Safari (see edbb59d).
  return `<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>Plugin preview runtime</title>
</head>
<body>
  <script src="${runtimeUrl}"><\/script>
</body>
</html>`;
}

async function waitForPreviewHost(frameWindow, timeoutMs = 15000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (frameWindow?.__previewHost) {
      return;
    }
    await new Promise((resolve) => {
      window.setTimeout(resolve, 50);
    });
  }
  throw new Error("Preview runtime failed to initialize");
}

export class PreviewSession {
  constructor() {
    this.iframe = null;
    this.gestureCaptureTarget = null;
    this.gestureCaptureCleanups = [];
  }

  get host() {
    return this.iframe?.contentWindow?.__previewHost ?? null;
  }

  async attach() {
    await this.destroy();
    window.__previewRuntimeLog = previewDebugLog;

    const iframe = document.createElement("iframe");
    iframe.className = "preview-runtime-frame";
    iframe.setAttribute("aria-hidden", "true");
    iframe.setAttribute("allow", "autoplay");
    iframe.tabIndex = -1;
    iframe.style.cssText = HIDDEN_FRAME_STYLE;

    const loaded = new Promise((resolve, reject) => {
      iframe.addEventListener("load", resolve, { once: true });
      iframe.addEventListener("error", () => {
        reject(new Error("Failed to load preview runtime"));
      }, { once: true });
    });

    iframe.src = "about:blank";
    document.body.append(iframe);
    this.iframe = iframe;
    await loaded;

    const frameDocument = iframe.contentDocument;
    if (!frameDocument) {
      throw new Error("Preview runtime iframe is inaccessible");
    }

    frameDocument.open();
    frameDocument.write(runtimeBootstrapHtml());
    frameDocument.close();

    const frameWindow = iframe.contentWindow;
    await waitForPreviewHost(frameWindow);

    frameWindow.addEventListener("error", (event) => {
      previewDebugLog("error", event.message || "Runtime error");
    });
    frameWindow.addEventListener("unhandledrejection", (event) => {
      previewDebugLog("error", event.reason || "Runtime promise rejection");
    });
  }

  clearGestureCaptureSync() {
    for (const cleanup of this.gestureCaptureCleanups) {
      try {
        cleanup();
      } catch {
        // Ignore cleanup errors during teardown.
      }
    }
    this.gestureCaptureCleanups = [];
  }

  // Keep the invisible capture frame covering the tap target even when mobile
  // chrome resizes or the user scrolls mid-"Tap to start".
  installGestureCaptureSync(captureTarget) {
    this.clearGestureCaptureSync();

    const previousPosition = captureTarget.style.position;
    const computedPosition = window.getComputedStyle(captureTarget).position;
    if (computedPosition === "static") {
      captureTarget.style.position = "relative";
      this.gestureCaptureCleanups.push(() => {
        captureTarget.style.position = previousPosition;
      });
    }

    const syncFrame = () => {
      if (!this.iframe || this.gestureCaptureTarget !== captureTarget) {
        return;
      }
      if (this.iframe.parentElement !== captureTarget) {
        captureTarget.append(this.iframe);
      }
      this.iframe.style.cssText = CAPTURE_FRAME_STYLE;
    };

    syncFrame();

    const onViewportChange = () => {
      syncFrame();
    };
    window.addEventListener("scroll", onViewportChange, true);
    window.addEventListener("resize", onViewportChange);
    this.gestureCaptureCleanups.push(() => {
      window.removeEventListener("scroll", onViewportChange, true);
      window.removeEventListener("resize", onViewportChange);
    });

    const visualViewport = window.visualViewport;
    if (visualViewport) {
      visualViewport.addEventListener("resize", onViewportChange);
      visualViewport.addEventListener("scroll", onViewportChange);
      this.gestureCaptureCleanups.push(() => {
        visualViewport.removeEventListener("resize", onViewportChange);
        visualViewport.removeEventListener("scroll", onViewportChange);
      });
    }
  }

  setGestureCapture(enabled, captureTarget = null) {
    if (!this.iframe) {
      return;
    }

    this.clearGestureCaptureSync();
    this.gestureCaptureTarget = enabled ? captureTarget : null;
    if (!enabled || !captureTarget) {
      if (this.iframe.parentElement !== document.body) {
        document.body.append(this.iframe);
      }
      this.iframe.style.cssText = HIDDEN_FRAME_STYLE;
      this.host?.disarmGestureStart?.();
      return;
    }

    // document.body has no useful containing block for inset:0 — use the viewport.
    if (captureTarget === document.body || captureTarget === document.documentElement) {
      if (this.iframe.parentElement !== document.body) {
        document.body.append(this.iframe);
      }
      const syncViewportFrame = () => {
        if (!this.iframe || this.gestureCaptureTarget !== captureTarget) {
          return;
        }
        this.iframe.style.cssText = [
          "position:fixed",
          "left:0",
          "top:0",
          "width:100vw",
          "height:100vh",
          "opacity:0",
          "border:0",
          "pointer-events:auto",
          "z-index:1000",
          "background:transparent",
        ].join(";");
      };
      syncViewportFrame();
      window.addEventListener("resize", syncViewportFrame);
      this.gestureCaptureCleanups.push(() => {
        window.removeEventListener("resize", syncViewportFrame);
      });
      const visualViewport = window.visualViewport;
      if (visualViewport) {
        visualViewport.addEventListener("resize", syncViewportFrame);
        this.gestureCaptureCleanups.push(() => {
          visualViewport.removeEventListener("resize", syncViewportFrame);
        });
      }
    } else {
      this.installGestureCaptureSync(captureTarget);
    }

    this.host?.armGestureStart?.(() => {
      this.setGestureCapture(false);
      window.__previewGestureDone?.();
    });
  }

  async destroy() {
    this.setGestureCapture(false);
    if (!this.iframe) {
      return;
    }
    this.iframe.remove();
    this.iframe = null;
  }
}
