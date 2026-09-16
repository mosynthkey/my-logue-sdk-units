import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";

const runtimePath = path.join(
  path.dirname(fileURLToPath(import.meta.url)),
  "../website/public/preview-runtime.js",
);

function withCacheToken(url, cacheToken) {
  const resolved = new URL(url, "https://example.com/preview/");
  resolved.searchParams.set("v", cacheToken);
  return resolved.href;
}

test("preview locateFile and AudioWorklet.addModule share one cache token with the glue JS", () => {
  const source = fs.readFileSync(runtimePath, "utf8");

  assert.match(source, /function withCacheToken\(/);
  assert.match(
    source,
    /locateFile:\s*\(path\)\s*=>\s*withCacheToken\(new URL\(path,\s*baseUrl\)\.href,\s*cacheToken\)/,
  );
  assert.match(source, /installWorkletModuleBase\(baseUrl,\s*cacheToken\)/);
  assert.match(source, /moduleURL = withCacheToken\(moduleURL,\s*cacheToken\)/);
  assert.doesNotMatch(
    source,
    /locateFile:\s*\(path\)\s*=>\s*new URL\(path,\s*baseUrl\)\.href\s*,/,
  );
});

test("withCacheToken applies the same v= to js wasm and aw.js", () => {
  const token = "1789556583208";
  const base = "https://example.com/sim/dubthrow/nts-3_kaoss/";
  assert.equal(
    withCacheToken(`${base}dubthrow.js`, token),
    `${base}dubthrow.js?v=${token}`,
  );
  assert.equal(
    withCacheToken(`${base}dubthrow.wasm`, token),
    `${base}dubthrow.wasm?v=${token}`,
  );
  assert.equal(
    withCacheToken(`${base}dubthrow.aw.js`, token),
    `${base}dubthrow.aw.js?v=${token}`,
  );
});
