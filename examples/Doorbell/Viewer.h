#pragma once

static const char VIEWER_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
  <meta charset="utf-8" /><meta name="viewport" content="width=device-width" />
  <title>ESP32 WebRTC Doorbell</title>
  <style>
    body {
      font: 17px system-ui;
      max-width: 760px;
      margin: 32px auto;
      padding: 0 18px;
      background: #15212d;
      color: #eef5fc;
    }
    button,
    input {
      font: inherit;
      padding: 10px;
      margin: 5px 3px;
      border-radius: 7px;
      border: 1px solid #728697;
    }
    button {
      cursor: pointer;
    }
    canvas {
      width: 100%;
      background: #080e14;
      border-radius: 10px;
      margin-top: 18px;
    }
    #status {
      min-height: 2em;
    }
    input {
      max-width: 90%;
    }
    audio {
      width: 100%;
    }
  </style>
  <h1>ESP32 WebRTC Doorbell</h1>
  <p>Enter the viewer token from Settings.h. One viewer at a time.</p>
  <input id="token" type="password" placeholder="Viewer token" autocomplete="off" />
  <button id="connect">Connect</button
  ><button id="disconnect" disabled>Disconnect</button>
  <p id="status" role="status">Disconnected</p>
  <button id="accept" disabled>Accept call</button
  ><button id="deny" disabled>End call</button>
  <button id="door" disabled>Open door</button>
  <canvas id="view" width="320" height="240"></canvas>
  <audio id="audio" autoplay controls></audio>
  <p>
    Type <b>r</b> in the board's Serial Monitor to ring. Accept starts the camera and available
    microphone. The example has no door lock configured.
  </p>
  <script>
    "use strict";
    const $ = (id) => document.getElementById(id);
    let pc,
      dc,
      running = false,
      generation = 0,
      ownsSession = false,
      frame = null,
      frameId = 0,
      offset = 0;
    let drawing = false,
      pendingImage = null;
    function status(s) {
      $("status").textContent = s;
    }
    async function api(path, body) {
      const response = await fetch(path, {
        method: body === undefined ? "GET" : "POST",
        headers: { "X-Viewer-Token": $("token").value, "Content-Type": "text/plain" },
        body,
        cache: "no-store",
        signal: AbortSignal.timeout(10000),
      });
      if (!response.ok) throw Error((await response.text()) || `HTTP ${response.status}`);
      return response.status === 204 ? "" : response.text();
    }
    function controls(enabled) {
      for (const id of ["accept", "deny", "door"]) $(id).disabled = !enabled;
    }
    // This example uses direct, same-LAN HTTP signaling. The ESP already knows this
    // browser's source IP. Resolve its obfuscated host candidate on that LAN to the
    // observed source address; do not use this adapter behind a proxy or over NAT.
    function lanOffer(sdp, address) {
      if (
        !/^\d{1,3}(\.\d{1,3}){3}$/.test(address) ||
        address.split(".").some((n) => Number(n) > 255)
      )
        throw Error("Invalid LAN address");
      return sdp.replace(
        /^(a=candidate:\S+ \d+ udp \d+ )\S+\.local( \d+ typ host[^\n]*)$/gim,
        (_, prefix, suffix) => prefix + address + suffix,
      );
    }
    async function disconnect() {
      ++generation;
      running = false;
      controls(false);
      $("disconnect").disabled = true;
      if (pc) pc.close();
      pc = null;
      dc = null;
      frame = null;
      pendingImage = null;
      $("audio").srcObject = null;
      try {
        if (ownsSession) await api("/close", "");
      } catch (e) {
        status(e.message);
      }
      ownsSession = false;
      $("connect").disabled = false;
    }
    async function draw(jpeg) {
      pendingImage = jpeg;
      if (drawing) return;
      drawing = true;
      while (pendingImage) {
        const bytes = pendingImage;
        pendingImage = null;
        try {
          const image = await createImageBitmap(new Blob([bytes], { type: "image/jpeg" }));
          $("view").width = image.width;
          $("view").height = image.height;
          $("view").getContext("2d").drawImage(image, 0, 0);
          image.close();
        } catch (e) {
          status("Dropped invalid camera frame");
        }
      }
      drawing = false;
    }
    function receive(e) {
      if (typeof e.data === "string") {
        status(e.data === "RING" ? "Doorbell ringing — accept or end the call" : e.data);
        return;
      }
      const packet = e.data;
      if (!(packet instanceof ArrayBuffer) || packet.byteLength < 17) return;
      const h = new DataView(packet),
        id = h.getUint32(4, true),
        size = h.getUint32(8, true),
        at = h.getUint32(12, true);
      if (h.getUint32(0, true) !== 0x47504a53 || size === 0 || size > 131072) return;
      if (at === 0) {
        frame = new Uint8Array(size);
        frameId = id;
        offset = 0;
      }
      const bytes = new Uint8Array(packet, 16);
      if (
        !frame ||
        id !== frameId ||
        size !== frame.length ||
        at !== offset ||
        at + bytes.length > size
      ) {
        frame = null;
        return;
      }
      frame.set(bytes, at);
      offset += bytes.length;
      if (offset === size) {
        const jpeg = frame;
        frame = null;
        draw(jpeg);
      }
    }
    $("connect").onclick = async () => {
      $("connect").disabled = true;
      const gen = ++generation;
      status("Connecting…");
      try {
        const [mode, lanAddress] = (await api("/config")).split("\n");
        pc = new RTCPeerConnection({ iceServers: [] });
        const connection = pc;
        const pendingCandidates = [];
        pc.onconnectionstatechange = () => {
          if (gen === generation) status("Connection: " + connection.connectionState);
        };
        pc.ontrack = (e) => {
          $("audio").srcObject = new MediaStream([e.track]);
          $("audio")
            .play()
            .catch(() => status("Press Play on the audio control to listen"));
        };
        // No browser microphone permission is required: this is receive-only audio.
        if (mode === "audio") pc.addTransceiver("audio", { direction: "recvonly" });
        dc = pc.createDataChannel("doorbell", { ordered: true });
        dc.binaryType = "arraybuffer";
        dc.onmessage = receive;
        dc.onopen = () => {
          controls(true);
          status("Ready — accept to view, or wait for a ring");
        };
        dc.onclose = () => {
          controls(false);
          status("Data channel closed");
        };
        // Gather completely so the offer includes candidates. This avoids racing
        // trickle requests with creation of the ESP peer.
        await pc.setLocalDescription(await pc.createOffer());
        await new Promise((resolve, reject) => {
          if (connection.iceGatheringState === "complete") {
            resolve();
            return;
          }
          const timer = setTimeout(() => {
            connection.removeEventListener("icegatheringstatechange", check);
            reject(Error("ICE gathering timed out"));
          }, 10000);
          function check() {
            if (connection.iceGatheringState === "complete") {
              clearTimeout(timer);
              connection.removeEventListener("icegatheringstatechange", check);
              resolve();
            }
          }
          connection.addEventListener("icegatheringstatechange", check);
        });
        await api("/offer", lanOffer(pc.localDescription.sdp, lanAddress));
        ownsSession = true;
        running = true;
        $("disconnect").disabled = false;
        while (running && gen === generation) {
          const msg = await api("/signal");
          if (gen !== generation) break;
          if (msg[0] === "S") {
            await pc.setRemoteDescription({ type: "answer", sdp: msg.slice(1) });
            for (const c of pendingCandidates) await pc.addIceCandidate(c);
            pendingCandidates.length = 0;
          } else if (msg[0] === "C") {
            const candidate = {
              candidate: msg.slice(1).replace(/^a=/, "").trim(),
              sdpMLineIndex: 0,
            };
            if (pc.remoteDescription) await pc.addIceCandidate(candidate);
            else pendingCandidates.push(candidate);
          }
          await new Promise((r) => setTimeout(r, 150));
        }
      } catch (e) {
        if (gen === generation) {
          await disconnect();
          status("Error: " + e.message);
        }
      }
    };
    $("disconnect").onclick = async () => {
      await disconnect();
      status("Disconnected");
    };
    $("accept").onclick = () => {
      if (dc?.readyState === "open") {
        dc.send("ACCEPT_CALL");
        $("audio")
          .play()
          .catch(() => {});
        status("Call accepted");
      }
    };
    $("deny").onclick = () => {
      if (dc?.readyState === "open") {
        dc.send("DENY_CALL");
        status("Call ended");
      }
    };
    $("door").onclick = () => {
      if (dc?.readyState === "open") dc.send("OPEN_DOOR");
    };
  </script>
</html>
)HTML";
