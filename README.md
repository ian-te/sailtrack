# Sailtrack

Reports a Sailfish OS phone's location to a self-hosted [Dawarich](https://dawarich.app), with no
app open. Built for the Jolla Phone (Sailfish OS 5.2, aarch64), as a native alternative to running
GPSLogger under Android App Support, to save battery.

One RPM, `harbour-sailtrack`, with two parts:

- **`harbour-sailtrack-daemon`**, a headless systemd **user** service. Nemo KeepAlive's
  `BackgroundActivity` wakes it every N minutes even while the CPU is suspended and the screen is off.
  Each wakeup it starts QtPositioning (the geoclue plugin), takes the first fix within the accuracy
  limit or gives up at the timeout, and **stops the GPS again**. The point goes into an on-disk queue,
  and the queue is drained to Dawarich. Then the daemon sleeps until the next wakeup.
- **`harbour-sailtrack`**, a small Silica app. It holds the settings, shows the daemon's status (last
  fix, queue length, last upload and its error), starts and stops the service, and can send now or
  take a fix now.

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
| Fix | A position cached from before the wakeup is skipped. The first fix within the accuracy limit ends the search, otherwise the timeout does. The timeout is kept at least 15 s under the interval |

The app opts out of Sailjail (`Sandboxing=Disabled`): it needs `systemctl --user` and the daemon's
bus name. The daemon isn't launched through Sailjail at all. This is not a Harbour app.

## Build

With the Sailfish SDK (`sfdk`). The engine mounts your home, so the repo can live anywhere under it.
A shadow build keeps the tree clean:

```bash
mkdir -p build-aarch64 && cd build-aarch64
sfdk -c target=SailfishOS-5.1.0.11-aarch64 build ../
```

The RPM lands in `build-aarch64/RPMS/`. Use the `armv7hl` target for 32-bit installs.

CI (`.github/workflows/sailfish.yml`) builds both architectures in `coderus/sailfishos-platform-sdk`,
like Ian Assistant does. Each push to `main` publishes a GitHub Release `v0.1.<run>` with the RPMs.

## Install

Copy the RPM to the phone and install it as root. There's no sudo, and `devel-su` asks for the
Developer Mode password:

```bash
scp build-aarch64/RPMS/harbour-sailtrack-*.aarch64.rpm defaultuser@192.168.1.145:
ssh -t defaultuser@192.168.1.145 'devel-su rpm -Uvh harbour-sailtrack-*.aarch64.rpm'
```

`pkcon install-local` over SSH stalls at "Waiting for authentication" for an unsigned package (see
`proxmos/infra/jolla/README.md`). Everything it needs is already on a stock install: libkeepalive,
qt5-qtpositioning, qt5-plugin-position-geoclue and Silica.

An update is the same `rpm -Uvh`. Restart the service afterwards so the new daemon runs:
`systemctl --user restart harbour-sailtrack`.

Nothing updates by itself yet. A signed RPM repository published by CI and added once with
`ssu ar` would fix that.

## Configure

1. Open **Sailtrack** → pull down → **Settings**:
   - **Base URL**: the address the phone reaches Dawarich at, **as it ends up after redirects**. An
     `http://` address that redirects to `https://` loses the POST body. For the homelab it's
     `http://10.99.0.3:3001`, through the Tailscale subnet route (10.99.0.0/24 via lxc-services);
     the phone must accept routes (`tailscale set --accept-routes`).
   - **API key**: Dawarich → Account → API key.
   - **Every**: 1 min to 1 h. 5 min is the default.
   - **Drop fixes worse than**: 100 m by default. Raise it to accept rougher fixes indoors.
   - **Give up on a fix after**: 90 s by default.
2. Turn on **Track in the background**. This runs `systemctl --user enable --now`, so the service
   starts now and at every boot (`user-session.target`). Turning it off disables and stops it.
3. **Start** and **Stop** run or end the service now without touching the boot setting.

**Positioning:** fixes come from GPS only unless online assistance is on. In Settings → Location, the
high-accuracy mode with online assistance (`hybris\online_enabled=true` in
`/etc/location/location.conf`) adds A-GPS. It downloads satellite data, so the first fix takes
seconds instead of minutes. Indoors with no sky view and no assistance there is often no fix, and
the wakeup is logged as "No fix in N s".

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
| `rpm/harbour-sailtrack.spec` | Packaging |
