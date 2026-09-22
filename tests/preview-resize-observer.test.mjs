import assert from "node:assert/strict";
import test from "node:test";
import {
  isBenignResizeObserverMessage,
} from "../website/src/preview/observeSize.js";

test("benign ResizeObserver browser warnings are recognized", () => {
  assert.equal(
    isBenignResizeObserverMessage("ResizeObserver loop completed with undelivered notifications."),
    true,
  );
  assert.equal(
    isBenignResizeObserverMessage("ResizeObserver loop limit exceeded"),
    true,
  );
  assert.equal(
    isBenignResizeObserverMessage("Preview timed out waiting for AudioWorklet."),
    false,
  );
});
