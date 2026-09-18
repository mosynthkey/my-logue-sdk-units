import assert from "node:assert/strict";
import test from "node:test";
import {
  PLUGIN_CATEGORIES,
  categoryMessageKey,
  filterPluginsByCategory,
  normalizeCategory,
  pluginCategory,
} from "../website/src/utils/pluginCategory.js";

test("pluginCategory maps plugin types onto the four catalog groups", () => {
  assert.equal(pluginCategory({ type: "osc" }), "oscillator");
  assert.equal(pluginCategory({ type: "loopkey" }), "oscillator");
  assert.equal(pluginCategory({ type: "synth" }), "synth");
  assert.equal(pluginCategory({ type: "fm" }), "synth");
  assert.equal(pluginCategory({ type: "drum" }), "drum");
  assert.equal(pluginCategory({ type: "shaker" }), "drum");
  assert.equal(pluginCategory({ type: "fx" }), "fx");
});

test("pluginCategory prefers an explicit category when it is known", () => {
  assert.equal(pluginCategory({ type: "fx", category: "synth" }), "synth");
  assert.equal(pluginCategory({ type: "osc", category: "unknown" }), "oscillator");
});

test("filterPluginsByCategory keeps every plugin for all", () => {
  const plugins = [
    { id: "hypersaw", type: "osc" },
    { id: "retrig", type: "fx" },
    { id: "shaker", type: "shaker" },
  ];

  assert.deepEqual(
    filterPluginsByCategory(plugins, "all").map((plugin) => plugin.id),
    ["hypersaw", "retrig", "shaker"],
  );
  assert.deepEqual(
    filterPluginsByCategory(plugins, "fx").map((plugin) => plugin.id),
    ["retrig"],
  );
  assert.deepEqual(
    filterPluginsByCategory(plugins, "drum").map((plugin) => plugin.id),
    ["shaker"],
  );
});

test("normalizeCategory rejects unknown query values", () => {
  assert.equal(normalizeCategory("fx"), "fx");
  assert.equal(normalizeCategory("all"), "all");
  assert.equal(normalizeCategory("nope"), "all");
  assert.equal(normalizeCategory(undefined), "all");
});

test("category labels cover the filter chips", () => {
  assert.equal(categoryMessageKey("all"), "categoryAll");
  for (const category of PLUGIN_CATEGORIES) {
    assert.equal(typeof categoryMessageKey(category), "string");
  }
});
