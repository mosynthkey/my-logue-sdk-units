// Emscripten AUDIO_WORKLET glue is a classic per-page program, not a library.
// Vue hosts it here so plugin switches can discard the whole document.
(() => {
  const AHR_ENVELOPE_TIME = 0.1;
  const PREVIEW_TIMEOUT_MS = 20000;

  let audioContext = null;
  let wasmProcessor = null;
  let masterVolumeNode = null;
  let analyser = null;
  let scopeTimeDomainBuffer = null;
  let scopeFrequencyBuffer = null;
  let referenceFrequency = 440;
  let envelope = null;
  let dryNodes = [];
  let drySourceEngine = null;
  let layout = "keyboard";
  let usesDryInput = false;
  let initialDrySource = "house";
  let initialDryPlaying = false;
  let targetName = "";
  let runtimeReady = false;
  let mainStarted = false;
  let runtimeWaiter = null;
  let audioWaiter = null;
  let audioTimeout = 0;
  let activeScript = null;
  let originalAddModule = null;
  let gestureListener = null;
  let gestureFinish = null;

  function log(kind, message, detail) {
    try {
      if (typeof window.parent.__previewRuntimeLog === "function") {
        window.parent.__previewRuntimeLog(kind, message, detail);
        return;
      }
    } catch {
      // Parent may already be gone during teardown.
    }
    if (kind === "error") {
      console.error(message, detail ?? "");
    } else if (kind === "warn") {
      console.warn(message, detail ?? "");
    } else {
      console.log(message, detail ?? "");
    }
  }

  function wasmJsUrl(wasmHref) {
    return wasmHref.replace(/\.html(?:\?.*)?$/, ".js");
  }

  function wasmBaseUrl(wasmHref) {
    const jsUrl = wasmJsUrl(wasmHref);
    return jsUrl.slice(0, jsUrl.lastIndexOf("/") + 1);
  }

  // Same token for .js / .wasm / .aw.js — busting only the glue JS after a Pages
  // deploy leaves a cached .wasm paired with new ASM_CONSTS keys (Safari:
  // "ASM_CONSTS[code] is not a function").
  function withCacheToken(url, cacheToken) {
    const resolved = new URL(url, window.location.href);
    resolved.searchParams.set("v", cacheToken);
    return resolved.href;
  }

  function isEmscriptenControlFlow(error) {
    return error === "unwind" || error?.message === "unwind";
  }

  function stopDrySourceEngine() {
    if (!drySourceEngine) {
      return;
    }
    drySourceEngine.stop();
    drySourceEngine = null;
  }

  function stopDryNodes() {
    stopDrySourceEngine();
    for (const node of dryNodes) {
      try {
        if (typeof node.stop === "function") {
          node.stop();
        }
        node.disconnect();
      } catch {
        // Ignore teardown errors.
      }
    }
    dryNodes = [];
  }

  function createNoiseBuffer(context, durationSeconds) {
    const frameCount = Math.max(1, Math.floor(context.sampleRate * durationSeconds));
    const buffer = context.createBuffer(1, frameCount, context.sampleRate);
    const samples = buffer.getChannelData(0);
    for (let sampleIndex = 0; sampleIndex < frameCount; sampleIndex += 1) {
      samples[sampleIndex] = Math.random() * 2 - 1;
    }
    return buffer;
  }

  function scheduleKickHit(context, destination, time, gain = 0.85) {
    const oscillator = context.createOscillator();
    const envelope = context.createGain();
    oscillator.type = "sine";
    oscillator.frequency.setValueAtTime(168, time);
    oscillator.frequency.exponentialRampToValueAtTime(46, time + 0.08);
    envelope.gain.setValueAtTime(0.0001, time);
    envelope.gain.exponentialRampToValueAtTime(gain, time + 0.004);
    envelope.gain.exponentialRampToValueAtTime(0.0001, time + 0.32);
    oscillator.connect(envelope).connect(destination);
    oscillator.start(time);
    oscillator.stop(time + 0.36);
  }

  function scheduleNoiseBurst(context, destination, noiseBuffer, time, duration, gain, highpassHz) {
    const source = context.createBufferSource();
    source.buffer = noiseBuffer;
    const filter = context.createBiquadFilter();
    filter.type = "highpass";
    filter.frequency.value = highpassHz;
    const envelope = context.createGain();
    envelope.gain.setValueAtTime(gain, time);
    envelope.gain.exponentialRampToValueAtTime(0.0001, time + duration);
    source.connect(filter).connect(envelope).connect(destination);
    source.start(time);
    source.stop(time + duration + 0.02);
  }

  function scheduleHat(context, destination, noiseBuffer, time, duration, gain) {
    scheduleNoiseBurst(context, destination, noiseBuffer, time, duration, gain, 7000);
  }

  function scheduleClap(context, destination, noiseBuffer, time, gain = 0.42) {
    const burstOffsets = [0, 0.012, 0.024];
    for (let burstIndex = 0; burstIndex < burstOffsets.length; burstIndex += 1) {
      const burstGain = gain * (burstIndex === burstOffsets.length - 1 ? 1 : 0.55);
      scheduleNoiseBurst(
        context,
        destination,
        noiseBuffer,
        time + burstOffsets[burstIndex],
        0.09,
        burstGain,
        900,
      );
    }
  }

  function scheduleSnare(context, destination, noiseBuffer, time, gain = 0.48) {
    const oscillator = context.createOscillator();
    oscillator.type = "triangle";
    oscillator.frequency.setValueAtTime(196, time);
    const toneEnvelope = context.createGain();
    toneEnvelope.gain.setValueAtTime(gain * 0.35, time);
    toneEnvelope.gain.exponentialRampToValueAtTime(0.0001, time + 0.12);
    oscillator.connect(toneEnvelope).connect(destination);
    oscillator.start(time);
    oscillator.stop(time + 0.14);
    scheduleNoiseBurst(context, destination, noiseBuffer, time, 0.14, gain * 0.7, 1400);
  }

  function scheduleBass(context, destination, time, frequency, duration, gain = 0.32) {
    const oscillator = context.createOscillator();
    oscillator.type = "sawtooth";
    oscillator.frequency.setValueAtTime(frequency, time);
    const filter = context.createBiquadFilter();
    filter.type = "lowpass";
    filter.Q.value = 1.2;
    filter.frequency.setValueAtTime(480, time);
    filter.frequency.exponentialRampToValueAtTime(140, time + duration * 0.75);
    const envelope = context.createGain();
    envelope.gain.setValueAtTime(0.0001, time);
    envelope.gain.exponentialRampToValueAtTime(gain, time + 0.01);
    envelope.gain.exponentialRampToValueAtTime(0.0001, time + duration);
    oscillator.connect(filter).connect(envelope).connect(destination);
    oscillator.start(time);
    oscillator.stop(time + duration + 0.02);
  }

  function scheduleAcid(context, destination, time, frequency, slideFrequency, gain = 0.22) {
    const oscillator = context.createOscillator();
    oscillator.type = "sawtooth";
    oscillator.frequency.setValueAtTime(frequency, time);
    if (slideFrequency) {
      oscillator.frequency.linearRampToValueAtTime(slideFrequency, time + 0.09);
    }
    const filter = context.createBiquadFilter();
    filter.type = "lowpass";
    filter.Q.value = 11;
    filter.frequency.setValueAtTime(2100, time);
    filter.frequency.exponentialRampToValueAtTime(240, time + 0.15);
    const envelope = context.createGain();
    envelope.gain.setValueAtTime(0.0001, time);
    envelope.gain.exponentialRampToValueAtTime(gain, time + 0.005);
    envelope.gain.exponentialRampToValueAtTime(0.0001, time + 0.17);
    oscillator.connect(filter).connect(envelope).connect(destination);
    oscillator.start(time);
    oscillator.stop(time + 0.19);
  }

  function scheduleStab(context, destination, time, frequencies, gain = 0.14) {
    for (let voiceIndex = 0; voiceIndex < frequencies.length; voiceIndex += 1) {
      const oscillator = context.createOscillator();
      oscillator.type = "sawtooth";
      oscillator.frequency.value = frequencies[voiceIndex];
      const filter = context.createBiquadFilter();
      filter.type = "lowpass";
      filter.frequency.setValueAtTime(1900, time);
      filter.frequency.exponentialRampToValueAtTime(620, time + 0.22);
      const envelope = context.createGain();
      envelope.gain.setValueAtTime(0.0001, time);
      envelope.gain.exponentialRampToValueAtTime(gain, time + 0.008);
      envelope.gain.exponentialRampToValueAtTime(0.0001, time + 0.28);
      oscillator.connect(filter).connect(envelope).connect(destination);
      oscillator.start(time);
      oscillator.stop(time + 0.3);
    }
  }

  function scheduleSawPad(context, destination, time, frequencies, duration, gain = 0.048) {
    const attack = Math.min(0.18, duration * 0.12);
    const release = Math.min(0.45, duration * 0.22);
    const sustainEnd = Math.max(time + attack + 0.05, time + duration - release);
    const stopAt = time + duration + 0.06;
    for (let voiceIndex = 0; voiceIndex < frequencies.length; voiceIndex += 1) {
      const oscillator = context.createOscillator();
      oscillator.type = "sawtooth";
      oscillator.frequency.value = frequencies[voiceIndex];
      const filter = context.createBiquadFilter();
      filter.type = "lowpass";
      filter.Q.value = 0.65;
      filter.frequency.setValueAtTime(1500, time);
      filter.frequency.linearRampToValueAtTime(980, sustainEnd);
      const envelope = context.createGain();
      envelope.gain.setValueAtTime(0.0001, time);
      envelope.gain.exponentialRampToValueAtTime(gain, time + attack);
      envelope.gain.setValueAtTime(gain, sustainEnd);
      envelope.gain.exponentialRampToValueAtTime(0.0001, time + duration);
      oscillator.connect(filter).connect(envelope).connect(destination);
      oscillator.start(time);
      oscillator.stop(stopAt);
    }
  }

  function startOscillatorSource(context, destination, type) {
    const dryGain = context.createGain();
    dryGain.gain.value = 0.2;
    const oscillator = context.createOscillator();
    oscillator.frequency.value = 220;
    oscillator.type = type;
    oscillator.connect(dryGain).connect(destination);
    oscillator.start();
    return [oscillator, dryGain];
  }

  function startNoiseSource(context, destination, noiseBuffer) {
    const dryGain = context.createGain();
    dryGain.gain.value = 0.14;
    const source = context.createBufferSource();
    source.buffer = noiseBuffer;
    source.loop = true;
    const filter = context.createBiquadFilter();
    filter.type = "highpass";
    filter.frequency.value = 180;
    source.connect(filter).connect(dryGain).connect(destination);
    source.start();
    return [source, filter, dryGain];
  }

  function startReeseSource(context, destination) {
    const mixGain = context.createGain();
    mixGain.gain.value = 0.16;
    mixGain.connect(destination);
    const nodes = [mixGain];
    const voiceFrequencies = [55, 55.35, 110];
    for (let voiceIndex = 0; voiceIndex < voiceFrequencies.length; voiceIndex += 1) {
      const oscillator = context.createOscillator();
      oscillator.type = "sawtooth";
      oscillator.frequency.value = voiceFrequencies[voiceIndex];
      const filter = context.createBiquadFilter();
      filter.type = "lowpass";
      filter.frequency.value = 260;
      filter.Q.value = 0.8;
      oscillator.connect(filter).connect(mixGain);
      oscillator.start();
      nodes.push(oscillator, filter);
    }
    return nodes;
  }

  const CHORD_PAD_SOURCE_IDS = new Set(["chord1", "chord2", "chord3"]);
  const LOOP_SOURCE_IDS = new Set([
    "house", "techno", "garage", "acid", "kick", "breakbeat", "stab",
    "chord1", "chord2", "chord3",
  ]);
  const OSCILLATOR_SOURCE_IDS = new Set(["sawtooth", "square", "sine", "triangle"]);
  const HOUSE_STAB = [220, 261.63, 329.63, 392];
  // One chord per bar. Each progression is 8 bars long.
  const CHORD_PAD_PROGRESSIONS = {
    // Am F C G Am Dm Em G
    chord1: [
      [220.00, 261.63, 329.63, 440.00],
      [174.61, 220.00, 261.63, 349.23],
      [130.81, 164.81, 196.00, 261.63],
      [196.00, 246.94, 293.66, 392.00],
      [220.00, 261.63, 329.63, 440.00],
      [146.83, 174.61, 220.00, 293.66],
      [164.81, 196.00, 246.94, 329.63],
      [196.00, 246.94, 293.66, 392.00],
    ],
    // Em C G D Am Em F#m B7
    chord2: [
      [164.81, 196.00, 246.94, 329.63],
      [130.81, 164.81, 196.00, 261.63],
      [196.00, 246.94, 293.66, 392.00],
      [146.83, 185.00, 220.00, 293.66],
      [220.00, 261.63, 329.63, 440.00],
      [164.81, 196.00, 246.94, 329.63],
      [185.00, 220.00, 277.18, 369.99],
      [123.47, 155.56, 185.00, 246.94],
    ],
    // Dm Bb F C Gm Eb Bb A
    chord3: [
      [146.83, 174.61, 220.00, 293.66],
      [116.54, 146.83, 174.61, 233.08],
      [174.61, 220.00, 261.63, 349.23],
      [130.81, 164.81, 196.00, 261.63],
      [196.00, 233.08, 293.66, 392.00],
      [155.56, 196.00, 233.08, 311.13],
      [116.54, 146.83, 174.61, 233.08],
      [110.00, 138.59, 164.81, 220.00],
    ],
  };
  const ACID_NOTES = {
    0: [110, null],
    1: [110, null],
    3: [146.83, null],
    4: [164.81, 196],
    6: [130.81, null],
    8: [110, null],
    10: [220, null],
    11: [164.81, null],
    13: [146.83, null],
    14: [130.81, null],
  };

  function scheduleLoopStep(context, destination, noiseBuffer, sourceId, stepIndex, time, stepInterval) {
    const onBeat = stepIndex % 4 === 0;
    const evenStep = stepIndex % 2 === 0;

    if (sourceId === "kick" && onBeat) {
      scheduleKickHit(context, destination, time);
      return;
    }

    if (sourceId === "house") {
      if (onBeat) {
        scheduleKickHit(context, destination, time, 0.8);
      }
      if (stepIndex === 4 || stepIndex === 12) {
        scheduleClap(context, destination, noiseBuffer, time);
      }
      if (evenStep) {
        scheduleHat(context, destination, noiseBuffer, time, 0.05, stepIndex % 4 === 2 ? 0.22 : 0.1);
      }
      if (stepIndex === 0) {
        scheduleBass(context, destination, time, 55, 0.22);
      } else if (stepIndex === 4) {
        scheduleBass(context, destination, time, 55, 0.12, 0.26);
      } else if (stepIndex === 6) {
        scheduleBass(context, destination, time, 65.41, 0.12, 0.26);
      } else if (stepIndex === 10) {
        scheduleBass(context, destination, time, 73.42, 0.12, 0.26);
      } else if (stepIndex === 12) {
        scheduleBass(context, destination, time, 55, 0.12, 0.26);
      } else if (stepIndex === 14) {
        scheduleBass(context, destination, time, 49, 0.12, 0.24);
      }
      return;
    }

    if (sourceId === "techno") {
      if (onBeat) {
        scheduleKickHit(context, destination, time, 0.88);
      }
      scheduleHat(context, destination, noiseBuffer, time, 0.035, onBeat ? 0.06 : 0.16);
      if (stepIndex === 14) {
        scheduleNoiseBurst(context, destination, noiseBuffer, time, 0.18, 0.2, 5000);
      }
      if (stepIndex === 0 || stepIndex === 8) {
        scheduleBass(context, destination, time, 41.2, 0.42, 0.28);
      }
      return;
    }

    if (sourceId === "garage") {
      if (stepIndex === 0 || stepIndex === 10) {
        scheduleKickHit(context, destination, time, 0.78);
      }
      if (stepIndex === 4 || stepIndex === 12) {
        scheduleSnare(context, destination, noiseBuffer, time, 0.5);
      }
      if (stepIndex !== 2 && stepIndex !== 5 && stepIndex !== 13) {
        scheduleHat(context, destination, noiseBuffer, time, 0.04, evenStep ? 0.14 : 0.08);
      }
      if (stepIndex === 0) {
        scheduleBass(context, destination, time, 55, 0.18);
      } else if (stepIndex === 6) {
        scheduleBass(context, destination, time, 41.2, 0.16, 0.28);
      } else if (stepIndex === 10) {
        scheduleBass(context, destination, time, 65.41, 0.16, 0.28);
      }
      return;
    }

    if (sourceId === "acid") {
      if (onBeat) {
        scheduleKickHit(context, destination, time, 0.72);
      }
      if (evenStep) {
        scheduleHat(context, destination, noiseBuffer, time, 0.04, 0.12);
      }
      const acidNote = ACID_NOTES[stepIndex];
      if (acidNote) {
        scheduleAcid(context, destination, time, acidNote[0], acidNote[1]);
      }
      return;
    }

    if (sourceId === "breakbeat") {
      if (stepIndex === 0 || stepIndex === 6 || stepIndex === 10) {
        scheduleKickHit(context, destination, time, 0.76);
      }
      if (stepIndex === 4 || stepIndex === 12 || stepIndex === 14) {
        scheduleSnare(context, destination, noiseBuffer, time, stepIndex === 14 ? 0.32 : 0.5);
      }
      if (evenStep) {
        scheduleHat(context, destination, noiseBuffer, time, 0.04, 0.14);
      }
      if (stepIndex === 0 || stepIndex === 8) {
        scheduleBass(context, destination, time, 49, 0.2, 0.24);
      }
      return;
    }

    if (sourceId === "stab") {
      if (onBeat) {
        scheduleKickHit(context, destination, time, 0.55);
      }
      if (stepIndex === 4 || stepIndex === 12) {
        scheduleStab(context, destination, time, HOUSE_STAB);
        scheduleClap(context, destination, noiseBuffer, time, 0.28);
      } else if (stepIndex === 0) {
        scheduleStab(context, destination, time, HOUSE_STAB, 0.08);
      }
      return;
    }

    if (CHORD_PAD_SOURCE_IDS.has(sourceId) && stepIndex % 16 === 0) {
      const progression = CHORD_PAD_PROGRESSIONS[sourceId];
      const barIndex = Math.floor(stepIndex / 16) % progression.length;
      // Hold one chord for a full bar, with a little overlap into the next.
      const chordDuration = stepInterval * 16.35;
      scheduleSawPad(context, destination, time, progression[barIndex], chordDuration);
    }
  }

  function createDrySourceEngine(context, destination) {
    const mixGain = context.createGain();
    mixGain.connect(destination);
    const noiseBuffer = createNoiseBuffer(context, 1.5);

    let currentBpm = 120;
    let sourceId = initialDrySource;
    let playing = false;
    let schedulerTimer = 0;
    let nextStepTime = 0;
    let stepIndex = 0;
    let continuousNodes = [];

    function clearContinuous() {
      for (const node of continuousNodes) {
        try {
          if (typeof node.stop === "function") {
            node.stop();
          }
          node.disconnect();
        } catch {
          // Ignore teardown errors.
        }
      }
      continuousNodes = [];
    }

    function stopScheduler() {
      if (schedulerTimer) {
        window.clearInterval(schedulerTimer);
        schedulerTimer = 0;
      }
    }

    function startContinuous() {
      if (OSCILLATOR_SOURCE_IDS.has(sourceId)) {
        continuousNodes = startOscillatorSource(context, mixGain, sourceId);
        return;
      }
      if (sourceId === "noise") {
        continuousNodes = startNoiseSource(context, mixGain, noiseBuffer);
        return;
      }
      if (sourceId === "reese") {
        continuousNodes = startReeseSource(context, mixGain);
      }
    }

    function loopLengthForSource(id) {
      if (CHORD_PAD_SOURCE_IDS.has(id)) {
        return CHORD_PAD_PROGRESSIONS[id].length * 16;
      }
      return 16;
    }

    function tick() {
      if (!playing || !LOOP_SOURCE_IDS.has(sourceId)) {
        return;
      }
      const stepInterval = (60 / currentBpm) / 4;
      const loopLength = loopLengthForSource(sourceId);
      while (nextStepTime < context.currentTime + 0.15) {
        scheduleLoopStep(context, mixGain, noiseBuffer, sourceId, stepIndex, nextStepTime, stepInterval);
        nextStepTime += stepInterval;
        stepIndex = (stepIndex + 1) % loopLength;
      }
    }

    function applyPlayback() {
      clearContinuous();
      stopScheduler();
      if (!playing) {
        return;
      }
      if (LOOP_SOURCE_IDS.has(sourceId)) {
        nextStepTime = context.currentTime + 0.03;
        stepIndex = 0;
        schedulerTimer = window.setInterval(tick, 25);
        tick();
        return;
      }
      startContinuous();
    }

    return {
      nodes: [mixGain],
      setSource(nextSourceId) {
        const normalized = LOOP_SOURCE_IDS.has(nextSourceId)
          || OSCILLATOR_SOURCE_IDS.has(nextSourceId)
          || nextSourceId === "noise"
          || nextSourceId === "reese"
          ? nextSourceId
          : "house";
        if (normalized === sourceId) {
          return;
        }
        sourceId = normalized;
        if (playing) {
          applyPlayback();
        }
      },
      setPlaying(nextPlaying) {
        const shouldPlay = Boolean(nextPlaying);
        if (shouldPlay === playing) {
          return;
        }
        playing = shouldPlay;
        applyPlayback();
      },
      setBpm(nextBpm) {
        currentBpm = Math.min(Math.max(nextBpm, 30), 240);
      },
      stop() {
        playing = false;
        clearContinuous();
        stopScheduler();
        mixGain.disconnect();
      },
    };
  }

  function connectProcessor(context, processor) {
    stopDryNodes();

    if (usesDryInput) {
      drySourceEngine = createDrySourceEngine(context, processor);
      drySourceEngine.setSource(initialDrySource);
      drySourceEngine.setPlaying(initialDryPlaying);
      dryNodes.push(...drySourceEngine.nodes);
      return;
    }

    if (targetName === "nts-3_kaoss") {
      const silentGain = context.createGain();
      silentGain.gain.value = 0;
      const silentSource = context.createConstantSource();
      silentSource.offset.value = 0;
      silentSource.connect(silentGain).connect(processor);
      silentSource.start();
      dryNodes.push(silentSource, silentGain);
    }
  }

  function installWorkletModuleBase(baseUrl, cacheToken) {
    const AudioWorkletCtor = window.AudioWorklet;
    if (!AudioWorkletCtor) {
      return;
    }
    const audioWorkletPrototype = AudioWorkletCtor.prototype;
    const installKey = `${baseUrl}::${cacheToken}`;
    if (audioWorkletPrototype.__previewModuleBaseUrl === installKey) {
      return;
    }
    if (!originalAddModule) {
      originalAddModule = audioWorkletPrototype.addModule;
    }
    audioWorkletPrototype.addModule = function previewAddModule(moduleURL, options) {
      if (typeof moduleURL === "string") {
        if (
          !moduleURL.includes("/")
          && !/^(?:[a-z]+:|blob:|data:)/i.test(moduleURL)
        ) {
          moduleURL = new URL(moduleURL, baseUrl).href;
        }
        if (!moduleURL.startsWith("blob:") && !moduleURL.startsWith("data:")) {
          moduleURL = withCacheToken(moduleURL, cacheToken);
        }
      }
      log("info", `AudioWorklet.addModule ${moduleURL}`);
      return originalAddModule.call(this, moduleURL, options).catch((error) => {
        log("error", "AudioWorklet.addModule failed", error);
        throw error;
      });
    };
    audioWorkletPrototype.__previewModuleBaseUrl = installKey;
  }

  function loadWasmScript(url) {
    log("info", `Loading wasm script ${url}`);
    return new Promise((resolve, reject) => {
      const script = document.createElement("script");
      script.src = url;
      script.async = true;
      script.onload = () => {
        log("info", "Wasm script loaded");
        resolve(script);
      };
      script.onerror = () => {
        const error = new Error(`Failed to load ${url}`);
        log("error", error.message);
        reject(error);
      };
      document.body.append(script);
      activeScript = script;
    });
  }

  function onAudioReady(context, processor) {
    window.clearTimeout(audioTimeout);
    audioContext = context;
    wasmProcessor = processor;
    const volume = context.createGain();
    volume.gain.value = 0.5;
    masterVolumeNode = volume;

    if (layout === "keyboard") {
      if (usesDryInput) {
        connectProcessor(context, processor);
        processor.connect(volume);
      } else {
        envelope = context.createGain();
        envelope.gain.value = 0;
        processor.connect(envelope).connect(volume);
      }
    } else {
      connectProcessor(context, processor);
      processor.connect(volume);
    }

    analyser = context.createAnalyser();
    analyser.fftSize = 4096;
    scopeTimeDomainBuffer = new Float32Array(analyser.fftSize);
    scopeFrequencyBuffer = new Float32Array(analyser.frequencyBinCount);
    volume.connect(analyser);
    volume.connect(context.destination);
    log("info", "setupWebAudioAndUI called", { state: context.state });
    audioWaiter?.resolve();
    audioWaiter = null;
  }

  function rejectAudioWaiter(error) {
    window.clearTimeout(audioTimeout);
    audioTimeout = 0;
    audioWaiter?.reject(error);
    audioWaiter = null;
  }

  function clearAudioWaiterTimeout() {
    window.clearTimeout(audioTimeout);
    audioTimeout = 0;
  }

  function restartAudioWaiterTimeout() {
    if (!audioWaiter) {
      return;
    }
    clearAudioWaiterTimeout();
    audioTimeout = window.setTimeout(() => {
      const coiHint = window.crossOriginIsolated
        ? ""
        : " Audio isolation is unavailable in this browser tab.";
      const error = new Error(`Preview timed out waiting for AudioWorklet.${coiHint}`);
      log("error", error.message);
      rejectAudioWaiter(error);
    }, PREVIEW_TIMEOUT_MS);
  }

  // Start the AudioWorklet timeout only once audio init is actually running.
  // With deferMain (mobile), configureAndLoad finishes long before the user
  // taps — arming the timer there falsely times out idle "Tap to start" waits.
  function createAudioWaiter({ armTimeout = true } = {}) {
    let resolve;
    let reject;
    const promise = new Promise((res, rej) => {
      resolve = res;
      reject = rej;
    });
    audioWaiter = { promise, resolve, reject };
    if (armTimeout) {
      restartAudioWaiterTimeout();
    }
    return promise;
  }

  function waitForRuntime() {
    if (runtimeReady) {
      return Promise.resolve();
    }
    return new Promise((resolve) => {
      runtimeWaiter = resolve;
    });
  }

  function readKnobs() {
    const moduleRef = window.Module;
    const parameters = moduleRef.getValidParameters();
    const knobs = [];
    for (let paramIndex = 0; paramIndex < parameters.size(); paramIndex += 1) {
      const param = parameters.get(paramIndex);
      knobs.push({
        name: param.name,
        min: param.min,
        max: param.max,
        value: param.init,
        index: paramIndex,
        placeholder: false,
        valueLabel: formatKnobValue(paramIndex, param.init),
      });
    }
    return knobs;
  }

  function formatKnobValue(index, value) {
    const moduleRef = window.Module;
    if (moduleRef?.getParameterValueString) {
      return moduleRef.getParameterValueString(index, value);
    }
    return String(Math.round(value));
  }

  function readMappings(knobCount) {
    const moduleRef = window.Module;
    const mappings = moduleRef.getDefaultMapping();
    const entries = [];
    for (let paramIndex = 0; paramIndex < knobCount; paramIndex += 1) {
      const mapping = mappings.get(paramIndex);
      if (!mapping) {
        continue;
      }
      entries.push({
        paramIndex,
        assign: mapping.assign,
        curve: mapping.curve,
        unipolar: mapping.unipolar,
        init: mapping.init,
      });
    }
    return {
      entries,
      paramAssign: {
        X: moduleRef.ParamAssign.X,
        Y: moduleRef.ParamAssign.Y,
        Depth: moduleRef.ParamAssign.Depth,
      },
    };
  }

  function setParam(index, value) {
    const audioParameter = wasmProcessor?.parameters.get(`${index}`);
    if (audioParameter) {
      audioParameter.value = value;
    }
  }

  function setMasterVolume(value) {
    if (!masterVolumeNode || !audioContext) {
      return;
    }
    const clamped = Math.min(Math.max(value, 0), 1);
    masterVolumeNode.gain.linearRampToValueAtTime(
      clamped,
      audioContext.currentTime + 0.1,
    );
  }

  function setBpm(value) {
    const moduleRef = window.Module;
    if (typeof moduleRef?.fx_set_bpm === "function") {
      moduleRef.fx_set_bpm(value);
    }
    drySourceEngine?.setBpm(value);
  }

  function readScopeSnapshot() {
    if (!analyser || !scopeTimeDomainBuffer || !scopeFrequencyBuffer) {
      return null;
    }
    analyser.getFloatTimeDomainData(scopeTimeDomainBuffer);
    analyser.getFloatFrequencyData(scopeFrequencyBuffer);
    return {
      timeDomain: scopeTimeDomainBuffer.slice(),
      frequency: scopeFrequencyBuffer.slice(),
      sampleRate: audioContext?.sampleRate ?? 44100,
      referenceFrequency,
    };
  }

  function resumeAudio() {
    if (!audioContext) {
      return false;
    }
    if (audioContext.state !== "running") {
      audioContext.resume();
    }
    return audioContext.state !== "suspended";
  }

  function unlockAudioSession() {
    const AudioContextClass = window.AudioContext || window.webkitAudioContext;
    if (!AudioContextClass) {
      return;
    }
    const unlockContext = new AudioContextClass();
    try {
      const silentBuffer = unlockContext.createBuffer(1, 1, unlockContext.sampleRate);
      const silentSource = unlockContext.createBufferSource();
      silentSource.buffer = silentBuffer;
      silentSource.connect(unlockContext.destination);
      silentSource.start(0);
    } catch {
      // Some browsers reject zero-length schedules; resume alone is still useful.
    }
    if (unlockContext.state === "suspended") {
      void unlockContext.resume();
    }
    window.setTimeout(() => {
      if (unlockContext.state !== "closed") {
        void unlockContext.close();
      }
    }, 1000);
    log("info", "Audio session unlocked");
  }

  function disarmGestureStart() {
    if (!gestureListener) {
      gestureFinish = null;
      return;
    }
    document.removeEventListener("pointerdown", gestureListener);
    document.removeEventListener("keydown", gestureListener);
    gestureListener = null;
    gestureFinish = null;
  }

  function armGestureStart(onGestureDone) {
    disarmGestureStart();
    gestureFinish = onGestureDone;

    gestureListener = (event) => {
      if (event.type === "keydown" && event.key !== "Enter" && event.key !== " ") {
        return;
      }
      if (event.type === "keydown") {
        event.preventDefault();
      }

      log("info", "Start gesture received in preview iframe", {
        type: event.type,
        pointerType: event.pointerType || null,
      });
      unlockAudioSession();
      if (!startMainFromGesture()) {
        // Keep listeners armed — a premature tap must not consume the gesture.
        log("warn", "Tap ignored — wasm not ready yet");
        return;
      }
      const finish = gestureFinish;
      disarmGestureStart();
      finish?.();
    };

    document.addEventListener("pointerdown", gestureListener);
    document.addEventListener("keydown", gestureListener);
  }

  function startMainFromGesture() {
    const moduleRef = window.Module;
    if (!moduleRef?.calledRun || typeof moduleRef._main !== "function") {
      log("info", "Wasm runtime not ready for main()");
      return false;
    }
    if (mainStarted || moduleRef.__previewMainStarted) {
      return true;
    }

    // AudioWorklet bootstrap begins here — start the wait clock now, not at load.
    restartAudioWaiterTimeout();
    if (!window.crossOriginIsolated) {
      log(
        "warn",
        "crossOriginIsolated is false — reload the page once if preview stays silent",
      );
    }

    moduleRef.__previewMainStarted = true;
    try {
      log("info", "Calling Module._main(0, 0) from iframe user gesture");
      moduleRef._main(0, 0);
      mainStarted = true;
      return true;
    } catch (error) {
      if (isEmscriptenControlFlow(error)) {
        mainStarted = true;
        log("info", "Module._main resumed async audio init (unwind)");
        return true;
      }
      moduleRef.__previewMainStarted = false;
      clearAudioWaiterTimeout();
      log("error", "Module._main(0, 0) failed", error?.message || String(error));
      return false;
    }
  }

  function setGate(open) {
    if (!envelope || !audioContext) {
      return;
    }
    envelope.gain.cancelAndHoldAtTime(audioContext.currentTime);
    envelope.gain.linearRampToValueAtTime(
      open ? 1.0 : 0.0,
      audioContext.currentTime + AHR_ENVELOPE_TIME,
    );
  }

  window.__previewHost = {
    async configureAndLoad({
      wasmHref,
      layoutName,
      dryInput,
      drySource,
      dryPlaying,
      target,
      deferMain,
    }) {
      layout = layoutName;
      usesDryInput = Boolean(dryInput);
      initialDrySource = drySource || "house";
      initialDryPlaying = Boolean(dryPlaying);
      targetName = target || "";
      runtimeReady = false;
      mainStarted = false;
      const baseUrl = wasmBaseUrl(wasmHref);
      const cacheToken = String(Date.now());
      const jsUrl = withCacheToken(wasmJsUrl(wasmHref), cacheToken);
      // When main is deferred until a tap, do not arm the AudioWorklet timeout yet.
      const audioReady = createAudioWaiter({ armTimeout: !deferMain });
      // Avoid an unhandled rejection if the waiter fails before mount awaits it.
      void audioReady.catch(() => {});

      const moduleConfig = {
        locateFile: (path) => withCacheToken(new URL(path, baseUrl).href, cacheToken),
        mainScriptUrlOrBlob: jsUrl,
        noInitialRun: deferMain,
        onAudioReady,
        printErr: (message) => log("error", `[wasm] ${message}`),
        onRuntimeInitialized: () => {
          runtimeReady = true;
          log("info", "Wasm runtime initialized");
          runtimeWaiter?.();
          runtimeWaiter = null;
        },
      };
      window.Module = moduleConfig;
      window.setupWebAudioAndUI = onAudioReady;
      installWorkletModuleBase(baseUrl, cacheToken);
      log("info", "Configured wasm module", {
        wasm: wasmHref,
        cacheToken,
        crossOriginIsolated: window.crossOriginIsolated,
        hasAudioContext: typeof AudioContext !== "undefined" || typeof webkitAudioContext !== "undefined",
        deferMain,
      });

      try {
        await loadWasmScript(jsUrl);
        await waitForRuntime();
      } catch (error) {
        rejectAudioWaiter(error);
        throw error;
      }
      if (!deferMain) {
        mainStarted = true;
      }
      return { audioReady };
    },

    armGestureStart,
    disarmGestureStart,

    startMain() {
      return startMainFromGesture();
    },

    readKnobs,
    formatKnobValue,
    readMappings,
    setParam,
    setMasterVolume,
    setBpm,
    setDrySource(sourceId) {
      drySourceEngine?.setSource(sourceId);
    },
    setDryPlaying(playing) {
      drySourceEngine?.setPlaying(playing);
    },
    readScopeSnapshot,
    applyCurve(normalized, curve, unipolar) {
      return window.Module.applyCurveToParameter0to1(normalized, curve, unipolar);
    },
    resumeAudio,
    suspendAudio() {
      audioContext?.suspend();
    },
    audioState() {
      return audioContext?.state || "closed";
    },
    setOscPitch(frequency) {
      referenceFrequency = frequency;
      window.Module.setOscPitch(frequency);
    },
    noteOn(note, velocity) {
      window.Module.noteOn(note, velocity);
    },
    noteOff(note) {
      window.Module.noteOff(note);
    },
    setGate,
    touchBegan(xNormalized, yNormalized) {
      resumeAudio();
      window.Module.touchEvent(window.Module.TouchEvent.Began, xNormalized, yNormalized);
    },
    touchMoved(xNormalized, yNormalized) {
      window.Module.touchEvent(window.Module.TouchEvent.Moved, xNormalized, yNormalized);
    },
    touchEnded(xNormalized, yNormalized) {
      window.Module.touchEvent(window.Module.TouchEvent.Ended, xNormalized, yNormalized);
    },
  };
})();
