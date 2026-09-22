<script setup>
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from "vue";
import { useI18n } from "../../composables/useI18n.js";
import { observeElementSize } from "../../preview/observeSize.js";

const props = defineProps({
  enabled: {
    type: Boolean,
    default: false,
  },
});
const { t } = useI18n();

const emit = defineEmits(["note-down", "note-up"]);

const MIDI_MIN = 0;
const MIDI_MAX = 127;
const MIDI_C3 = 48;
const PAD_SIZE = 56;
const PITCH_CLASS_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
const BLACK_PITCH_CLASSES = new Set([1, 3, 6, 8, 10]);
const TYPING_OFFSETS = {
  KeyA: 0,
  KeyW: 1,
  KeyS: 2,
  KeyE: 3,
  KeyD: 4,
  KeyF: 5,
  KeyT: 6,
  KeyG: 7,
  KeyY: 8,
  KeyH: 9,
  KeyU: 10,
  KeyJ: 11,
  KeyK: 12,
  KeyO: 13,
  KeyL: 14,
  KeyP: 15,
};

const wrapEl = ref(null);
const viewMidi = ref(MIDI_C3);
const activeMidis = ref(new Set());
const canOctaveDown = ref(false);
const canOctaveUp = ref(false);

let pointerId = null;
let pointerMidi = null;
let pointerX = 0;
let pointerY = 0;
let edgeScrollFrame = 0;
let stopObservingSize = null;
let didInitialScroll = false;
const typingMidis = new Map();

const keys = buildKeys();
const whiteKeyCount = keys.filter((key) => key.color === "white").length;
const viewLabel = computed(() => midiName(viewMidi.value));

function buildKeys() {
  const built = [];
  let whiteColumn = 0;
  for (let midi = MIDI_MIN; midi <= MIDI_MAX; midi += 1) {
    const pitchClass = midi % 12;
    const isBlack = BLACK_PITCH_CLASSES.has(pitchClass);
    if (!isBlack) {
      whiteColumn += 1;
    }
    built.push({
      midi,
      name: midiName(midi),
      color: isBlack ? "black" : "white",
      column: whiteColumn,
      row: isBlack ? 1 : 2,
      isC: pitchClass === 0,
    });
  }
  return built;
}

function midiName(midi) {
  const pitchClass = ((midi % 12) + 12) % 12;
  const octave = Math.floor(midi / 12) - 1;
  return `${PITCH_CLASS_NAMES[pitchClass]}${octave}`;
}

function frequencyFromMidi(midi) {
  return 440 * (2 ** ((midi - 69) / 12));
}

function isTypingTarget(event) {
  const target = event.target;
  if (!(target instanceof HTMLElement)) {
    return false;
  }
  return (
    target.closest(
      "input, textarea, select, [contenteditable='true'], .v-field, [role='combobox']",
    ) !== null
  );
}

function noteOn(midi) {
  if (activeMidis.value.has(midi)) {
    return;
  }
  const nextActive = new Set(activeMidis.value);
  nextActive.add(midi);
  activeMidis.value = nextActive;
  emit("note-down", midi, frequencyFromMidi(midi));
}

function noteOff(midi) {
  if (!activeMidis.value.has(midi)) {
    return;
  }
  const nextActive = new Set(activeMidis.value);
  nextActive.delete(midi);
  activeMidis.value = nextActive;
  emit("note-up", midi, frequencyFromMidi(midi));
}

function keyFromPoint(clientX, clientY) {
  const hit = document.elementFromPoint(clientX, clientY);
  if (!(hit instanceof Element)) {
    return null;
  }
  const keyEl = hit.closest("[data-midi]");
  if (!keyEl) {
    return null;
  }
  return Number.parseInt(keyEl.getAttribute("data-midi"), 10);
}

function midiFromEvent(event) {
  if (event.target instanceof Element) {
    const fromTarget = event.target.closest("[data-midi]");
    if (fromTarget) {
      return Number.parseInt(fromTarget.getAttribute("data-midi"), 10);
    }
  }
  return keyFromPoint(event.clientX, event.clientY);
}

function playMidi(midi) {
  if (!Number.isInteger(midi) || midi === pointerMidi) {
    return;
  }
  if (pointerMidi !== null) {
    noteOff(pointerMidi);
  }
  pointerMidi = midi;
  noteOn(midi);
}

