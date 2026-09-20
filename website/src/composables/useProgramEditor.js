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
    depth: 512,
    x: 512,
    y: 512,
    params: DEFAULT_PARAM_NAMES.map((name, paramIndex) => {
      if (name === "MIX") {
        return createParam(name, { assign: "DEPTH", value: 512 });
      }
      if (paramIndex === 0) {
        return createParam(name, { assign: "X", value: 600 });
      }
      if (paramIndex === 1) {
        return createParam(name, { assign: "Y", value: 400 });
      }
      return createParam(name);
    }),
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

  function clampParamRange(value) {
    return Math.max(0, Math.min(1023, Math.round(value)));
  }

  function updateParam(paramIndex, patch) {
    const slot = activeSlot();
    const param = slot.params[paramIndex];
    Object.assign(param, patch);
    param.value = clampParamRange(param.value);
    param.min = clampParamRange(param.min);
    param.max = clampParamRange(param.max);
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
    slot.depth = 512;
    slot.x = 512;
    slot.y = 512;
    slot.params = DEFAULT_PARAM_NAMES.map((name) => createParam(name));
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
