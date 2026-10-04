# Field Records

Installable web app (PWA) for tracking field inputs, harvest, sales and expenses.
Data syncs to Firebase Realtime Database under `users/<uid>/fieldRecords`, and a copy
is kept on the device so the app opens and takes edits without signal. Unsynced edits
upload automatically when the connection comes back.

## Layout

```
public/              ← everything that gets deployed
  index.html         the app
  sw.js              service worker (offline support)
  manifest.json      install metadata
  icon-*.png, icon.svg
tools/generate-icons.html   regenerates the PNG icons if you change the design
database.rules.json         recommended Realtime Database rules
firebase.json, .firebaserc  Firebase Hosting config
```

## Deploy

```bash
npm install -g firebase-tools
firebase login
firebase deploy --only hosting
```

> **Heads-up:** `.firebaserc` points at the `shopping-list-app-d077b` project. If the
> shopping list app is already hosted at `shopping-list-app-d077b.web.app`, this deploy
> **replaces it**. To keep both, create a second site and deploy there instead:
>
> ```bash
> firebase hosting:sites:create field-records-<something-unique>
> firebase target:apply hosting fieldrecords field-records-<something-unique>
> ```
> then add `"target": "fieldrecords"` inside `"hosting"` in `firebase.json`.

After a change to `index.html`, bump `CACHE` in `public/sw.js` (e.g. `v2` → `v3`) so
installed copies drop their old cache.

## Database rules

`database.rules.json` limits each account to its own `users/<uid>` data. It is **not**
deployed automatically, because the Realtime Database is shared with the shopping list
app and deploying would replace that app's rules too. Merge it into the existing rules
in the Firebase console (Realtime Database → Rules).

## Install on a phone

- **Android (Chrome):** open the site → ⋮ menu → *Add to Home screen* / *Install app*.
- **iPhone (Safari):** open the site → Share → *Add to Home Screen*.
