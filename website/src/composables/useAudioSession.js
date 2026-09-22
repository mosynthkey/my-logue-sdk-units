import { ref } from "vue";
import { previewDebugLog } from "./usePreviewDebugLog.js";
import { needsGestureForWasmStart } from "../preview/gesture.js";

const unlocked = ref(false);
let unlockPromise = null;
let resolveUnlock = null;

function resetUnlockPromise() {
  unlockPromise = new Promise((resolve) => {
    resolveUnlock = resolve;
  });
}

resetUnlockPromise();

if (!needsGestureForWasmStart()) {
  unlocked.value = true;
  resolveUnlock?.();
  resolveUnlock = null;
}

export function isAudioUnlocked() {
  return unlocked.value;
}

export function whenAudioUnlocked() {
  if (unlocked.value) {
    return Promise.resolve();
  }
  return unlockPromise;
}

export function unlockAudioSessionSync() {
  if (unlocked.value) {
    return;
  }

  const AudioContextClass = window.AudioContext || window.webkitAudioContext;
  if (AudioContextClass) {
    const unlockContext = new AudioContextClass();
    try {
      const silentBuffer = unlockContext.createBuffer(1, 1, unlockContext.sampleRate);
      const silentSource = unlockContext.createBufferSource();
      silentSource.buffer = silentBuffer;
      silentSource.connect(unlockContext.destination);
      silentSource.start(0);
    } catch {
      // Resume alone still helps when buffer scheduling is unavailable.
    }
    if (unlockContext.state === "suspended") {
      void unlockContext.resume();
    }
    window.setTimeout(() => {
      if (unlockContext.state !== "closed") {
        void unlockContext.close();
      }
    }, 1000);
  }

  unlocked.value = true;
  previewDebugLog("info", "Audio session unlocked");
  resolveUnlock?.();
  resolveUnlock = null;
}

export function useAudioSession() {
  return {
    unlocked,
    unlockAudioSessionSync,
    whenAudioUnlocked,
  };
}
