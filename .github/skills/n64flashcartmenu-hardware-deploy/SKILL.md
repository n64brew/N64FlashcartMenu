---
name: n64flashcartmenu-hardware-deploy
description: 'Build, deploy, and live-debug the N64FlashcartMenu project on real SC64 hardware. Use when: rebuilding the flashcart devcontainer after a Dockerfile or libdragon submodule update; building the menu with make inside that container; bridging the SC64 USB device from Windows host into the Linux container via sc64deployer server; deploying a build to the cart with remotedeploy.sh; the SD card reports "locked by the N64 side"; capturing a live crash/assertion backtrace over the debug channel. Covers container recreation without the devcontainer CLI, LTO-mismatch clean rebuilds, and reliable debug-log capture.'
---

# N64FlashcartMenu Hardware Build & Deploy

Workflow for building this project inside its devcontainer and deploying/debugging on a physically connected SC64 cart.

## When to Use
- After `git submodule update` bumps `libdragon` (or any devcontainer Dockerfile change).
- Building/testing a change and deploying it to a physically connected SC64 cart.
- Diagnosing an on-console crash/assertion — need the real backtrace, not guesswork.
- "SD card is locked by the N64 side" errors when trying to read/write SD contents from the PC.

## 1. Find/recreate the build container

```powershell
docker ps -a   # look for an image named vsc-n64flashcartmenu-flashcart or similar
```

Container IDs change every time it's recreated — never assume an ID from a previous session, always re-check.

If the Dockerfile (`.devcontainer/flashcart/Dockerfile.sc64deployer`) or the `libdragon` submodule changed, rebuild from scratch (no `devcontainer` CLI is installed, so do it manually):

```powershell
cd "<repo-root>\.devcontainer\flashcart"
docker build -t vsc-n64flashcartmenu-flashcart:latest -f Dockerfile.sc64deployer .
docker stop <old-container-id>; docker rm <old-container-id>
docker run -d --name n64flashcartmenu-flashcart `
  -v n64flashcartmenu-flashcart-bashhistory:/commandhistory `
  -v "<repo-root>:/workspaces/N64FlashcartMenu" `
  vsc-n64flashcartmenu-flashcart:latest sleep infinity
```

Then replicate the devcontainer's `postCreateCommand` (check `devcontainer.json` for the exact current command — it has changed before):

```powershell
docker exec -e N64_INST=/opt/libdragon <container> bash -c 'cd /workspaces/N64FlashcartMenu && git submodule update --init && cd libdragon && make clobber -j && ./build.sh --no-examples'
```

## 2. Build the menu

```powershell
docker exec -e N64_INST=/opt/libdragon <container> bash -c 'cd /workspaces/N64FlashcartMenu && make -j4'
```

**Always `make clean` first** if libdragon/toolchain was just rebuilt — stale `.o` files linked against the old toolchain fail with a confusing `lto1: fatal error: bytecode stream ... generated with LTO version X instead of the expected Y` rather than a normal compile error:

```powershell
docker exec -e N64_INST=/opt/libdragon <container> bash -c 'cd /workspaces/N64FlashcartMenu && make clean && make -j4'
```

## 3. Bridge the USB device from host into the container

The SC64 USB device is only visible to the host OS, not the Linux container — a network bridge is required.

```powershell
# Confirm the device is visible to the host first:
& "<repo-root>\tools\sc64\sc64deployer.exe" list
# Expect: "Found devices: 1: [SC64...] at port [serial://COMxx]"

# Start the bridge server (run on host, leave running in its own terminal):
& "<repo-root>\tools\sc64\sc64deployer.exe" server 0.0.0.0:9064
```

If port 9064 is already bound, an old server terminal is still holding it (`Only one usage of each socket address...`) — kill that terminal first, then restart the server.

If `list` reports "No SC64 devices found", it's a physical connection issue (cable/power) — ask the user to check the cart, not something fixable from the container side.

## 4. Deploy from inside the container

```powershell
docker exec -e N64_INST=/opt/libdragon -e REMOTE=host.docker.internal:9064 <container> bash -c 'cd /workspaces/N64FlashcartMenu && ./remotedeploy.sh -dur'
```

`remotedeploy.sh` flags:
- `-d` — upload + enter debug mode (no reboot)
- `-dr` — upload + debug + reboot (RAM boot only, doesn't persist)
- `-dur` — upload + write to SD as `/sc64menu.n64` + reboot (**use this one** — persists across normal power cycles)

## 5. Capture a live crash / assertion backtrace

Piping the live debug stream through `docker exec` is unreliable — it can disconnect prematurely or buffer/lose output before a crash prints. **Run it directly on the host instead:**

```powershell
& "<repo-root>\tools\sc64\sc64deployer.exe" debug --no-writeback
```

Leave this running (async), then have the user reproduce the crash on console. The assertion text + full backtrace (function/file/line for every frame) will print directly to this terminal — use it to pinpoint the exact failing line rather than guessing from binary diffs or file contents.

## 6. SD card operations from the PC side

```powershell
docker exec <container> bash -c "sc64deployer --remote host.docker.internal:9064 sd upload <local-file> <sd-path>"
docker exec <container> bash -c "sc64deployer --remote host.docker.internal:9064 sd ls <sd-dir>"
```

If this fails with `Couldn't init the SD card: SD card is locked by the N64 side`, the console is currently powered on and has the SD card mounted — **ask the user to power off the console**, then retry. `sc64deployer reset` alone is not sufficient; the console must actually be off.

For general code-review pitfalls (input validation, resource lifecycle, etc.), see `.github/copilot-instructions.md`.
