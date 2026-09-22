<script setup>
import { computed, onBeforeUnmount, ref, watch } from "vue";
import PreviewDebugLog from "./preview/PreviewDebugLog.vue";
import PreviewDepthPad from "./preview/PreviewDepthPad.vue";
import PreviewKeyboard from "./preview/PreviewKeyboard.vue";
import PreviewKnob from "./preview/PreviewKnob.vue";
import PreviewScope from "./preview/PreviewScope.vue";
import PreviewXyPad from "./preview/PreviewXyPad.vue";
import { useWasmPreview } from "../preview/useWasmPreview.js";
import {
  enablePreviewDebugLog,
  usePreviewDebugLog,
} from "../composables/usePreviewDebugLog.js";
import { useI18n } from "../composables/useI18n.js";

const props = defineProps({
  build: {
    type: Object,
    default: null,
  },
  plugin: {
    type: Object,
    default: null,
  },
});
const { t } = useI18n();
const { enabled: previewDebugEnabled } = usePreviewDebugLog();

const xyPadRef = ref(null);

const previewShellRef = ref(null);

const {
  message,
  layout,
  knobs,
  showInstrument,
  showKnobs,
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
  drySources,
  drySourceId,
  dryPlaying,
  isReady,
  mount,
  teardown,
  setKnobValue,
  setMasterVolume,
  setBpm,
  setDrySource,
  toggleDryPlayback,
  handleDepthChange,
  readScopeSnapshot,
  onKeyboardDown,
  onKeyboardUp,
  onLatchToggle,
  onTouchBegan,
  onTouchMoved,
  onTouchEnded,
  onHoldToggle,
  startPreviewFromTap,
} = useWasmPreview(previewShellRef);

const drySourceItems = computed(() =>
  drySources.map((source) => ({
    value: source.id,
    title: t(source.labelKey),
  })),
);

watch(
  () => [props.build, props.plugin],
  ([build, plugin]) => {
    mount(build, plugin);
  },
  { immediate: true },
);

onBeforeUnmount(() => {
  teardown();
});

function onKnobUpdate(knobIndex, nextValue) {
  setKnobValue(knobIndex, nextValue, !knobs.value[knobIndex]?.placeholder);
}

function onXyPointerDown(position) {
  onTouchBegan(position.xNormalized, position.yNormalized);
}

function onXyPointerMove(position) {
  onTouchMoved(position.xNormalized, position.yNormalized);
}

function onXyPointerUp(position) {
  onTouchEnded(position.xNormalized, position.yNormalized);
}

function handleHoldToggle() {
  const lastPointer = xyPadRef.value?.lastPointer?.value ?? null;
  onHoldToggle(lastPointer);
}

function showPreviewDebug() {
  enablePreviewDebugLog("tap-to-start-ui");
}
</script>

<template>
  <section class="detail__stage" :aria-label="t('preview')">
    <div ref="previewShellRef" class="preview-shell">
      <p
        v-if="!showInstrument && message"
        class="preview-status"
        :class="{ 'preview-status--action': awaitingWasmTap }"
        :role="awaitingWasmTap ? 'button' : undefined"
        :tabindex="awaitingWasmTap ? 0 : -1"
        @keydown.enter.prevent="startPreviewFromTap"
        @keydown.space.prevent="startPreviewFromTap"
      >
        {{ message }}
      </p>

      <div
        v-if="showInstrument"
        class="preview-toolbar"
      >
        <div class="preview-toolbar__controls">
          <div
            v-if="showDryInput"
            class="preview-dry-input"
          >
            <v-btn
              variant="tonal"
              :color="dryPlaying ? 'primary' : undefined"
              :prepend-icon="dryPlaying ? 'mdi-stop' : 'mdi-play'"
              :aria-pressed="dryPlaying"
              :aria-label="t(dryPlaying ? 'stop' : 'play')"
              :disabled="!isReady"
              @click="toggleDryPlayback"
            >
              {{ t(dryPlaying ? "stop" : "play") }}
            </v-btn>
            <v-select
              class="preview-dry-input__select"
              :model-value="drySourceId"
              :items="drySourceItems"
              item-title="title"
              item-value="value"
              density="compact"
              hide-details
              :aria-label="t('selectInputSource')"
              :disabled="!isReady"
              @update:model-value="setDrySource"
            />
          </div>

          <v-btn
            v-if="showInstrument && layout === 'keyboard' && !kickDemoActive"
            variant="tonal"
            :color="latchEnabled ? 'primary' : undefined"
            :aria-pressed="latchEnabled"
            @click="onLatchToggle"
          >
            {{ t("latch") }} {{ t(latchEnabled ? "on" : "off") }}
          </v-btn>

          <v-btn
            v-if="showInstrument && layout === 'xypad'"
            variant="tonal"
            :color="holdEnabled ? 'primary' : undefined"
            :aria-pressed="holdEnabled"
            @click="handleHoldToggle"
          >
            {{ t("hold") }} {{ t(holdEnabled ? "on" : "off") }}
          </v-btn>
        </div>
      </div>

      <div v-if="showInstrument" class="preview-instrument">
        <div class="preview-instrument-row">
          <div
            v-if="layout === 'keyboard' && !kickDemoActive"
            class="preview-instrument-main"
          >
            <PreviewKeyboard
              :enabled="isReady"
              @note-down="onKeyboardDown"
              @note-up="onKeyboardUp"
            />
          </div>

          <div
            v-else
            class="preview-xypad-group"
          >
            <PreviewDepthPad
              v-if="hasDepthMapping"
              :depth-normalized="depthNormalized"
              @update:depth="handleDepthChange"
            />

            <PreviewXyPad
              ref="xyPadRef"
              :hold-enabled="holdEnabled"
              @pointer-down="onXyPointerDown"
              @pointer-move="onXyPointerMove"
              @pointer-up="onXyPointerUp"
            />
          </div>

          <PreviewScope
            :enabled="isReady"
            :read-snapshot="readScopeSnapshot"
          />
        </div>
      </div>

      <div v-if="showInstrument" class="preview-knobs">
        <PreviewKnob
          knob-id="master-volume"
          name="[Volume]"
          :min="0"
          :max="1"
          :value="masterVolume"
          :value-label="masterVolumeLabel"
          :input-value="masterVolume * 100"
          :input-min="0"
          :input-max="100"
          @update:value="setMasterVolume"
        />

        <PreviewKnob
          knob-id="master-bpm"
          name="[BPM]"
          :min="30"
          :max="240"
          :value="bpm"
          :value-label="bpmLabel"
          @update:value="setBpm"
        />

        <PreviewKnob
          v-for="(knob, knobIndex) in knobs"
          :key="`${knob.name}-${knobIndex}`"
          :knob-id="`knob-${knobIndex}`"
          :name="knob.name"
          :min="knob.min"
          :max="knob.max"
          :value="knob.value"
          :value-label="knob.valueLabel"
          :placeholder="knob.placeholder"
          @update:value="onKnobUpdate(knobIndex, $event)"
        />
      </div>

    </div>

    <!-- Keep debug UI outside the gesture-capture shell so the iframe cannot swallow taps. -->
    <p
      v-if="awaitingWasmTap && !previewDebugEnabled"
      class="preview-debug-hint"
    >
      <button
        type="button"
        class="preview-debug-hint__button"
        @click="showPreviewDebug"
      >
        Show preview debug log
      </button>
      <span class="preview-debug-hint__or">or open with</span>
      <code>?previewDebug</code>
    </p>

    <PreviewDebugLog />
  </section>
</template>
