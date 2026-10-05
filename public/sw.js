// Field Records Service Worker
// Bump CACHE when you deploy changes to the shell files below; old caches are deleted on activate.
const CACHE = 'field-records-v3';
const SHELL = [
  '/',
  '/manifest.json',
  '/icon.svg',
  '/icon-180.png',
  '/icon-192.png',
  '/icon-512.png',
  '/icon-maskable-512.png'
];
const NAV_TIMEOUT_MS = 4000;   // weak signal: fall back to the cached app after this long

self.addEventListener('install', function(e) {
  e.waitUntil(
    caches.open(CACHE).then(function(cache) { return cache.addAll(SHELL); })
  );
  self.skipWaiting();
});

self.addEventListener('activate', function(e) {
  e.waitUntil(
    caches.keys().then(function(keys) {
      return Promise.all(keys.filter(function(k){ return k !== CACHE; }).map(function(k){ return caches.delete(k); }));
    }).then(function() { return self.clients.claim(); })
  );
});

self.addEventListener('fetch', function(e) {
  var req = e.request;
  if (req.method !== 'GET') return;
  var url = new URL(req.url);

  // Google Fonts: cache-first so the app keeps its look offline
  if (url.hostname === 'fonts.googleapis.com' || url.hostname === 'fonts.gstatic.com') {
    e.respondWith(
      caches.match(req).then(function(cached) {
        return cached || fetch(req).then(function(res) {
          var copy = res.clone();
          caches.open(CACHE).then(function(c){ c.put(req, copy); });
          return res;
        });
      })
    );
    return;
  }

  // Everything else off-site (Firebase auth + database) goes straight to the network.
  // Never fake a response here: the app must see real failures to know a save didn't land.
  if (url.origin !== self.location.origin) return;

  // The page itself: network-first so deploys show up right away, cached copy when offline
  if (req.mode === 'navigate') {
    e.respondWith(
      new Promise(function(resolve) {
        var settled = false;
        function fallback() {
          if (settled) return;
          caches.match('/').then(function(cached) {
            if (settled) return;
            if (cached) { settled = true; resolve(cached); }
          });
        }
        var timer = setTimeout(fallback, NAV_TIMEOUT_MS);
        fetch(req).then(function(res) {
          clearTimeout(timer);
          if (res.ok) {
            var copy = res.clone();
            caches.open(CACHE).then(function(c){ c.put('/', copy); });
          }
          if (!settled) { settled = true; resolve(res); }
        }).catch(function() {
          clearTimeout(timer);
          caches.match('/').then(function(cached) {
            if (!settled) { settled = true; resolve(cached || Response.error()); }
          });
        });
      })
    );
    return;
  }

  // Other same-origin files: serve from cache, refresh in the background
  e.respondWith(
    caches.match(req).then(function(cached) {
      var network = fetch(req).then(function(res) {
        if (res.ok) {
          var copy = res.clone();
          caches.open(CACHE).then(function(c){ c.put(req, copy); });
        }
        return res;
      });
      if (cached) { network.catch(function(){}); return cached; }
      return network;
    })
  );
});
