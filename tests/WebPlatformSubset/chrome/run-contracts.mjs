#!/usr/bin/env node
import { spawn, spawnSync } from "node:child_process";
import { existsSync } from "node:fs";
import { createServer } from "node:http";
import { mkdir, mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import process from "node:process";
import { fileURLToPath } from "node:url";
import { CdpClient } from "./cdp-client.mjs";

const delay = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds));
const subsetRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

function parseArguments(values) {
  const paths = [];
  let output = null;
  let timeoutSeconds = 20;
  let deviceScaleFactor = 1;
  for (let index = 0; index < values.length; index++) {
    const name = values[index];
    const value = values[++index];
    if (!value) throw new Error(`Missing value after '${name}'.`);
    if (name === "--path") paths.push(value);
    else if (name === "--output") output = path.resolve(value);
    else if (name === "--timeout-seconds") timeoutSeconds = Number(value);
    else if (name === "--device-scale-factor") deviceScaleFactor = Number(value);
    else throw new Error(`Unknown argument '${name}'.`);
  }
  if (!paths.length) throw new Error("Pass at least one --path relative to tests/WebPlatformSubset.");
  if (!output) throw new Error("Pass --output <new evidence directory>.");
  if (!Number.isFinite(timeoutSeconds) || timeoutSeconds <= 0) {
    throw new Error("--timeout-seconds must be positive.");
  }
  if (!Number.isFinite(deviceScaleFactor) || deviceScaleFactor <= 0) {
    throw new Error("--device-scale-factor must be positive.");
  }
  return { paths, output, timeoutSeconds, deviceScaleFactor };
}

function chromeIdentity() {
  const executable = [
    process.env.CHROME_BIN,
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    "/Applications/Google Chrome Canary.app/Contents/MacOS/Google Chrome Canary",
    "/usr/bin/google-chrome",
    "/usr/bin/chromium"
  ].filter(Boolean).find(existsSync);
  if (!executable) throw new Error("Chrome was not found; set CHROME_BIN.");
  const version = spawnSync(executable, ["--version"], { encoding: "utf8" });
  if (version.status !== 0) throw new Error(version.stderr || "Could not read Chrome version.");
  return { executable, version: version.stdout.trim() };
}

async function launchChrome(executable, deviceScaleFactor) {
  const userDataDirectory = await mkdtemp(path.join(os.tmpdir(), "webscene-wpt-chrome-"));
  const child = spawn(executable, [
    "--headless=new",
    // DeviceMetricsOverride alone changes JS DPR without changing the physical
    // scale used by ResizeObserver.devicePixelContentBoxSize in Chromium.
    `--force-device-scale-factor=${deviceScaleFactor}`,
    "--disable-background-networking",
    "--disable-component-update",
    "--disable-default-apps",
    "--disable-extensions",
    "--disable-features=Translate",
    "--disable-sync",
    "--metrics-recording-only",
    "--no-first-run",
    "--no-default-browser-check",
    "--remote-debugging-port=0",
    `--user-data-dir=${userDataDirectory}`,
    "about:blank"
  ], { stdio: ["ignore", "ignore", "pipe"] });
  const endpoint = await new Promise((resolve, reject) => {
    let stderr = "";
    const timeout = setTimeout(() => reject(new Error("Timed out waiting for Chrome DevTools.")), 20_000);
    child.stderr.setEncoding("utf8");
    child.stderr.on("data", chunk => {
      stderr += chunk;
      const match = stderr.match(/DevTools listening on (ws:\/\/[^\s]+)/);
      if (!match) return;
      clearTimeout(timeout);
      resolve(match[1]);
    });
    child.once("exit", code => {
      clearTimeout(timeout);
      reject(new Error(`Chrome exited before DevTools was ready (${code}).\n${stderr}`));
    });
  });
  const endpointUrl = new URL(endpoint);
  const origin = `http://${endpointUrl.hostname}:${endpointUrl.port}`;
  let target = null;
  for (let attempt = 0; attempt < 100 && !target; attempt++) {
    try {
      const targets = await (await fetch(`${origin}/json/list`)).json();
      target = targets.find(item => item.type === "page" && item.webSocketDebuggerUrl);
    } catch { /* The endpoint can race target publication. */ }
    if (!target) await delay(50);
  }
  if (!target) throw new Error("Chrome exposed no debuggable page target.");
  const client = await CdpClient.connect(target.webSocketDebuggerUrl);
  await client.send("Page.enable");
  await client.send("Runtime.enable");
  return { child, client, userDataDirectory };
}

