# Browser wiring sketch

This folder is intentionally a sketch. The C++ core does not care whether
EncodedChunks arrive from WebSocket, WebTransport, or a native WebView bridge.

Publisher side:

```js
const ingest = new WebSocket("wss://sfu.example.com/ingest?room=demo");
ingest.binaryType = "arraybuffer";

const encoder = new VideoEncoder({
  output(chunk, metadata) {
    // Use the same packet shape as my-webcodec_mediasoup:
    // uint32 headerLength, JSON header, encoded bytes.
    sendEncodedChunk(ingest, chunk, metadata);
  },
  error(error) {
    console.error(error);
  },
});
```

SFU side:

```cpp
void onIngestPacket(BinaryMessage msg) {
  EncodedChunk chunk = parseBrowserPacket(msg);
  sfu.ingest(chunk);
}
```

Viewer side:

```js
const pc = new RTCPeerConnection();
pc.ontrack = (event) => video.srcObject = event.streams[0];
await pc.setRemoteDescription({ type: "offer", sdp: offerFromNativeSfu });
const answer = await pc.createAnswer();
await pc.setLocalDescription(answer);
sendAnswerToNativeSfu(pc.localDescription.sdp);
```

