import { reactive, ref } from "vue";

export const ROUTING_OPTIONS = [
  { id: "serial", label: "SERIAL", hint: "1→2→3→4" },
  { id: "serpar", label: "SERPAR", hint: "1→2→(3∥4)" },
  { id: "parser", label: "PARSER", hint: "(1∥2)→3→4" },
  { id: "1para", label: "1 PARA", hint: "(1→2→3)∥4" },
  { id: "2by2", label: "2 BY 2", hint: "(1→2)∥(3→4)" },
  { id: "3to1", label: "3 TO 1", hint: "(1∥2∥3)→4" },
];

export const CURVE_OPTIONS = [
  "LINEAR",
  "EXP",
  "LOG",
  "TOGGLE",
  "MINCLP",
  "MAXCLP",
];

export const ASSIGN_OPTIONS = ["NONE", "X", "Y", "DEPTH"];

export const INPUT_SELECT_OPTIONS = ["STEREO", "MID", "SIDE"];

export const RELEASE_MODE_OPTIONS = ["THRU", "SILENT"];

export const SLOT_FX_OPTIONS = [
  "00: Thru",
  "01: Filter",
  "02: Distortion",
  "35: StepGrain",
];

const DEFAULT_PARAM_NAMES = [
  "FEEL",
  "OCT",
  "MIX",
  "MODE",
  "STEPS",
  "ENV",
  "SPRD",
  "REVS",
];

// Mirrors plugins/stepgrain/targets/nts-3_kaoss/header.c defaults.
const STEP_GRAIN_PARAM_DEFAULTS = {
  FEEL: { assign: "X", value: 1023, min: 0, max: 1023 },
  OCT: { assign: "Y", value: 512, min: 0, max: 1023 },
  MIX: { assign: "DEPTH", value: 100, min: 0, max: 100 },
  MODE: { assign: "NONE", value: 1, min: 0, max: 1 },
  STEPS: { assign: "NONE", value: 6, min: 0, max: 7 },
  ENV: { assign: "NONE", value: 4, min: 0, max: 4 },
  SPRD: { assign: "NONE", value: 100, min: 0, max: 100 },
  REVS: { assign: "NONE", value: 50, min: 0, max: 100 },
};

function createParam(name, defaults = {}) {
  return {
    name,
    value: defaults.value ?? 512,
    min: defaults.min ?? 0,
    max: defaults.max ?? 1023,
    curve: defaults.curve ?? 0,
    polarity: defaults.polarity ?? "uni",
    assign: defaults.assign ?? "NONE",
  };
}

function createStepGrainParams() {
  return DEFAULT_PARAM_NAMES.map((name) =>
    createParam(name, STEP_GRAIN_PARAM_DEFAULTS[name] ?? {}),
  );
}

function createSlot(fxLabel) {
  return {
    on: true,
    fxIndex: SLOT_FX_OPTIONS.indexOf(fxLabel),
    inputSelect: 0,
    releaseMode: 0,
    releaseTime: 0,
    outGain: 512,
    xyFreeze: false,
    depthFreeze: false,
    depth: 100,
    x: 1023,
    y: 512,
    params: createStepGrainParams(),
  };
}

function createProgramState() {
  return {
    routing: "serial",
    activeSlot: 0,
    slots: [
      createSlot("35: StepGrain"),
      createSlot("00: Thru"),
      createSlot("00: Thru"),
      createSlot("00: Thru"),
    ],
  };
}

export function useProgramEditor() {
  const isOpen = ref(false);
  const program = reactive(createProgramState());

  function openEditor() {
    isOpen.value = true;
  }

  function closeEditor() {
    isOpen.value = false;
  }

  function selectSlot(slotIndex) {
    program.activeSlot = slotIndex;
  }

  function activeSlot() {
    return program.slots[program.activeSlot];
  }

  function clampParamRange(value, min = 0, max = 1023) {
    return Math.max(min, Math.min(max, Math.round(value)));
  }

  function updateParam(paramIndex, patch) {
    const slot = activeSlot();
    const param = slot.params[paramIndex];
    Object.assign(param, patch);
    param.min = clampParamRange(param.min, 0, 1023);
    param.max = clampParamRange(param.max, param.min, 1023);
    param.value = clampParamRange(param.value, param.min, param.max);
  }

  function clearActiveSlot() {
    const slot = activeSlot();
    slot.on = true;
    slot.fxIndex = 0;
    slot.inputSelect = 0;
    slot.releaseMode = 0;
    slot.releaseTime = 0;
    slot.outGain = 512;
    slot.xyFreeze = false;
    slot.depthFreeze = false;
    slot.depth = 100;
    slot.x = 1023;
    slot.y = 512;
    slot.params = createStepGrainParams();
  }

  return {
    isOpen,
    program,
    openEditor,
    closeEditor,
    selectSlot,
    activeSlot,
    updateParam,
    clearActiveSlot,
    clampParamRange,
  };
}
