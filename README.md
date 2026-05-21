# libdatachannel WebCodecs SFU toy

This toy project shows the shape of a C++ function-call media path for the
`my-webcodec_mediasoup` idea:

```text
Browser WebCodecs Encoder
  EncodedVideoChunk
  |
  | WebSocket/WebTransport/native bridge adapter
  v
C++ NativeSfu::ingest()
  H264 NALU parsing
  RTP packetization
  peer-by-peer RTP state rewrite
  rtc::Track::send()
  |
  +--> Viewer 1 browser
  +--> Viewer 2 browser
  +--> Viewer 3 browser
```

The important boundary is this: this project does not use mediasoup or a
mediasoup worker process. The C++ app owns the SFU-like relay logic and calls
libdatachannel tracks directly. libdatachannel still runs its own WebRTC
networking internally for ICE, DTLS, SRTP, and UDP I/O.

## What is included

- `EncodedChunk`: a compact C++ representation of a WebCodecs
  `EncodedVideoChunk`.
- `H264RtpPacketizer`: converts H.264 Annex-B or AVCC/length-prefixed chunks
  into RTP packets.
- `NativeSfu`: owns viewer PeerConnections, creates a send-only video track,
  and sends RTP packets to every connected viewer track.
- `ldc_toy_demo`: a small executable that exercises the in-process ingest path
  with fake H.264 chunks. It is a scaffold for wiring real signaling/ingest.

## Build

```powershell
cd C:\VScode\webCodecs\my-webcodec_mediasoup\libdatachannel
cmake -S . -B build -DLDC_TOY_FETCH_LIBDATACHANNEL=ON
cmake --build build --config Release
```

If you already installed libdatachannel with vcpkg or another package manager:

```powershell
cmake -S . -B build -DLDC_TOY_FETCH_LIBDATACHANNEL=OFF
cmake --build build --config Release
```

## Next wiring step

The demo intentionally leaves HTTP/WebSocket/WebTransport out of the core. Add
one adapter that receives the browser's binary WebCodecs packet and calls:

```cpp
sfu.ingest(chunk);
```

For viewer signaling, expose these `NativeSfu` methods over whatever signaling
transport you choose:

- `createViewer(peerId)`
- `setViewerAnswer(peerId, sdp)`
- `addViewerCandidate(peerId, candidate, mid)`
- local description/candidate callbacks from `SfuCallbacks`

That keeps signaling outside the media path while preserving the function-call
boundary for `EncodedChunk -> RTP`.

