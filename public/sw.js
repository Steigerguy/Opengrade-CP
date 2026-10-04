// Field Records Service Worker
const CACHE = 'field-records-v1';
const SHELL = ['/'];

self.addEventListener('install', function(e) {
  e.waitUntil(
    caches.open(CACHE).then(function(cache) {
      return cache.addAll(SHELL);
    })
  );
  self.skipWaiting();
});

self.addEventListener('activate', function(e) {
  e.waitUntil(
    caches.keys().then(function(keys) {
      return Promise.all(keys.filter(function(k){ return k !== CACHE; }).map(function(k){ return caches.delete(k); }));
    })
  );
  self.clients.claim();
});

self.addEventListener('fetch', function(e) {
  var url = e.request.url;

  // Always go network-first for Firebase API calls (auth + database)
  if (url.includes('firebaseio.com') || url.includes('identitytoolkit') || url.includes('securetoken')) {
    e.respondWith(fetch(e.request).catch(function() { return new Response('{}'); }));
    return;
  }

  // App shell: cache-first, fallback to network
  e.respondWith(
    caches.match(e.request).then(function(cached) {
      var networkFetch = fetch(e.request).then(function(response) {
        if (response.ok) {
          var clone = response.clone();
          caches.open(CACHE).then(function(cache){ cache.put(e.request, clone); });
        }
        return response;
      });
      return cached || networkFetch;
    })
  );
});
