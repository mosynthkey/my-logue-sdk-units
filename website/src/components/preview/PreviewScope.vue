<script setup>
import { onBeforeUnmount, onMounted, ref, watch } from "vue";
import { paintFrequencyScope, paintTimeDomainScope } from "../../preview/scope.js";

const props = defineProps({
  enabled: {
    type: Boolean,
    default: false,
  },
  readSnapshot: {
    type: Function,
    required: true,
  },
});

const timeCanvasEl = ref(null);
const frequencyCanvasEl = ref(null);
let animationFrame = 0;

function drawFrame() {
  const snapshot = props.readSnapshot();
  if (snapshot && timeCanvasEl.value && frequencyCanvasEl.value) {
    paintTimeDomainScope(
      timeCanvasEl.value,
      snapshot.timeDomain,
      snapshot.referenceFrequency,
      snapshot.sampleRate,
    );
    paintFrequencyScope(frequencyCanvasEl.value, snapshot.frequency);
  }
  animationFrame = window.requestAnimationFrame(drawFrame);
}

function startLoop() {
  stopLoop();
  animationFrame = window.requestAnimationFrame(drawFrame);
}

function stopLoop() {
  if (animationFrame) {
    window.cancelAnimationFrame(animationFrame);
    animationFrame = 0;
  }
}

watch(() => props.enabled, (enabled) => {
  if (enabled) {
    startLoop();
  } else {
    stopLoop();
  }
});

onMounted(() => {
  // Continuous rAF already remeasures canvas size each frame — no ResizeObserver.
  if (props.enabled) {
    startLoop();
  }
});

onBeforeUnmount(() => {
  stopLoop();
});
</script>

<template>
  <div class="preview-scope" aria-label="Scope">
    <canvas
      ref="timeCanvasEl"
      class="preview-scope__canvas preview-scope__canvas--wave"
      aria-hidden="true"
    />
    <canvas
      ref="frequencyCanvasEl"
      class="preview-scope__canvas preview-scope__canvas--fft"
      aria-hidden="true"
    />
  </div>
</template>
