<?php
// generate_token.py  http://127.0.0.1:8000 ServiceWorkerNavigationPreload -expire-timestamp=2000000000
header("Origin-Trial: AsAA4dg2Rm+GSgnpyxxnpVk1Bk8CcE+qVBTDpPbIFNscyNRJOdqw1l0vkC4dtsGm1tmP4ZDAKwycQDzsc9xr7gMAAABmeyJvcmlnaW4iOiAiaHR0cDovLzEyNy4wLjAuMTo4MDAwIiwgImZlYXR1cmUiOiAiU2VydmljZVdvcmtlck5hdmlnYXRpb25QcmVsb2FkIiwgImV4cGlyeSI6IDIwMDAwMDAwMDB9");
header('Content-Type: application/javascript');
?>

self.addEventListener('activate', event => {
    event.waitUntil(
      registration.navigationPreload.enable()
        .then(_ => registration.navigationPreload.setHeaderValue('hello')));
  });

self.addEventListener('fetch', event => {
    if (event.request.url.indexOf('BrokenChunked') != -1) {
      event.respondWith(
        event.preloadResponse
          .catch(_ => { return new Response('dummy'); }));
      return;
    }
    if (event.request.url.indexOf('RedirectError') != -1) {
      event.respondWith(
        event.preloadResponse
          .catch(_ => { return new Response('dummy'); }));
      return;
    }
    if (event.preloadResponse) {
      event.respondWith(event.preloadResponse);
    }
  });
