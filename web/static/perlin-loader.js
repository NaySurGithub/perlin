(function () {
  var DB_NAME = 'perlin-web';
  var STATE_STORE = 'state';
  var FILE_STORE = 'files';
  var DATA_DIR = '/perlin';
  var SAMPLES_REPO = DATA_DIR + '/bedrock-samples/repo';
  var SKIP_PERSIST = {
    'bedrock-samples': 1, server_packs: 1, screenshots: 1, shaderpacks: 1, ui_generated: 1,
    ui_defaults: 1, animation_overrides: 1, 'perlin.log': 1, 'blocks.log': 1
  };
  var CONCURRENCY = 24;

  var el = function (id) { return document.getElementById(id); };
  var statusEl = el('status');
  var barEl = el('bar');
  var progressEl = el('progress');
  var startButton = el('start');
  var form = el('form');
  var runtimeReady = false;
  var starting = false;
  var lastSaved = {};

  function setStatus(text, isError) {
    statusEl.textContent = text;
    statusEl.className = isError ? 'error' : '';
  }

  function setProgress(fraction) {
    progressEl.hidden = fraction < 0;
    if (fraction >= 0) barEl.style.width = Math.max(0, Math.min(1, fraction)) * 100 + '%';
  }

  function openDb() {
    return new Promise(function (resolve, reject) {
      var request = indexedDB.open(DB_NAME, 1);
      request.onupgradeneeded = function () {
        var db = request.result;
        if (!db.objectStoreNames.contains(STATE_STORE)) db.createObjectStore(STATE_STORE);
        if (!db.objectStoreNames.contains(FILE_STORE)) db.createObjectStore(FILE_STORE);
      };
      request.onsuccess = function () { resolve(request.result); };
      request.onerror = function () { reject(request.error); };
    });
  }

  function transactionDone(tx) {
    return new Promise(function (resolve, reject) {
      tx.oncomplete = function () { resolve(); };
      tx.onerror = function () { reject(tx.error); };
      tx.onabort = function () { reject(tx.error); };
    });
  }

  function cursorAll(db, storeName, visit) {
    return new Promise(function (resolve, reject) {
      var request = db.transaction(storeName, 'readonly').objectStore(storeName).openCursor();
      request.onsuccess = function () {
        var cursor = request.result;
        if (!cursor) { resolve(); return; }
        visit(cursor.key, cursor.value);
        cursor.continue();
      };
      request.onerror = function () { reject(request.error); };
    });
  }

  function isDirectory(mode) { return (mode & 61440) === 16384; }

  var knownDirs = {};
  function ensureDir(path) {
    if (knownDirs[path]) return;
    try { Module.FS.mkdirTree(path); } catch (e) {}
    knownDirs[path] = 1;
  }

  function writeFile(path, bytes) {
    var slash = path.lastIndexOf('/');
    if (slash > 0) ensureDir(path.substring(0, slash));
    Module.FS.writeFile(path, bytes);
  }

  async function restoreState(db) {
    await cursorAll(db, STATE_STORE, function (key, value) {
      try {
        writeFile(DATA_DIR + '/' + key, new Uint8Array(value.bytes));
        lastSaved[key] = value.stamp;
      } catch (e) {}
    });
  }

  function walk(dir, relative, out) {
    var names;
    try { names = Module.FS.readdir(dir); } catch (e) { return; }
    names.forEach(function (name) {
      if (name === '.' || name === '..') return;
      if (!relative && SKIP_PERSIST[name]) return;
      var full = dir + '/' + name;
      var rel = relative ? relative + '/' + name : name;
      var stat;
      try { stat = Module.FS.stat(full); } catch (e) { return; }
      if (isDirectory(stat.mode)) walk(full, rel, out);
      else out.push({ rel: rel, full: full, stamp: stat.size + ':' + (+stat.mtime) });
    });
  }

  var saving = false;
  async function saveState() {
    if (!runtimeReady || saving) return;
    saving = true;
    try {
      var files = [];
      walk(DATA_DIR, '', files);
      var changed = files.filter(function (f) { return lastSaved[f.rel] !== f.stamp; });
      if (!changed.length) return;
      var db = await openDb();
      var tx = db.transaction(STATE_STORE, 'readwrite');
      var store = tx.objectStore(STATE_STORE);
      changed.forEach(function (f) {
        try {
          var bytes = Module.FS.readFile(f.full);
          store.put({ bytes: bytes.slice().buffer, stamp: f.stamp }, f.rel);
          lastSaved[f.rel] = f.stamp;
        } catch (e) {}
      });
      await transactionDone(tx);
      db.close();
    } catch (e) {
    } finally {
      saving = false;
    }
  }

  function rawUrl(manifest, path) {
    if (manifest.baseUrl) return manifest.baseUrl + path.split('/').map(encodeURIComponent).join('/');
    return 'https://raw.githubusercontent.com/' + manifest.repository + '/' + manifest.ref + '/' +
      path.split('/').map(encodeURIComponent).join('/');
  }

  async function fetchWithRetry(url) {
    var lastError;
    for (var attempt = 0; attempt < 4; attempt++) {
      try {
        var response = await fetch(url, { mode: 'cors', credentials: 'omit' });
        if (!response.ok) throw new Error('HTTP ' + response.status);
        return new Uint8Array(await response.arrayBuffer());
      } catch (e) {
        lastError = e;
        await new Promise(function (r) { setTimeout(r, 400 * (attempt + 1)); });
      }
    }
    throw lastError;
  }

  async function ensureSamples(db, includeSounds) {
    setStatus('Reading asset list...');
    var manifestResponse = await fetch('assets-manifest.json', { cache: 'no-cache' });
    if (!manifestResponse.ok) throw new Error('assets-manifest.json is missing (HTTP ' + manifestResponse.status + ')');
    var manifest = await manifestResponse.json();
    var wanted = manifest.files.core.slice();
    if (includeSounds) wanted = wanted.concat(manifest.files.sounds);
    var wantedSet = {};
    wanted.forEach(function (p) { wantedSet[p] = 1; });

    var cached = {};
    var cachedCount = 0;
    setStatus('Loading cached assets...');
    await cursorAll(db, FILE_STORE, function (key, value) {
      if (!wantedSet[key] || value.ref !== manifest.ref) return;
      writeFile(SAMPLES_REPO + '/' + key, new Uint8Array(value.bytes));
      cached[key] = 1;
      cachedCount++;
    });

    var missing = wanted.filter(function (p) { return !cached[p]; });
    if (missing.length) {
      var total = missing.length;
      var done = 0;
      var failed = [];
      var pending = [];
      var flushing = Promise.resolve();
      function flush(batch) {
        flushing = flushing.then(async function () {
          var tx = db.transaction(FILE_STORE, 'readwrite');
          var store = tx.objectStore(FILE_STORE);
          batch.forEach(function (item) { store.put({ bytes: item.bytes.slice().buffer, ref: manifest.ref }, item.path); });
          await transactionDone(tx);
        }).catch(function () {});
      }
      var next = 0;
      async function worker() {
        while (next < missing.length) {
          var path = missing[next++];
          try {
            var bytes = await fetchWithRetry(rawUrl(manifest, path));
            writeFile(SAMPLES_REPO + '/' + path, bytes);
            pending.push({ path: path, bytes: bytes });
            if (pending.length >= 200) { flush(pending); pending = []; }
          } catch (e) {
            failed.push(path);
          }
          done++;
          if (done % 8 === 0 || done === total) {
            setProgress(done / total);
            setStatus('Downloading assets from GitHub: ' + done + ' / ' + total);
          }
        }
      }
      var workers = [];
      for (var i = 0; i < CONCURRENCY; i++) workers.push(worker());
      await Promise.all(workers);
      if (pending.length) flush(pending);
      await flushing;
      if (failed.length) {
        var coreFailed = failed.filter(function (p) { return manifest.files.core.indexOf(p) >= 0; });
        if (coreFailed.length) throw new Error(coreFailed.length + ' required files failed to download (first: ' + coreFailed[0] + '). Press Start to retry.');
      }
    }
    writeFile(DATA_DIR + '/bedrock-samples/.ready', new Uint8Array(0));
    setProgress(-1);
  }

  function launchArguments() {
    var server = el('server').value.trim();
    var args = ['--join', server.replace(/^https?:\/\//, ''), '--name', el('name').value.trim() || 'Steve', '--nethernet', '--accept-eula'];
    var mode = el('signaling').value;
    var host = server.replace(/^https?:\/\//, '').replace(/[:\/].*$/, '');
    var local = host === 'localhost' || host === '127.0.0.1' || host === '[::1]';
    var secure = mode === 'https' || /^https:\/\//.test(server) ||
      (mode === 'auto' && location.protocol === 'https:' && !local);
    if (secure && !/^https:\/\//.test(server)) args[1] = 'https://' + args[1];
    var stun = el('stun').value.trim();
    if (stun) args.push('--stun', stun);
    return args;
  }

  function loadForm() {
    try {
      var saved = JSON.parse(localStorage.getItem('perlin-launcher') || '{}');
      ['server', 'name', 'signaling', 'stun'].forEach(function (id) { if (saved[id] !== undefined) el(id).value = saved[id]; });
      el('sounds').checked = !!saved.sounds;
      el('eula').checked = !!saved.eula;
    } catch (e) {}
    var query = new URLSearchParams(location.search);
    if (query.get('server')) el('server').value = query.get('server');
    if (query.get('name')) el('name').value = query.get('name');
  }

  function saveForm() {
    try {
      var data = { sounds: el('sounds').checked, eula: el('eula').checked };
      ['server', 'name', 'signaling', 'stun'].forEach(function (id) { data[id] = el(id).value; });
      localStorage.setItem('perlin-launcher', JSON.stringify(data));
    } catch (e) {}
  }

  async function start() {
    if (starting || !runtimeReady) return;
    starting = true;
    startButton.disabled = true;
    saveForm();
    try {
      var db = await openDb();
      setStatus('Restoring saved data...');
      await restoreState(db);
      await ensureSamples(db, el('sounds').checked);
      db.close();
      setStatus('Starting...');
      el('launcher').hidden = true;
      setInterval(saveState, 5000);
      window.addEventListener('pagehide', saveState);
      document.addEventListener('visibilitychange', function () { if (document.hidden) saveState(); });
      document.getElementById('canvas').focus();
      Module.callMain(launchArguments());
    } catch (e) {
      setProgress(-1);
      setStatus(String(e && e.message ? e.message : e), true);
      el('launcher').hidden = false;
      startButton.disabled = false;
      starting = false;
    }
  }

  form.addEventListener('submit', function (event) {
    event.preventDefault();
    start();
  });

  el('clear').addEventListener('click', async function () {
    if (!confirm('Delete all cached assets and saved settings?')) return;
    await new Promise(function (resolve) {
      var request = indexedDB.deleteDatabase(DB_NAME);
      request.onsuccess = request.onerror = request.onblocked = function () { resolve(); };
    });
    try { localStorage.removeItem('perlin-launcher'); } catch (e) {}
    setStatus('Cached data removed.');
  });

  window.Module = {
    canvas: el('canvas'),
    noInitialRun: true,
    print: function (text) { console.log(text); },
    printErr: function (text) { console.warn(text); },
    onAbort: function (what) {
      el('launcher').hidden = false;
      setStatus('The engine crashed: ' + what, true);
    },
    onRuntimeInitialized: function () {
      runtimeReady = true;
      startButton.disabled = false;
      startButton.textContent = 'Start';
    }
  };

  loadForm();
  if (window.__perlinNoIsolation) setStatus(window.__perlinNoIsolation, true);
  else if (!window.crossOriginIsolated) setStatus('Enabling cross-origin isolation...');
  else setStatus('');
})();
