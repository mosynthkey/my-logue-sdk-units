<script setup>
import { ref } from "vue";
import {
  enablePreviewDebugLog,
  previewDebugLog,
  usePreviewDebugLog,
} from "../../composables/usePreviewDebugLog.js";

const { lines, visible, copyLog } = usePreviewDebugLog();
const copied = ref(false);
const copyFailed = ref(false);

async function onCopy() {
  copied.value = false;
  copyFailed.value = false;
  try {
    const ok = await copyLog();
    if (ok) {
      copied.value = true;
      window.setTimeout(() => {
        copied.value = false;
      }, 2000);
      return;
    }
    copyFailed.value = true;
  } catch (error) {
    copyFailed.value = true;
    previewDebugLog("error", "Copy failed", error);
  }
}

function onEnable() {
  enablePreviewDebugLog("panel");
}
</script>

<template>
  <div
    v-if="visible"
    class="preview-debug"
  >
    <header class="preview-debug__head">
      <h3>Preview debug log</h3>
      <div class="preview-debug__actions">
        <v-btn
          v-if="lines.length === 0"
          variant="tonal"
          size="small"
          @click="onEnable"
        >
          Refresh
        </v-btn>
        <v-btn
          variant="tonal"
          size="small"
          prepend-icon="mdi-content-copy"
          :disabled="lines.length === 0"
          @click="onCopy"
        >
          {{ copied ? "Copied" : copyFailed ? "Copy failed — select text" : "Copy log" }}
        </v-btn>
      </div>
    </header>
    <p
      v-if="lines.length === 0"
      class="preview-debug__empty"
    >
      Waiting for preview events. On iOS open with <code>?previewDebug</code>, tap Start, then Copy log.
    </p>
    <pre
      v-else
      class="preview-debug__body"
    ><code
      v-for="(line, lineIndex) in lines"
      :key="lineIndex"
      class="preview-debug__line"
      :class="`preview-debug__line--${line.kind}`"
    >{{ line.at }} [{{ line.kind }}] {{ line.message }}
</code></pre>
  </div>
</template>