async function closeChrome(chrome) {
  try { await chrome.client.send("Browser.close", {}, 2_000); }
  catch { chrome.child.kill("SIGTERM"); }
  chrome.client.close();
  await Promise.race([
    new Promise(resolve => chrome.child.once("exit", resolve)),
    delay(2_000).then(() => chrome.child.kill("SIGKILL"))
  ]);
  await rm(chrome.userDataDirectory, { recursive: true, force: true });
}

async function launchContractServer(setViewport) {
  const resourceCacheRequests = new Map();
  const server = createServer(async (request, response) => {
    try {
      const requestUrl = new URL(request.url || "/", "http://127.0.0.1");
      if (requestUrl.pathname === '/__webscene_test_viewport' && request.method === 'POST') {
        const width=Number(requestUrl.searchParams.get('width'));
        const height=Number(requestUrl.searchParams.get('height'));
        if (!Number.isInteger(width) || !Number.isInteger(height)
            || width<1 || height<1 || width>8192 || height>8192) throw new Error('Invalid test viewport');
        await setViewport(width,height);
        response.writeHead(204); response.end(); return;
      }
      if (requestUrl.pathname === '/__webscene_resource_cache_count') {
        const key = requestUrl.searchParams.get('key') || '';
        response.writeHead(200, {
          'content-type':'application/json; charset=utf-8',
          'cache-control':'no-store'
        });
        response.end(JSON.stringify(resourceCacheRequests.get(key)
          || {requests:0, conditional:0}));
        return;
      }
      if (requestUrl.pathname === '/__webscene_resource_cache.js') {
        const key = requestUrl.searchParams.get('key') || '';
        const mode = requestUrl.searchParams.get('mode') || '';
        if (!key || key.length > 128
            || !['fresh', 'validator'].includes(mode)) {
          response.writeHead(400, {'content-type':'text/plain; charset=utf-8'});
          response.end('Invalid resource-cache oracle request');
          return;
        }
        const state = resourceCacheRequests.get(key)
          || {requests:0, conditional:0};
        state.requests++;
        const conditional = request.headers['if-none-match'] === '"webscene-cache-v1"';
        if (conditional) state.conditional++;
        resourceCacheRequests.set(key, state);
        const headers = {
          'content-type':'text/javascript; charset=utf-8',
          'etag':'"webscene-cache-v1"',
          'cache-control':mode === 'validator' ? 'no-cache' : 'public, max-age=3600'
        };
        if (mode === 'validator' && conditional) {
          response.writeHead(304, headers);
          response.end();
          return;
        }
        response.writeHead(200, headers);
        response.end(`globalThis.__resourceCacheExecutions =
          (globalThis.__resourceCacheExecutions || 0) + 1;`);
        return;
      }
      // Local contracts use the native runner's set_viewport extension. Drive
      // a real Chromium viewport change; do not emulate CSS/media results.
      if (requestUrl.pathname === '/resources/testdriver.js') {
        response.writeHead(200, {'content-type':'text/javascript; charset=utf-8'});
        response.end(`globalThis.test_driver={async set_viewport(_element,width,height){
          const response=await fetch('/__webscene_test_viewport?width='+width+'&height='+height,{method:'POST'});
          if(!response.ok) throw new Error('Viewport driver failed');
          await new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
        }};`); return;
      }
      if (['/resources/testdriver-actions.js','/resources/testdriver-vendor.js'].includes(requestUrl.pathname)) {
        response.writeHead(200, {'content-type':'text/javascript; charset=utf-8'});
        response.end('// Only the local set_viewport extension is implemented.'); return;
      }
      let relativePath = decodeURIComponent(requestUrl.pathname).replace(/^\/+/, "");
      // Local WPT-style reductions use the canonical /resources/ URLs. Serve
      // the same pinned harness used by the native runner, not a second copy.
      if (relativePath.startsWith("resources/")) relativePath = `upstream/${relativePath}`;
      const resolved = path.resolve(subsetRoot, relativePath);
      if (!resolved.startsWith(`${subsetRoot}${path.sep}`) || !existsSync(resolved)) {
        response.writeHead(404, { "content-type": "text/plain; charset=utf-8" });
        response.end("Not found");
        return;
      }
      const extension = path.extname(resolved).toLowerCase();
      const contentType = extension === ".html"
        ? "text/html; charset=utf-8"
        : extension === ".js" || extension === ".mjs"
          ? "text/javascript; charset=utf-8"
          : "application/octet-stream";
      response.writeHead(200, {
        "content-type": contentType,
        "cache-control": "no-store"
      });
      let content = await readFile(resolved);
      if (path.extname(resolved).toLowerCase() === ".html") {
        // Existing custom contracts publish __webSceneWptState themselves.
        // Adapt standard WPT result callbacks only for documents which load
        // testharness.js, leaving the actual assertions and harness unchanged.
        content = content.toString("utf8").replace(
          /<script\b[^>]*\bsrc=["']\/resources\/testharness\.js["'][^>]*>\s*<\/script>/i,
          tag => tag + `<script>
            globalThis.__webSceneWptState = {
              complete:false, harness:null, results:[], diagnostics:[]
            };
            add_result_callback(t => __webSceneWptState.results.push({
              name:t.name, status:t.status === 0 ? "PASS" : "FAIL",
              message:t.message, stack:t.stack
            }));
            add_completion_callback((tests, status) => {
              __webSceneWptState.harness = {status:status.status, message:status.message};
              __webSceneWptState.complete = true;
            });
          </script>`);
      }
      response.end(content);
    } catch (error) {
      response.writeHead(500, { "content-type": "text/plain; charset=utf-8" });
      response.end(String(error && error.message || error));
    }
  });
  await new Promise((resolve, reject) => {
    server.once("error", reject);
    server.listen(0, "127.0.0.1", resolve);
  });
  const address = server.address();
  if (!address || typeof address === "string") {
    server.close();
    throw new Error("Contract HTTP server did not expose a TCP address.");
  }
  return {
    server,
    baseUrl: `http://127.0.0.1:${address.port}/`
  };
}

async function closeContractServer(contractServer) {
  await new Promise((resolve, reject) => {
    contractServer.server.close(error => error ? reject(error) : resolve());
  });
}

async function evaluate(client, expression) {
  const response = await client.send("Runtime.evaluate", {
    expression, returnByValue: true, awaitPromise: true
  });
  if (response.exceptionDetails) throw new Error(response.exceptionDetails.text || "Chrome evaluation failed.");
  return response.result.value;
}

async function runDocument(client, baseUrl, relativePath, timeoutSeconds, exceptionState) {
  const started = Date.now();
  exceptionState.current = [];
  // A newly created headless target can have an activeElement while its
  // document is unfocused. Activate the page before testing real :focus CSS.
  await client.send("Page.bringToFront");
  await client.send("Page.navigate", { url: new URL(relativePath, baseUrl).href });
  const deadline = Date.now() + timeoutSeconds * 1000;
  let state = null;
  while (Date.now() < deadline) {
    try {
      state = JSON.parse(await evaluate(client, "JSON.stringify(globalThis.__webSceneWptState || null)"));
      if (state?.complete) break;
    } catch { /* The execution context is replaced during navigation. */ }
    await delay(25);
  }
  if (!state?.complete) {
    return { path: relativePath, status: "TIMEOUT", duration: Date.now() - started,
      message: exceptionState.current.join("\n") || "Chrome contract did not complete.",
      subtests: state?.results || [],
      diagnostics: [...(state?.diagnostics || []), ...exceptionState.current] };
  }
  const subtests = state.results || [];
  const expectedRuntimeExceptions = Array.isArray(state.expectedRuntimeExceptions)
    ? state.expectedRuntimeExceptions.map(String)
    : [];
  const unexpectedRuntimeExceptions = exceptionState.current.filter(message =>
    !expectedRuntimeExceptions.some(expected => String(message).includes(expected)));
  const passed = state.harness?.status === 0
    && subtests.every(result => result.status === "PASS")
    && unexpectedRuntimeExceptions.length === 0;
  return {
    path: relativePath,
    status: passed ? "PASS" : "FAIL",
    duration: Date.now() - started,
    message: passed ? null : state.harness?.message || unexpectedRuntimeExceptions.join("\n") || "Chrome subtest failed.",
    subtests,
    diagnostics: state.diagnostics || []
  };
}

const options = parseArguments(process.argv.slice(2));
if (existsSync(options.output)) {
  throw new Error(`Output '${options.output}' must not already exist; evidence is never overwritten.`);
}
for (const relativePath of options.paths) {
  const resolved = path.resolve(subsetRoot, relativePath);
  if (!resolved.startsWith(`${subsetRoot}${path.sep}`) || !existsSync(resolved)) {
    throw new Error(`Contract path '${relativePath}' is missing or outside the subset root.`);
  }
}

const identity = chromeIdentity();
let chrome;
const contractServer = await launchContractServer(async (width,height) => {
  if (!chrome) throw new Error('Chrome is not ready');
  await chrome.client.send('Emulation.setDeviceMetricsOverride', {
    width,height,mobile:false,deviceScaleFactor:options.deviceScaleFactor
  });
});
try {
  chrome = await launchChrome(identity.executable, options.deviceScaleFactor);
} catch (error) {
  await closeContractServer(contractServer);
  throw error;
}
const exceptionState = { current: [] };
chrome.client.on("Runtime.exceptionThrown", event => {
  exceptionState.current.push(
    event.exceptionDetails?.exception?.description
      || event.exceptionDetails?.text
      || "Chrome runtime exception");
});
const results = [];
try {
  await chrome.client.send("Emulation.setDeviceMetricsOverride", {
    width: 800, height: 600, mobile: false,
    deviceScaleFactor: options.deviceScaleFactor
  });
  await chrome.client.send("Page.addScriptToEvaluateOnNewDocument", {
    source: `globalThis.__webSceneWptExpectedDeviceScaleFactor = ${options.deviceScaleFactor};`
  });
  for (const relativePath of options.paths) {
    await chrome.client.send('Emulation.setDeviceMetricsOverride', {
      width:800,height:600,mobile:false,deviceScaleFactor:options.deviceScaleFactor
    });
    process.stdout.write(`RUN  chrome ${relativePath} ... `);
    const result = await runDocument(
      chrome.client,
      contractServer.baseUrl,
      relativePath,
      options.timeoutSeconds,
      exceptionState);
    results.push(result);
    process.stdout.write(`${result.status} (${result.duration} ms)\n`);
  }
} finally {
  await closeChrome(chrome);
  await closeContractServer(contractServer);
}

const subtests = results.flatMap(result => result.subtests);
const artifact = {
  schema: "webscene-wpt-contract-chrome-result-v1",
  engine: "chrome",
  identity: identity.version,
  viewport: { width: 800, height: 600, deviceScaleFactor: options.deviceScaleFactor },
  origin: contractServer.baseUrl,
  recordedAt: new Date().toISOString(),
  summary: {
    tests: results.length,
    passed: results.filter(result => result.status === "PASS").length,
    failed: results.filter(result => result.status === "FAIL").length,
    timedOut: results.filter(result => result.status === "TIMEOUT").length,
    subtests: subtests.length,
    subtestsPassed: subtests.filter(result => result.status === "PASS").length,
    subtestsFailed: subtests.filter(result => result.status !== "PASS").length
  },
  results
};
await mkdir(options.output, { recursive: false });
await writeFile(path.join(options.output, "results.json"), JSON.stringify(artifact, null, 2) + "\n");
process.stdout.write(`Chrome contract evidence: ${path.join(options.output, "results.json")}\n`);
if (artifact.summary.passed !== artifact.summary.tests) process.exitCode = 1;
