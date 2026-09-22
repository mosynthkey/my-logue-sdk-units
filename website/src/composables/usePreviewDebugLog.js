import { computed, ref } from "vue";

const MAX_LOG_LINES = 120;
const lines = ref([]);
const enabled = ref(false);

function detectDebugEnabled() {
  if (typeof window === "undefined") {
    return false;
  }
  const params = new URLSearchParams(window.location.search);
  return params.has("previewDebug") || params.has("debug");
}

function formatDetail(detail) {
  if (detail === undefined || detail === null) {
    return "";
  }
  if (detail instanceof Error) {
    return detail.stack || detail.message;
  }
  if (typeof detail === "string") {
    return detail;
  }
  try {
    return JSON.stringify(detail);
  } catch {
    return String(detail);
  }
}

export function initPreviewDebugLog() {
  if (typeof window === "undefined") {
    return;
  }
  if (!enabled.value) {
    enabled.value = detectDebugEnabled();
  }

  if (window.__previewDebugHooksInstalled) {
    return;
  }
  window.__previewDebugHooksInstalled = true;

  const originalConsoleError = console.error.bind(console);
  console.error = (...args) => {
    previewDebugLog("error", args.map((arg) => formatDetail(arg)).join(" "));
    originalConsoleError(...args);
  };

  window.addEventListener("error", (event) => {
    previewDebugLog("error", event.message || "Unhandled error");
  });

  window.addEventListener("unhandledrejection", (event) => {
    previewDebugLog("error", formatDetail(event.reason) || "Unhandled promise rejection");
  });
}

export function enablePreviewDebugLog(reason = "manual") {
  enabled.value = true;
  previewDebugLog("info", "Preview debug log enabled", { reason });
}

export function isPreviewDebugEnabled() {
  return enabled.value;
}

export function previewDebugLog(kind, message, detail) {
  const detailText = formatDetail(detail);
  const text = detailText ? `${message} — ${detailText}` : message;
  lines.value = [
    {
      kind,
      message: text,
      at: new Date().toISOString(),
    },
    ...lines.value,
  ].slice(0, MAX_LOG_LINES);

  if (kind === "error") {
    enabled.value = true;
  }
}

export function formatPreviewDebugLogText() {
  return lines.value
    .slice()
    .reverse()
    .map((line) => `${line.at} [${line.kind}] ${line.message}`)
    .join("\n");
}

export function usePreviewDebugLog() {
  initPreviewDebugLog();

  const visible = computed(() => enabled.value);

  async function copyLog() {
    const payload = formatPreviewDebugLogText();
    if (!payload) {
      return false;
    }
    if (navigator.clipboard?.writeText) {
      try {
        await navigator.clipboard.writeText(payload);
        return true;
      } catch {
        // Fall through to execCommand / Share on iOS when permission is denied.
      }
    }
    if (typeof navigator.share === "function") {
      try {
        await navigator.share({ text: payload });
        return true;
      } catch {
        // User cancel or unsupported share payload — try textarea fallback.
      }
    }
    const textarea = document.createElement("textarea");
    textarea.value = payload;
    textarea.setAttribute("readonly", "");
    textarea.style.position = "fixed";
    textarea.style.left = "0";
    textarea.style.top = "0";
    textarea.style.opacity = "0";
    document.body.append(textarea);
    textarea.focus();
    textarea.select();
    textarea.setSelectionRange(0, payload.length);
    let copied = false;
    try {
      copied = document.execCommand("copy");
    } catch {
      copied = false;
    }
    textarea.remove();
    return copied;
  }

  return {
    lines,
    visible,
    enabled,
    copyLog,
    enablePreviewDebugLog,
    previewDebugLog,
  };
}
