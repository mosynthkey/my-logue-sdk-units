import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";

const websiteRoot = path.join(path.dirname(fileURLToPath(import.meta.url)), "../website");
const runtimePath = path.join(websiteRoot, "public/preview-runtime.js");
const sessionPath = path.join(websiteRoot, "src/preview/PreviewSession.js");
const previewHookPath = path.join(websiteRoot, "src/preview/useWasmPreview.js");
const debugLogPath = path.join(websiteRoot, "src/composables/usePreviewDebugLog.js");

test("AudioWorklet timeout is not armed while waiting for the start tap", () => {
  const source = fs.readFileSync(runtimePath, "utf8");

  assert.match(source, /createAudioWaiter\(\{\s*armTimeout\s*=/);
  assert.match(source, /createAudioWaiter\(\{\s*armTimeout:\s*!deferMain\s*\}/);
  assert.match(source, /start the wait clock now, not at load/);
  assert.match(
    source,
    /startMainFromGesture[\s\S]*?restartAudioWaiterTimeout\(\)/,
  );
});

test("gesture arming survives a premature tap and stays listening", () => {
  const source = fs.readFileSync(runtimePath, "utf8");

  assert.match(source, /Tap ignored — wasm not ready yet/);
  assert.match(source, /Keep listeners armed/);
  assert.doesNotMatch(
    source,
    /addEventListener\("pointerdown",\s*gestureListener,\s*\{\s*once:\s*true\s*\}/,
  );
});

test("gesture capture keeps the iframe on document.body and syncs fixed bounds", () => {
  const sessionSource = fs.readFileSync(sessionPath, "utf8");
  const hookSource = fs.readFileSync(previewHookPath, "utf8");

  assert.match(sessionSource, /Never move the iframe node/);
  assert.match(sessionSource, /position:fixed/);
  assert.match(sessionSource, /getBoundingClientRect/);
  assert.match(sessionSource, /visualViewport/);
  assert.doesNotMatch(sessionSource, /position:absolute/);
  assert.doesNotMatch(sessionSource, /captureTarget\.append\(this\.iframe\)/);
  assert.match(hookSource, /nextTick/);
  assert.match(hookSource, /previewShellRef/);
});

test("preview debug log can be enabled without an error", () => {
  const source = fs.readFileSync(debugLogPath, "utf8");
  assert.match(source, /export function enablePreviewDebugLog/);
  assert.match(source, /previewDebug/);
  assert.match(source, /navigator\.share/);
});
