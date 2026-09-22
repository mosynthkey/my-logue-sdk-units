// ResizeObserver callbacks that mutate layout (e.g. canvas.width) in the same
// delivery turn trigger "ResizeObserver loop completed with undelivered
// notifications" — a benign browser warning. Defer work to the next frame.
export function observeElementSize(element, onSize) {
  if (!element || typeof ResizeObserver !== "function") {
    return () => {};
  }

  let frameId = 0;
  const observer = new ResizeObserver(() => {
    if (frameId) {
      return;
    }
    frameId = window.requestAnimationFrame(() => {
      frameId = 0;
      onSize();
    });
  });
  observer.observe(element);

  return () => {
    if (frameId) {
      window.cancelAnimationFrame(frameId);
      frameId = 0;
    }
    observer.disconnect();
  };
}

export function isBenignResizeObserverMessage(message) {
  if (typeof message !== "string") {
    return false;
  }
  return message.includes("ResizeObserver loop");
}
