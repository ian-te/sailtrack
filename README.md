# Sailtrack

Reports a Sailfish OS phone's location to a self-hosted [Dawarich](https://dawarich.app), with no
app open. Built for the Jolla Phone (Sailfish OS 5.x, aarch64) as a native alternative to running
GPSLogger or OwnTracks under Android App Support, to save battery. It also builds for armv7hl.

One RPM, `harbour-sailtrack`, with two parts:

- **`harbour-sailtrack-daemon`**, a headless systemd **user** service. Nemo KeepAlive's
  `BackgroundActivity` wakes it every N minutes even while the CPU is suspended and the screen is off.
  Each wakeup it starts QtPositioning (the geoclue plugin), waits for a GPS fix or gives up at the
  timeout, and **stops the GPS again**. The point goes into an on-disk queue, and the queue is
  drained to Dawarich. Then the daemon sleeps until the next wakeup.
- **`harbour-sailtrack`**, a small Silica app. It holds the settings, shows the daemon's status (last
  fix, queue length, last upload and its error), starts and stops the service, and can send now or
  take a fix now.

Sailtrack needs a Dawarich server the phone can reach (over the internet, a VPN or your LAN) and an
API key from it.

## Install from SailfishOS:Chum

1. Install the SailfishOS:Chum GUI app if you don't have it: get the *SailfishOS:Chum GUI
   Installer* from OpenRepos (e.g. through Storeman), or see
   [how to install it](https://github.com/sailfishos-chum/main#how-to-install-the-sailfishoschum-gui-application).
2. Open **Chum**, search for **Sailtrack** and install it.
3. [Configure](#configure) it.

Updates come through Chum like any other package. The package reloads the systemd user units and
restarts a running daemon itself, so there is nothing to do after an update.

From the command line, once the Chum repository is set up:
`devel-su pkcon refresh && devel-su pkcon install harbour-sailtrack`.

## Install by hand

RPMs for aarch64 (64-bit installs) and armv7hl (32-bit ones) are on the
[GitHub Releases](https://github.com/ian-te/sailtrack/releases) page, or [build](#build) your own.
Copy the RPM to the phone and install it as root. There's no sudo; `devel-su` asks for the Developer
Mode password (Settings → Developer tools). Over SSH, with the phone at `PHONE`:

```bash
scp harbour-sailtrack-*.aarch64.rpm defaultuser@PHONE:
```

```bash
ssh -t defaultuser@PHONE 'devel-su rpm -Uvh harbour-sailtrack-*.aarch64.rpm'
```

Then, as `defaultuser` (not root), make systemd reread the unit and restart the daemon so the new
one runs:

```bash
ssh defaultuser@PHONE 'systemctl --user daemon-reload && systemctl --user try-restart harbour-sailtrack'
```

From 0.2.0 on the package does this itself on install and upgrade (`systemctl-user` in `%post`, as
Jolla's own packages do), so the line above only matters for 0.1.x RPMs, or when `systemctl` warns
that `harbour-sailtrack.service` changed on disk. Skipping it leaves systemd on the old unit and
the old daemon running.

`pkcon install-local` over SSH stalls at "Waiting for authentication" for an unsigned package, so
use `rpm`. Everything Sailtrack needs is on a stock install: libkeepalive, qt5-qtpositioning,
qt5-plugin-position-geoclue and Silica.

## Configure

1. Open **Sailtrack** → pull down → **Settings**:
   - **Base URL**: the address the phone reaches Dawarich at, **as it ends up after redirects**. An
     `http://` address that redirects to `https://` loses the POST body, so give the `https://` one.
     For example, a homelab Dawarich on a private subnet reached over Tailscale might be
     `http://10.99.0.3:3001`; the subnet is advertised by a subnet router, and the phone must accept
     routes (`tailscale set --accept-routes`).
   - **API key**: Dawarich → Account → API key.
   - **Every**: 1 min to 1 h. 5 min is the default.
   - **Drop fixes worse than**: 100 m by default. Raise it to accept rougher fixes indoors.
   - **Give up on a fix after**: 60 s by default. Indoors the GPS rarely locks, so it stays on this
     long every wakeup before a rough fix is sent. Shorten it to save battery.
2. Turn on **Track in the background**. This runs `systemctl --user enable --now`, so the service
   starts now and at every boot (`user-session.target`). Turning it off disables and stops it.
3. **Start** and **Stop** run or end the service now without touching the boot setting.

Points arrive in Dawarich with tracker id `SF` (OwnTracks topic `owntracks/sailtrack/SF`).

**Positioning:** fixes come from GPS only unless online assistance is on. In Settings → Location, the
high-accuracy mode with online assistance (`hybris\online_enabled=true` in
`/etc/location/location.conf`) adds A-GPS and network positions. A-GPS downloads satellite data, so
the first fix takes seconds instead of minutes. Indoors with no sky view and no assistance there is
often no fix, and the wakeup is logged as "No fix in N s".

## How it works

| | |
|---|---|
| Endpoint | `POST <base>/api/v1/owntracks/points?api_key=<key>`, one OwnTracks `_type: location` object per request |
| Fields | `lat`, `lon`, `tst`, `acc`, plus `alt`, `vac`, `vel` (km/h), `cog`, `batt`, `bs` when known, `tid` (`SF`), `topic` (`owntracks/sailtrack/SF`, which makes Dawarich read `vel` as km/h), `t` (`t` for timer, `u` for a manual fix) |
| Queue | `~/.local/share/harbour-sailtrack/queue.jsonl`, one point per line. Drained in batches of 25, up to 1000 per wakeup, oldest first. A batch is cut from the file once it's through |
| Failures | Network errors, timeouts (30 s), 401/403/404 and 5xx stop the drain and keep the point for the next wakeup. 400 and 422 drop the point, so one bad point can't block the queue. Dawarich ignores duplicates (`user_id, timestamp, lonlat` is unique), so a point sent twice after a crash is harmless |
| Config | `~/.config/harbour-sailtrack/sailtrack.conf` (INI, mode 600, it holds the API key). Written by the app and reread by the daemon at each wakeup and on `Reload` |
| Status | `~/.local/share/harbour-sailtrack/status.json`, written by the daemon on every change. The app watches it |
| Control | The app runs `systemctl --user start/stop/enable --now/disable --now harbour-sailtrack.service`, and calls the daemon on the session bus: `dev.iante.sailtrack`, path `/`, interface `dev.iante.sailtrack.Daemon`, methods `Reload`, `SendNow`, `FixNow` |
| Wakeups | An interval that is a standard keepalive slot (1, 2.5, 5, 10, 15, 30, 60 min) uses that slot, so the system wakes once for every app waiting on it. Other values use a range of interval to interval + 30 s |
| Fix | A position cached from before the wakeup is skipped. A GPS-grade fix (±25 m or better) ends the search early. Otherwise the search runs to the timeout and the best fix within the accuracy limit is sent. With online assistance on, that is often a network position of ±50–100 m, which arrives within seconds but would otherwise stop the GPS before it locks. The timeout is kept at least 15 s under the interval |

The app opts out of Sailjail (`Sandboxing=Disabled`): it needs `systemctl --user` and the daemon's
bus name. The daemon isn't launched through Sailjail at all. This is not a Harbour app.

## Build

With the Sailfish SDK (`sfdk`). The engine mounts your home, so the repo can live anywhere under it.
A shadow build keeps the tree clean:

```bash
mkdir -p build-aarch64 && cd build-aarch64 && sfdk -c target=SailfishOS-5.1.0.11-aarch64 build ../
```

The RPM lands in `build-aarch64/RPMS/`. Use the `armv7hl` target for 32-bit installs. sfdk names
the build after the newest git tag, with `+branch.date.sha` added when the tree isn't exactly on it.

CI (`.github/workflows/sailfish.yml`) builds both architectures in `coderus/sailfishos-platform-sdk`
on every push and pull request and keeps the RPMs as a workflow artifact for three days. Only a tag
`vX.Y.Z` publishes a GitHub Release; see [Releasing](#releasing).

## Releasing

This procedure is meant to be copied as is into my other Sailfish apps; only the names in this table
change.

| | |
|---|---|
| GitHub repo | `ian-te/sailtrack` |
| Package and spec | `harbour-sailtrack`, `rpm/harbour-sailtrack.spec` |
| OBS package | `home:<obs-user>/harbour-sailtrack` on [build.sailfishos.org](https://build.sailfishos.org) |
| OBS service file | `obs/_service` |

**Versions come from git tags only.** A tag `vX.Y.Z` is the release X.Y.Z. CI and OBS's `tar_git`
both set the spec's `Version:` from it, and sfdk names local builds after the newest tag. Untagged
CI builds keep the spec's `Version:` with `Release: 0.<run number>.git<sha>`, which sorts below the
release and above the CI build before it.

### Each release

1. Make sure CI is green on `main` and the build has been tried on a phone.
2. Set `Version:` in the spec to X.Y.Z (CI warns if the tag and the spec disagree), commit, and push.
3. Tag that commit and push the tag:

   ```bash
   git tag -a vX.Y.Z -m "vX.Y.Z" && git push origin vX.Y.Z
   ```

   CI builds both architectures and publishes the GitHub Release `vX.Y.Z` with the RPMs.
4. Point OBS at the tag: set `revision` to `vX.Y.Z` in `obs/_service`, commit that, and put the same
   file on the OBS package (step 4 of the one-time setup below). For a package already in Chum,
   change it in **`sailfishos:chum:testing`** instead: you are its maintainer there after the first
   submission, and saving `_service` triggers a new build.
5. When the build in `sailfishos:chum:testing` succeeds, open the package there → **Submit package**
   → target project `sailfishos:chum`, target package empty, description optional. The Chum
   maintainers promote it, and phones get it as an update.

Chum wants a fixed revision in `_service`, a tag or a hash, never a branch head or a webhook, so
nothing reaches users without a release.

### One-time setup on OBS

Done once per developer (steps 1–3) and once per package (4–7). OBS is
[build.sailfishos.org](https://build.sailfishos.org); `<obs-user>` is your account name there.

1. **Account.** There's no sign-up form: as the build.sailfishos.org front page says, ask *Keto*
   for an account, in `#sailfishos` on the OFTC IRC network or with a message on the
   [Sailfish OS Forum](https://forum.sailfishos.org). Say what you want to build (e.g. "a home
   project to package my apps for SailfishOS:Chum"). Once you can log in, your home project is
   `https://build.sailfishos.org/project/show/home:<obs-user>`.
2. **Build targets.** In the home project → **Advanced** → **Meta** (the project's Meta, not a
   package's), add these between `<project>` and `</project>`, then **Save**:

   ```xml
   <repository name="sailfishos_5.2_aarch64">
     <path project="sailfishos:5.2" repository="latest_aarch64"/>
     <arch>aarch64</arch>
   </repository>
   <repository name="sailfishos_5.2_armv7hl">
     <path project="sailfishos:5.2" repository="latest_armv7hl"/>
     <arch>armv8el</arch>
   </repository>
   ```

   Add `sailfishos:5.1` and others the same way to try older releases; Chum builds its own set of
   releases (up to 5.2 at the time of writing) once the package is there.
3. **Optional: `osc`**, the OBS command line, if you'd rather not click: put your credentials in
   `~/.config/osc/oscrc` with `apiurl = https://build.sailfishos.org`.
4. **Package.** In the home project → **Create Package**, name it exactly like the spec
   (`harbour-sailtrack`), **Save**. Then **Add File** → upload this repo's `obs/_service`
   (the file must be called `_service`). OBS runs `tar_git`, which checks out the tag, packs the
   source and builds. Watch each target's status on the package page; click a status for its log.
   **Trigger services** re-runs the checkout.
5. **Fix it until it builds** for every target, in your home project. Chum only takes packages that
   build there.
6. **Submit to Chum testing.** On the package page → **Submit package** → target project
   `sailfishos:chum:testing`, leave the target package name empty, add a short description (e.g.
   "New package: Sailtrack X.Y.Z, background location reporter for Dawarich"), **Ok**. With `osc`:
   `osc submitrequest home:<obs-user> harbour-sailtrack sailfishos:chum:testing`.
   Submit any dependency that isn't in Chum or Sailfish OS yet, too.
7. **Promotion.** The testing maintainers accept the request, and the package builds in
   `sailfishos:chum:testing`; you become its maintainer there. Test it from the testing repository
   (the Chum GUI app can switch to testing in its settings), then from
   `sailfishos:chum:testing/harbour-sailtrack` → **Submit package** → target `sailfishos:chum`
   (target package empty). Once accepted it's in Chum for everyone.

After that, each release is the short list above.

The Chum store page comes from the metadata block at the end of the spec's `%description`
(`%if 0%{?_chum}` … `%endif`, [format](https://github.com/sailfishos-chum/main/blob/main/Metadata.md)):
title, categories, icon, links and the AI rating. Keep it the last paragraph of `%description`, with no
blank or comment lines inside it.

## Debugging

- The daemon logs to the journal (`devel-su journalctl --user-unit harbour-sailtrack -f`, or
  `devel-su journalctl -f | grep sailtrack`). `defaultuser` can't read the journal.
- To run it in the foreground with logs on the terminal, first run
  `systemctl --user stop harbour-sailtrack`, then
  `QT_LOGGING_TO_CONSOLE=1 harbour-sailtrack-daemon`.
- To poke it over D-Bus:
  `dbus-send --session --print-reply --dest=dev.iante.sailtrack / dev.iante.sailtrack.Daemon.FixNow`.
- Watch `status.json` and `queue.jsonl` in `~/.local/share/harbour-sailtrack/`.

## Layout

| Path | What |
|---|---|
| `common/paths.h` | Paths, config keys and defaults, D-Bus names, shared by both binaries |
| `daemon/` | `tracker.*` (wakeup cycle, fix, status, D-Bus), `queue.*` (on-disk queue), `uploader.*` (OwnTracks POSTs) |
| `systemd/harbour-sailtrack.service` | The user unit |
| `app/src/controller.*` | The app's settings, status, systemctl and D-Bus |
| `app/qml/` | Main page, settings dialog, cover |
| `app/icons/` | `harbour-sailtrack.svg` and the PNGs made from it (`rsvg-convert -w N -h N`) |
| `rpm/harbour-sailtrack.spec` | Packaging, with the Chum metadata in `%description` |
| `obs/_service` | The OBS source service: `tar_git` from a release tag (see Releasing) |
| `.github/workflows/sailfish.yml` | CI builds and tag releases |