function playAtPoint(clientX, clientY) {
  playMidi(keyFromPoint(clientX, clientY));
}

function stopEdgeScroll() {
  if (edgeScrollFrame) {
    cancelAnimationFrame(edgeScrollFrame);
    edgeScrollFrame = 0;
  }
}

function edgeScrollLoop() {
  const wrap = wrapEl.value;
  if (!wrap || pointerId === null) {
    edgeScrollFrame = 0;
    return;
  }

  const box = wrap.getBoundingClientRect();
  const edge = PAD_SIZE;
  let scrollDelta = 0;
  if (pointerX < box.left + edge) {
    scrollDelta = -Math.max(2, Math.ceil((edge - (pointerX - box.left)) / 3));
  } else if (pointerX > box.right - edge) {
    scrollDelta = Math.max(2, Math.ceil((edge - (box.right - pointerX)) / 3));
  }

  if (scrollDelta !== 0) {
    wrap.scrollLeft = Math.min(
      wrap.scrollWidth - wrap.clientWidth,
      Math.max(0, wrap.scrollLeft + scrollDelta),
    );
    playAtPoint(pointerX, pointerY);
  }

  edgeScrollFrame = requestAnimationFrame(edgeScrollLoop);
}

function onPointerDown(event) {
  if (!props.enabled || event.button !== 0) {
    return;
  }
  const midi = midiFromEvent(event);
  if (!Number.isInteger(midi)) {
    return;
  }
  event.preventDefault();
  wrapEl.value?.setPointerCapture(event.pointerId);
  pointerId = event.pointerId;
  pointerX = event.clientX;
  pointerY = event.clientY;
  playMidi(midi);
  stopEdgeScroll();
  edgeScrollFrame = requestAnimationFrame(edgeScrollLoop);
}

function onPointerMove(event) {
  if (event.pointerId !== pointerId) {
    return;
  }
  event.preventDefault();
  pointerX = event.clientX;
  pointerY = event.clientY;
  playMidi(midiFromEvent(event));
}

function onPointerUp(event) {
  if (event.pointerId !== pointerId) {
    return;
  }
  if (wrapEl.value?.hasPointerCapture(event.pointerId)) {
    wrapEl.value.releasePointerCapture(event.pointerId);
  }
  stopEdgeScroll();
  pointerId = null;
  if (pointerMidi !== null) {
    noteOff(pointerMidi);
    pointerMidi = null;
  }
}

function leftmostVisibleMidi() {
  const wrap = wrapEl.value;
  if (!wrap) {
    return MIDI_C3;
  }
  const wrapLeft = wrap.getBoundingClientRect().left;
  const whiteKeys = wrap.querySelectorAll('[data-note-type="white"]');
  for (const keyEl of whiteKeys) {
    if (keyEl.getBoundingClientRect().right > wrapLeft + 8) {
      return Number.parseInt(keyEl.getAttribute("data-midi"), 10);
    }
  }
  return MIDI_MIN;
}

function snapToC(midi) {
  return midi - (midi % 12);
}

function updateOctaveState() {
  const wrap = wrapEl.value;
  if (!wrap) {
    return;
  }
  const currentC = snapToC(leftmostVisibleMidi());
  viewMidi.value = currentC;
  canOctaveDown.value = wrap.scrollLeft > 1;
  canOctaveUp.value = wrap.scrollLeft < wrap.scrollWidth - wrap.clientWidth - 1;
}

function scrollToMidi(midi) {
  const wrap = wrapEl.value;
  const keyEl = wrap?.querySelector(`[data-midi="${midi}"]`);
  if (!wrap || !keyEl) {
    return;
  }
  const wrapBox = wrap.getBoundingClientRect();
  const keyBox = keyEl.getBoundingClientRect();
  const nextLeft = wrap.scrollLeft + (keyBox.left - wrapBox.left);
  wrap.scrollLeft = Math.max(0, Math.min(wrap.scrollWidth - wrap.clientWidth, nextLeft));
  updateOctaveState();
}

function scrollToDefault() {
  const wrap = wrapEl.value;
  if (!wrap || wrap.scrollWidth <= wrap.clientWidth + 1) {
    return;
  }
  didInitialScroll = true;
  scrollToMidi(MIDI_C3);
}

