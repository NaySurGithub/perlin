self.addEventListener('install', function () { self.skipWaiting(); });
self.addEventListener('activate', function (event) { event.waitUntil(self.clients.claim()); });
self.addEventListener('fetch', function (event) {
  var request = event.request;
  if (request.cache === 'only-if-cached' && request.mode !== 'same-origin') return;
  if (new URL(request.url).origin !== self.location.origin) return;
  event.respondWith(fetch(request).then(function (response) {
    if (response.status === 0) return response;
    var headers = new Headers(response.headers);
    headers.set('Cross-Origin-Embedder-Policy', 'require-corp');
    headers.set('Cross-Origin-Opener-Policy', 'same-origin');
    headers.set('Cross-Origin-Resource-Policy', 'same-origin');
    return new Response(response.body, { status: response.status, statusText: response.statusText, headers: headers });
  }).catch(function () { return fetch(request); }));
});
