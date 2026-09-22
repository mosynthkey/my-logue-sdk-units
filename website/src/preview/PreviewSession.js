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

function fixedCaptureStyle(bounds) {
  return [
    "position:fixed",
    `left:${Math.max(0, bounds.left)}px`,
    `top:${Math.max(0, bounds.top)}px`,
    `width:${Math.max(1, bounds.width)}px`,
    `height:${Math.max(1, bounds.height)}px`,
    "opacity:0",
    "border:0",
    "pointer-events:auto",
    "z-index:1000",
    "background:transparent",
  ].join(";");
}

export class PreviewSession {
  constructor() {
    this.iframe = null;
    this.gestureCaptureTarget = null;
    this.gestureCaptureCleanups = [];
    this.runtimeGeneration = 0;
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
    // Stay on document.body for the whole lifetime. Reparenting the iframe
    // (e.g. into .preview-shell) reloads it on iOS Safari and wipes __previewHost.
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
    this.runtimeGeneration += 1;
    const attachedGeneration = this.runtimeGeneration;

    frameWindow.addEventListener("error", (event) => {
      previewDebugLog("error", event.message || "Runtime error");
    });
    frameWindow.addEventListener("unhandledrejection", (event) => {
      previewDebugLog("error", event.reason || "Runtime promise rejection");
    });

    // If the browsing context is unexpectedly navigated/reloaded, taps go nowhere.
    iframe.addEventListener("load", () => {
      if (this.iframe !== iframe || attachedGeneration !== this.runtimeGeneration) {
        return;
      }
      if (!iframe.contentWindow?.__previewHost) {
        previewDebugLog(
          "error",
          "Preview runtime iframe reloaded and lost __previewHost — tap-to-start will not work",
        );
      }
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

  syncGestureCaptureFrame(captureTarget) {
    if (!this.iframe || this.gestureCaptureTarget !== captureTarget) {
      return null;
    }
    if (this.iframe.parentElement !== document.body) {
      document.body.append(this.iframe);
    }
    const bounds = captureTarget.getBoundingClientRect();
    this.iframe.style.cssText = fixedCaptureStyle(bounds);
    return bounds;
  }

  // Keep a fixed overlay aligned to the tap target across scroll / mobile chrome.
  // Never move the iframe node — iOS Safari reloads reparented frames.
  installGestureCaptureSync(captureTarget) {
    this.clearGestureCaptureSync();

    const bounds = this.syncGestureCaptureFrame(captureTarget);
    previewDebugLog("info", "Gesture capture armed", {
      left: Math.round(bounds?.left ?? 0),
      top: Math.round(bounds?.top ?? 0),
      width: Math.round(bounds?.width ?? 0),
      height: Math.round(bounds?.height ?? 0),
      hostAlive: Boolean(this.host),
    });

    const onViewportChange = () => {
      const nextBounds = this.syncGestureCaptureFrame(captureTarget);
      if (nextBounds && !this.host) {
        previewDebugLog("error", "Gesture capture target moved but preview host is gone");
      }
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

    this.installGestureCaptureSync(captureTarget);

    const runtime = this.host;
    if (!runtime?.armGestureStart) {
      previewDebugLog(
        "error",
        "Cannot arm gesture start — preview host missing (iframe may have reloaded)",
      );
      return;
    }

    runtime.armGestureStart(() => {
      this.setGestureCapture(false);
      window.__previewGestureDone?.();
    });
  }

  async destroy() {
    this.runtimeGeneration += 1;
    this.setGestureCapture(false);
    if (!this.iframe) {
      return;
    }
    this.iframe.remove();
    this.iframe = null;
  }
}
