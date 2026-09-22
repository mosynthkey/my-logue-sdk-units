import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";

const websiteRoot = path.join(path.dirname(fileURLToPath(import.meta.url)), "../website");
const runtimePath = path.join(websiteRoot, "public/preview-runtime.js");
const sessionPath = path.join(websiteRoot, "src/preview/PreviewSession.js");
const previewHookPath = path.join(websiteRoot, "src/preview/useWasmPreview.js");

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

test("gesture capture iframe is laid out inside the tap target", () => {
  const sessionSource = fs.readFileSync(sessionPath, "utf8");
  const hookSource = fs.readFileSync(previewHookPath, "utf8");

  assert.match(sessionSource, /position:absolute/);
  assert.match(sessionSource, /installGestureCaptureSync/);
  assert.match(sessionSource, /visualViewport/);
  assert.match(hookSource, /nextTick/);
  assert.match(hookSource, /captureTarget \?\? document\.body/);
});
