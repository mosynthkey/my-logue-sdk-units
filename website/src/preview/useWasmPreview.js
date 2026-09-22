import { computed, nextTick, ref, shallowRef } from "vue";
import { unlockAudioSessionSync } from "../composables/useAudioSession.js";
import { previewDebugLog } from "../composables/usePreviewDebugLog.js";
import { defaultDrySourceId, DRY_SOURCES, isDrySourceId } from "./drySources.js";
import { previewLayout, usesDryInput, usesKickDemo } from "./layout.js";
import { needsGestureForWasmStart } from "./gesture.js";
import { PreviewSession } from "./PreviewSession.js";

function approximatelyEqual(a, b, epsilon = 1e-4) {
  return Math.abs(a - b) < epsilon;
}

function describePreviewError(error) {
  if (!error) {
    return "Preview failed.";
  }
  if (typeof error === "string") {
    return error;
  }
  if (typeof error.message === "string" && error.message.length > 0) {
    return error.message;
  }
  return String(error);
}

function buildPlaceholderKnobs(plugin) {
  return (plugin?.params || []).map((param) => ({
    name: param.name,
    min: 0,
    max: 100,
    value: 50,
    index: 0,
    placeholder: true,
    valueLabel: "50",
  }));
}

export function useWasmPreview(previewShellRef) {
  const phase = ref("idle");
  const message = ref("Select a plugin to preview.");
  const layout = ref("keyboard");
  const knobs = ref([]);
  const showInstrument = ref(false);
  const showKnobs = ref(false);
  const audioRunning = ref(false);
  const latchEnabled = ref(false);
  const holdEnabled = ref(false);
  const masterVolume = ref(0.5);
  const bpm = ref(120);

  const previewGeneration = ref(0);
  const knobMappings = shallowRef([]);
  const paramAssign = shallowRef({ X: 1, Y: 2, Depth: 3 });
  const frequencyStack = ref([]);
  const depthNormalized = ref(0.5);
  const awaitingWasmTap = ref(false);
  const kickDemoActive = ref(false);
  const showDryInput = ref(false);
  const drySourceId = ref("house");
  const dryPlaying = ref(false);

  let session = null;
  let pendingTapFinish = null;

  const isLoading = computed(() => phase.value === "loading");
  const isReady = computed(() => phase.value === "ready");
  const hasWasm = computed(() => phase.value !== "no-wasm");
  const hasDepthMapping = computed(() => knobMappings.value.some(
    (mapping) => mapping.assign === paramAssign.value.Depth,
  ));

  function host() {
    return session?.host ?? null;
  }

  function resetPlaybackState() {
    frequencyStack.value = [];
    latchEnabled.value = false;
    holdEnabled.value = false;
    audioRunning.value = false;
    dryPlaying.value = false;
  }

  function setDrySource(nextSourceId) {
    if (!isDrySourceId(nextSourceId)) {
      return;
    }
    drySourceId.value = nextSourceId;
    host()?.setDrySource(nextSourceId);
  }

  function setDryPlaying(nextPlaying) {
    dryPlaying.value = Boolean(nextPlaying);
    host()?.setDryPlaying(dryPlaying.value);
  }

  function toggleDryPlayback() {
    const nextPlaying = !dryPlaying.value;
    if (nextPlaying) {
      host()?.resumeAudio();
      audioRunning.value = true;
    }
    setDryPlaying(nextPlaying);
  }

  function formatKnobValue(index, value) {
    return host()?.formatKnobValue(index, value) ?? String(Math.round(value));
  }

  function setKnobValue(knobIndex, nextValue, dispatch = true) {
    const knob = knobs.value[knobIndex];
    if (!knob) {
      return;
    }
    const clamped = Math.min(Math.max(nextValue, knob.min), knob.max);
    knob.value = clamped;
    knob.valueLabel = formatKnobValue(knob.index, clamped);
    if (dispatch) {
      host()?.setParam(knob.index, clamped);
    }
  }

  function setMasterVolume(nextValue) {
    const clamped = Math.min(Math.max(nextValue, 0), 1);
    masterVolume.value = clamped;
    host()?.setMasterVolume(clamped);
  }

  function setBpm(nextValue) {
    const clamped = Math.min(Math.max(nextValue, 30), 240);
    bpm.value = clamped;
    host()?.setBpm(clamped);
  }

  const masterVolumeLabel = computed(() => String(Math.round(masterVolume.value * 100)));
  const bpmLabel = computed(() => {
    const rounded = Math.round(bpm.value * 10) / 10;
    return Number.isInteger(rounded) ? String(rounded) : rounded.toFixed(1);
  });

  function applyMappingValue(paramIndex, normalized, unipolar, curve) {
    const runtime = host();
    const knobIndex = knobs.value.findIndex((knob) => knob.index === paramIndex);
    if (!runtime || knobIndex < 0) {
      return;
    }
    const knob = knobs.value[knobIndex];
    const curved = runtime.applyCurve(normalized, curve, unipolar);
    setKnobValue(knobIndex, curved * (knob.max - knob.min) + knob.min);
  }

  function handleXyPosition(xNormalized, yNormalized) {
    // Match NTS-3 / SDK xypad: screen top = Y max (browser Y is top-origin).
    const yFromBottom = 1 - yNormalized;
    for (const mapping of knobMappings.value) {
      if (mapping.assign === paramAssign.value.X) {
        applyMappingValue(mapping.paramIndex, xNormalized, mapping.unipolar, mapping.curve);
      } else if (mapping.assign === paramAssign.value.Y) {
        applyMappingValue(mapping.paramIndex, yFromBottom, mapping.unipolar, mapping.curve);
      }
    }
  }

  function handleDepthChange(nextDepthNormalized) {
    const clamped = Math.min(Math.max(nextDepthNormalized, 0), 1);
    depthNormalized.value = clamped;
    for (const mapping of knobMappings.value) {
      if (mapping.assign === paramAssign.value.Depth) {
        applyMappingValue(mapping.paramIndex, clamped, mapping.unipolar, mapping.curve);
      }
    }
  }

  function readScopeSnapshot() {
    return host()?.readScopeSnapshot() ?? null;
  }

  function syncAudioRunning() {
    audioRunning.value = host()?.audioState() === "running";
  }

  function toggleAudio() {
    const runtime = host();
    if (!runtime) {
      return;
    }
    if (runtime.audioState() !== "running") {
      runtime.resumeAudio();
      audioRunning.value = true;
    } else {
      runtime.suspendAudio();
      audioRunning.value = false;
    }
  }

  function onKeyboardDown(note, frequency) {
    const runtime = host();
    if (!runtime) {
      return;
    }
    runtime.resumeAudio();
    audioRunning.value = true;
    frequencyStack.value = [...frequencyStack.value, frequency];
    runtime.setOscPitch(frequency);
    runtime.noteOn(note, 100);
    runtime.setGate(true);
  }

  function onKeyboardUp(note, frequency) {
    const runtime = host();
    if (!runtime) {
      return;
    }
    const nextStack = [...frequencyStack.value];
    for (let stackIndex = nextStack.length - 1; stackIndex >= 0; stackIndex -= 1) {
      if (approximatelyEqual(nextStack[stackIndex], frequency)) {
        nextStack.splice(stackIndex, 1);
        break;
      }
    }
    frequencyStack.value = nextStack;

    if (nextStack.length > 0) {
      runtime.setOscPitch(nextStack[nextStack.length - 1]);
    } else if (!latchEnabled.value) {
      runtime.setGate(false);
    }

    runtime.noteOff(note);
  }

  function onLatchToggle() {
    latchEnabled.value = !latchEnabled.value;
    if (!latchEnabled.value && frequencyStack.value.length === 0) {
      host()?.setGate(false);
    }
  }

  function onTouchBegan(xNormalized, yNormalized) {
    host()?.touchBegan(xNormalized, yNormalized);
    audioRunning.value = true;
    handleXyPosition(xNormalized, yNormalized);
  }

  function onTouchMoved(xNormalized, yNormalized) {
    host()?.touchMoved(xNormalized, yNormalized);
    handleXyPosition(xNormalized, yNormalized);
  }

  function onTouchEnded(xNormalized, yNormalized) {
    host()?.touchEnded(xNormalized, yNormalized);
    handleXyPosition(xNormalized, yNormalized);
  }

  function onHoldToggle(lastPointerEvent) {
    holdEnabled.value = !holdEnabled.value;
    if (!holdEnabled.value && lastPointerEvent) {
      onTouchEnded(lastPointerEvent.xNormalized, lastPointerEvent.yNormalized);
    }
  }

  async function teardown() {
    previewGeneration.value += 1;
    pendingTapFinish = null;
    delete window.__previewGestureDone;
    awaitingWasmTap.value = false;
    resetPlaybackState();
    masterVolume.value = 0.5;
    bpm.value = 120;
    depthNormalized.value = 0.5;
    showInstrument.value = false;
    showKnobs.value = false;
    showDryInput.value = false;
    knobs.value = [];
    knobMappings.value = [];
    if (session) {
      session.setGestureCapture(false);
      await session.destroy();
      session = null;
    }
  }

  async function mount(build, plugin) {
    await teardown();
    const generation = previewGeneration.value;
    previewDebugLog("info", "Mount preview", { plugin: plugin?.id, target: build?.target });

    if (!build?.wasm) {
      phase.value = "no-wasm";
      message.value = "No WebAssembly preview for this plugin yet.";
      knobs.value = buildPlaceholderKnobs(plugin);
      showKnobs.value = knobs.value.length > 0;
      showInstrument.value = false;
      return;
    }

    layout.value = previewLayout(build);
    kickDemoActive.value = usesKickDemo(plugin);
    showDryInput.value = usesDryInput(plugin, build);
    drySourceId.value = defaultDrySourceId(plugin);
    dryPlaying.value = false;
    phase.value = "loading";
    message.value = "Loading preview…";
    knobs.value = buildPlaceholderKnobs(plugin);
    showKnobs.value = knobs.value.length > 0;
    showInstrument.value = false;

    try {
      session = new PreviewSession();
      await session.attach();
      if (generation !== previewGeneration.value) {
        return;
      }
      previewDebugLog("info", "Preview runtime attached", {
        parentIsolated: window.crossOriginIsolated,
        runtimeIsolated: session.iframe?.contentWindow?.crossOriginIsolated,
      });

      const deferMain = needsGestureForWasmStart();
      const wasmHref = new URL(build.wasm, document.baseURI).href;
      const { audioReady } = await session.host.configureAndLoad({
        wasmHref,
        layoutName: layout.value,
        dryInput: showDryInput.value,
        drySource: drySourceId.value,
        dryPlaying: dryPlaying.value,
        target: build.target,
        deferMain,
      });
      if (generation !== previewGeneration.value) {
        return;
      }

      if (deferMain) {
        awaitingWasmTap.value = true;
        phase.value = "gesture";
        message.value = window.crossOriginIsolated
          ? "Tap to start preview"
          : "Tap to start preview (reload once if audio stays silent)";
        previewDebugLog("info", "Waiting for single tap to unlock audio and start wasm");
        await new Promise((resolve) => {
          pendingTapFinish = resolve;
          window.__previewGestureDone = () => {
            awaitingWasmTap.value = false;
            resolve();
          };
          void nextTick(() => {
            if (generation !== previewGeneration.value || !session) {
              return;
            }
            const captureTarget = previewShellRef?.value ?? null;
            if (!captureTarget) {
              previewDebugLog("warn", "Gesture capture target missing — using document body");
            }
            session.setGestureCapture(true, captureTarget ?? document.body);
          });
        });
        pendingTapFinish = null;
        delete window.__previewGestureDone;
        if (generation !== previewGeneration.value) {
          return;
        }
      }

      phase.value = "loading";
      message.value = "Loading preview…";
      await audioReady;
      if (generation !== previewGeneration.value) {
        return;
      }

      const runtime = host();
      knobs.value = runtime.readKnobs();
      showKnobs.value = knobs.value.length > 0;

      if (layout.value === "xypad") {
        const mappingData = runtime.readMappings(knobs.value.length);
        knobMappings.value = mappingData.entries;
        paramAssign.value = mappingData.paramAssign;
        for (const mapping of knobMappings.value) {
          const knobIndex = knobs.value.findIndex((knob) => knob.index === mapping.paramIndex);
          if (knobIndex >= 0) {
            setKnobValue(knobIndex, mapping.init);
          }
        }
        if (hasDepthMapping.value) {
          handleDepthChange(depthNormalized.value);
        }
      }

      showInstrument.value = true;
      phase.value = "ready";
      message.value = "";
      runtime.setMasterVolume(masterVolume.value);
      runtime.setBpm(bpm.value);
      if (showDryInput.value) {
        runtime.setDrySource(drySourceId.value);
        runtime.setDryPlaying(dryPlaying.value);
      }
      runtime.resumeAudio();
      syncAudioRunning();
      audioRunning.value = true;
    } catch (error) {
      if (generation !== previewGeneration.value) {
        return;
      }
      phase.value = "error";
      message.value = describePreviewError(error);
      showInstrument.value = false;
      previewDebugLog("error", "Preview mount failed", describePreviewError(error));
    }
  }

  function startPreviewFromTap() {
    if (!awaitingWasmTap.value) {
      return;
    }
    unlockAudioSessionSync();
    if (!host()?.startMain()) {
      previewDebugLog("warn", "Tap ignored — wasm not ready yet");
      return;
    }
    awaitingWasmTap.value = false;
    session?.setGestureCapture(false);
    pendingTapFinish?.();
  }

  return {
    phase,
    message,
    layout,
    knobs,
    showInstrument,
    showKnobs,
    audioRunning,
    latchEnabled,
    holdEnabled,
    masterVolume,
    bpm,
    depthNormalized,
    hasDepthMapping,
    masterVolumeLabel,
    bpmLabel,
    awaitingWasmTap,
    kickDemoActive,
    showDryInput,
    drySources: DRY_SOURCES,
    drySourceId,
    dryPlaying,
    isLoading,
    isReady,
    hasWasm,
    mount,
    teardown,
    setKnobValue,
    setMasterVolume,
    setBpm,
    setDrySource,
    toggleDryPlayback,
    handleDepthChange,
    readScopeSnapshot,
    toggleAudio,
    onKeyboardDown,
    onKeyboardUp,
    onLatchToggle,
    onTouchBegan,
    onTouchMoved,
    onTouchEnded,
    onHoldToggle,
    startPreviewFromTap,
  };
}