function observeWrap() {
  const wrap = wrapEl.value;
  if (!wrap) {
    scrollToDefault();
    return;
  }
  stopObservingSize?.();
  stopObservingSize = observeElementSize(wrap, () => {
    if (!didInitialScroll) {
      scrollToDefault();
    }
    updateOctaveState();
  });
  scrollToDefault();
}

function shiftOctave(octaveDelta) {
  const nextC = Math.min(120, Math.max(MIDI_MIN, snapToC(leftmostVisibleMidi()) + octaveDelta * 12));
  scrollToMidi(nextC);
}

function onOctaveDown() {
  shiftOctave(-1);
}

function onOctaveUp() {
  shiftOctave(1);
}

function onWrapScroll() {
  updateOctaveState();
}

function onTypingDown(event) {
  if (!props.enabled || isTypingTarget(event) || event.repeat) {
    return;
  }
  if (event.key === "z") {
    event.preventDefault();
    onOctaveDown();
    return;
  }
  if (event.key === "x") {
    event.preventDefault();
    onOctaveUp();
    return;
  }
  const offset = TYPING_OFFSETS[event.code];
  if (offset === undefined) {
    return;
  }
  const midi = snapToC(viewMidi.value) + offset;
  if (midi < MIDI_MIN || midi > MIDI_MAX || typingMidis.has(event.code)) {
    return;
  }
  typingMidis.set(event.code, midi);
  noteOn(midi);
}

function onTypingUp(event) {
  if (event.key === "z" || event.key === "x") {
    return;
  }
  const midi = typingMidis.get(event.code);
  if (!Number.isInteger(midi)) {
    return;
  }
  typingMidis.delete(event.code);
  noteOff(midi);
}

function releaseAllNotes() {
  stopEdgeScroll();
  pointerId = null;
  pointerMidi = null;
  typingMidis.clear();
  for (const midi of activeMidis.value) {
    emit("note-up", midi, frequencyFromMidi(midi));
  }
  activeMidis.value = new Set();
}

watch(() => props.enabled, (enabled) => {
  if (enabled) {
    didInitialScroll = false;
    nextTick(observeWrap);
  } else {
    releaseAllNotes();
  }
});

onMounted(() => {
  document.addEventListener("keydown", onTypingDown);
  document.addEventListener("keyup", onTypingUp);
  if (props.enabled) {
    observeWrap();
  }
});

onBeforeUnmount(() => {
  document.removeEventListener("keydown", onTypingDown);
  document.removeEventListener("keyup", onTypingUp);
  stopObservingSize?.();
  stopObservingSize = null;
  releaseAllNotes();
});
</script>

<template>
  <div class="preview-keyboard-shell">
    <div class="preview-keyboard-head">
      <v-btn
        variant="tonal"
        :disabled="!canOctaveDown"
        :aria-label="t('octaveDown')"
        @click="onOctaveDown"
      >
        Octave −
      </v-btn>
      <span
        class="preview-octave"
        aria-live="polite"
      >{{ viewLabel }}</span>
      <v-btn
        variant="tonal"
        :disabled="!canOctaveUp"
        :aria-label="t('octaveUp')"
        @click="onOctaveUp"
      >
        Octave +
      </v-btn>
    </div>

    <div
      ref="wrapEl"
      class="preview-keyboard-wrap preview-keyboard-wrap--mk2"
      @scroll="onWrapScroll"
      @pointerdown="onPointerDown"
      @pointermove="onPointerMove"
      @pointerup="onPointerUp"
      @pointercancel="onPointerUp"
      @contextmenu.prevent
      @dragstart.prevent
    >
      <div
        id="preview-keyboard"
        class="preview-keyboard-keys"
        role="list"
        aria-label="MIDI keyboard"
        :style="{ '--pad-columns': String(whiteKeyCount) }"
      >
        <div
          v-for="key in keys"
          :key="key.midi"
          role="listitem"
          class="preview-key"
          :class="[
            `preview-key--${key.color}`,
            { 'is-active': activeMidis.has(key.midi) },
          ]"
          :data-note-type="key.color"
          :data-midi="key.midi"
          :title="key.name"
          :style="{ gridColumn: String(key.column), gridRow: String(key.row) }"
        >
          <span
            v-if="key.isC"
            class="preview-key__name"
          >{{ key.name }}</span>
        </div>
      </div>
    </div>
  </div>
</template>
