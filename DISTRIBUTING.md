# Distributing Volume over CEC

Three tiers, from "get it on my own TVs" to "list it in the store". Prebuilt
artifacts are in this package: the ipk (`org.webosbrew.cecvold_0.4.0_all.ipk`)
and the app manifest (`dist/org.webosbrew.cecvold.manifest.json`).

Note on architecture: the bundled binary is aarch64 (64-bit). That covers the C1
and every other recent LG set. Older webOS TVs are 32-bit (`armv7l`) and the
binary won't run there. That's fine for your own fleet (all aarch64). For public
release, add a 32-bit binary and select at boot; see the last section.

## Tier 1 - your own fleet (a few C1s)

Just install the ipk on each TV, then open the app once and press Enable. Pick
whichever install path you like.

Dev Manager Desktop (GUI, easiest): add each TV, Install, pick the ipk.

ares-install (CLI, if you set up ares device auth):

```
ares-install --device TVNAME org.webosbrew.cecvold_0.4.0_all.ipk
```

Straight over SSH (no ares device setup): copy the ipk to the TV and install via
the app-install Luna service:

```
scp org.webosbrew.cecvold_0.4.0_all.ipk root@TV_IP:/tmp/
ssh root@TV_IP
luna-send-pub -i 'luna://com.webos.appInstallService/dev/install' \
  '{"id":"org.webosbrew.cecvold","ipkUrl":"/tmp/org.webosbrew.cecvold_0.4.0_all.ipk","subscribe":true}'
```

Then open "Volume over CEC" on the TV, press Enable (it autodetects the node and
installs the boot hook), and set that Apple TV to Volume Control > HDMI. Done per
set. Nothing else to host.

## Tier 2 - your own repo (in-app install + updates, no PR)

Host the ipk and manifest anywhere public (GitHub Releases is simplest), publish
a small repo manifest, and add its URL in Homebrew Channel > Settings > Add
repository. Now the app installs and self-updates in-app on any TV you point at
the repo, and you can share the URL.

- Put `org.webosbrew.cecvold_0.4.0_all.ipk` and
  `org.webosbrew.cecvold.manifest.json` on a GitHub Release (tag `v0.4.0`). The
  manifest's `ipkUrl` already points at that release path; keep them together.
- A repo is just a directory served over HTTPS containing the manifests it lists.
  The easiest route is the apps-repo tooling, but for a private one-app repo you
  can serve a single `apps.json`-style index that references your manifest. If
  you'd rather not run your own index, skip to Tier 3, which reuses the same
  release artifacts.
- You can deep-link the "Add repository" prompt:
  `ares-launch org.webosbrew.hbchannel -p '{"launchMode":"addRepository","url":"https://YOUR_REPO"}'`

## Tier 3 - the official webosbrew store (public)

One PR to the central repo, reusing your release artifacts.

1. Push this project to GitHub (e.g. `github.com/jakergirard/volume-over-cec`).
   The included `.github/workflows/release.yml` builds the ipk and manifest and
   attaches both to a GitHub Release whenever you push a `vX.Y.Z` tag, so you
   don't repackage by hand:

   ```
   git tag v0.4.0 && git push --tags
   ```

2. Fork `github.com/webosbrew/apps-repo`, add `packages/org.webosbrew.cecvold.yml`
   (included here), and open a PR. It points `manifestUrl` at your release's
   `latest/download/...manifest.json`. They review and test, merge, and the app
   shows up in every user's Homebrew Channel shortly after.

Consider using your own app id namespace instead of `org.webosbrew.*` for a
third-party app (the id lives in `app/appinfo.json`, `dist/*.manifest.json`, and
the yml filename). Keep it consistent across all three if you change it.

## Making it universal (before public release)

To cover 32-bit sets too, ship both binaries and pick at boot:

- Build a 32-bit binary as well and name them by arch:

  ```
  docker run --rm --platform linux/arm64  -v "$PWD":/src -w /src gcc \
    gcc -O2 -static -o app/cecvold.aarch64 cecvold.c
  docker run --rm --platform linux/arm/v7 -v "$PWD":/src -w /src gcc \
    gcc -O2 -static -o app/cecvold.armv7l cecvold.c
  ```

- In the boot hook, select by arch:

  ```
  BIN=/media/developer/apps/usr/palm/applications/org.webosbrew.cecvold/cecvold.$(uname -m)
  ```

- Have the Enable button's autodetect call the same `cecvold.$(uname -m)`.

Autodetect already handles the per-model injection-node difference, and the
volume keycodes (115/114/113) are LG-wide, so once both arch binaries are in the
ipk the app is portable across rooted LG TVs.
