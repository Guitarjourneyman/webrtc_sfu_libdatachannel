const publishButton = document.querySelector('#publishButton');
const viewButton = document.querySelector('#viewButton');
const localVideo = document.querySelector('#localVideo');
const remoteVideo = document.querySelector('#remoteVideo');
const chunkCount = document.querySelector('#chunkCount');
const signalState = document.querySelector('#signalState');
const ingestState = document.querySelector('#ingestState');
const logBox = document.querySelector('#log');

const signalingBase = 'ws://127.0.0.1:8000';
let chunks = 0;

publishButton.addEventListener('click', () => {
  publishButton.disabled = true;
  startPublisher().catch(error => {
    publishButton.disabled = false;
    writeLog(`publisher failed: ${error.message}`);
  });
});

viewButton.addEventListener('click', () => {
  viewButton.disabled = true;
  startViewer().catch(error => {
    viewButton.disabled = false;
    writeLog(`viewer failed: ${error.message}`);
  });
});

async function startPublisher() {
  assertWebCodecs();

  const ingest = new WebSocket(`${signalingBase}/ingest`);
  ingest.binaryType = 'arraybuffer';
  await waitForSocket(ingest);
  ingestState.textContent = 'connected';

  const stream = await navigator.mediaDevices.getUserMedia({
    video: { width: 640, height: 360, frameRate: 30 },
    audio: false
  });
  localVideo.srcObject = stream;

  await startWebCodecsEncoder(stream, ingest);
  writeLog('publisher started');
}

async function startViewer() {
  const peerId = crypto.randomUUID();
  const signal = new WebSocket(`${signalingBase}/signal`);
  await waitForSocket(signal);
  signalState.textContent = 'connected';

  const pc = new RTCPeerConnection();
  const pendingRemoteCandidates = [];
  const pendingLocalCandidates = [];
  let remoteDescriptionReady = false;
  let viewerRequested = false;
  const control = pc.createDataChannel('ldc-control');
  control.onopen = () => writeLog('viewer datachannel open ldc-control');
  control.onclose = () => writeLog('viewer datachannel close ldc-control');
  control.onerror = () => writeLog('viewer datachannel error ldc-control');

  const transceiver = pc.addTransceiver('video', { direction: 'recvonly' });
  preferH264(transceiver);

  pc.onconnectionstatechange = () => {
    writeLog(`viewer connection ${pc.connectionState}`);
  };
  pc.oniceconnectionstatechange = () => {
    writeLog(`viewer ice ${pc.iceConnectionState}`);
  };
  pc.onicegatheringstatechange = () => {
    writeLog(`viewer gathering ${pc.iceGatheringState}`);
  };
  pc.ondatachannel = event => {
    writeLog(`viewer datachannel ${event.channel.label}`);
    event.channel.onopen = () => writeLog(`viewer datachannel open ${event.channel.label}`);
    event.channel.onclose = () => writeLog(`viewer datachannel close ${event.channel.label}`);
    event.channel.onerror = () => writeLog(`viewer datachannel error ${event.channel.label}`);
  };
  pc.ontrack = event => {
    remoteVideo.srcObject = event.streams[0] ?? new MediaStream([event.track]);
    writeLog(`viewer track ${event.track.kind} ${event.track.readyState}`);
    void remoteVideo.play();
  };
  pc.onicecandidate = event => {
    if (!event.candidate) {
      writeLog('viewer local candidates complete');
      return;
    }
    writeLog(`viewer local candidate mid=${event.candidate.sdpMid ?? 'n/a'}`);
    const message = {
      action: 'viewerCandidate',
      peerId,
      candidate: event.candidate.candidate,
      mid: event.candidate.sdpMid ?? 'video0'
    };
    if (viewerRequested) {
      send(signal, message);
    } else {
      pendingLocalCandidates.push(message);
    }
  };

  signal.addEventListener('message', async event => {
    const message = JSON.parse(event.data);
    if (message.event === 'localDescription') {
      await pc.setRemoteDescription(message.data);
      remoteDescriptionReady = true;
      writeLog(`viewer remote ${message.data.type} set`);
      writeLog(`viewer remote m-lines ${summarizeSdp(message.data.sdp)}`);
      while (pendingRemoteCandidates.length > 0) {
        await pc.addIceCandidate(pendingRemoteCandidates.shift());
      }
    } else if (message.event === 'localCandidate') {
      const candidate = {
        candidate: message.data.candidate,
        sdpMid: message.data.mid
      };
      if (remoteDescriptionReady) {
        await pc.addIceCandidate(candidate);
        writeLog(`viewer remote candidate mid=${message.data.mid}`);
      } else {
        pendingRemoteCandidates.push(candidate);
        writeLog(`viewer queued remote candidate mid=${message.data.mid}`);
      }
    }
  });

  const offer = await pc.createOffer();
  await pc.setLocalDescription(offer);
  writeLog(`viewer offer m-lines ${summarizeSdp(pc.localDescription.sdp)}`);
  send(signal, { action: 'createViewer', peerId, sdp: pc.localDescription.sdp });
  viewerRequested = true;
  while (pendingLocalCandidates.length > 0) {
    send(signal, pendingLocalCandidates.shift());
  }
  writeLog(`viewer requested: ${peerId}`);
}

async function startWebCodecsEncoder(stream, ingest) {
  const track = stream.getVideoTracks()[0];
  const Processor = window.MediaStreamTrackProcessor;
  const processor = new Processor({ track });
  const reader = processor.readable.getReader();
  const settings = track.getSettings();
  const width = settings.width ?? 640;
  const height = settings.height ?? 360;
  const frameRate = settings.frameRate ?? 30;
  const canvas = new OffscreenCanvas(width, height);
  const ctx = canvas.getContext('2d');
  let frameIndex = 0;

  const config = {
    codec: 'avc1.42E01F',
    width,
    height,
    framerate: frameRate,
    bitrate: 1_200_000,
    latencyMode: 'realtime',
    hardwareAcceleration: 'prefer-hardware',
    avc: { format: 'avc' }
  };
  const support = await VideoEncoder.isConfigSupported(config);
  if (!support.supported) {
    throw new Error('H264 WebCodecs encoder is not supported');
  }

  const encoder = new VideoEncoder({
    output(chunk, metadata) {
      sendEncodedChunk(ingest, chunk, metadata);
    },
    error(error) {
      writeLog(`encoder error: ${error.message}`);
    }
  });
  encoder.configure(support.config ?? config);

  void pump();

  async function pump() {
    while (true) {
      const { value, done } = await reader.read();
      if (done || !value) {
        break;
      }
      frameIndex++;
      try {
        ctx.drawImage(value, 0, 0, width, height);
        ctx.fillStyle = 'rgba(20, 82, 104, 0.74)';
        ctx.fillRect(12, 12, 220, 64);
        ctx.fillStyle = '#ffffff';
        ctx.font = '18px Arial';
        ctx.fillText('libdatachannel SFU', 24, 38);
        ctx.font = '13px Arial';
        ctx.fillText(`frame ${frameIndex}`, 24, 60);
        const processed = new VideoFrame(canvas, { timestamp: value.timestamp });
        encoder.encode(processed, {
          keyFrame: frameIndex % Math.max(1, Math.floor(frameRate * 2)) === 1
        });
        processed.close();
      } finally {
        value.close();
      }
    }
  }
}

function sendEncodedChunk(socket, chunk, metadata) {
  if (socket.readyState !== WebSocket.OPEN) {
    return;
  }

  const encoded = new Uint8Array(chunk.byteLength);
  chunk.copyTo(encoded);

  const config = metadata?.decoderConfig?.description;
  const configBytes = config ? new Uint8Array(config) : new Uint8Array(0);
  const packet = new Uint8Array(21 + configBytes.byteLength + encoded.byteLength);
  const view = new DataView(packet.buffer);
  packet[0] = chunk.type === 'key' ? 1 : 0;
  view.setBigUint64(1, BigInt(chunk.timestamp));
  view.setBigUint64(9, BigInt(chunk.duration ?? 0));
  view.setUint32(17, configBytes.byteLength);
  packet.set(configBytes, 21);
  packet.set(encoded, 21 + configBytes.byteLength);
  socket.send(packet);

  chunks++;
  chunkCount.textContent = String(chunks);
}

function send(socket, message) {
  socket.send(JSON.stringify(message));
}

function waitForSocket(socket) {
  return new Promise((resolve, reject) => {
    socket.addEventListener('open', resolve, { once: true });
    socket.addEventListener('error', () => reject(new Error('socket failed')), { once: true });
  });
}

function assertWebCodecs() {
  if (!('VideoEncoder' in window) || !('MediaStreamTrackProcessor' in window)) {
    throw new Error('WebCodecs or MediaStreamTrackProcessor is unavailable');
  }
}

function writeLog(line) {
  logBox.textContent = `${new Date().toLocaleTimeString()} ${line}\n${logBox.textContent}`;
}

function preferH264(transceiver) {
  const capabilities = RTCRtpReceiver.getCapabilities?.('video');
  if (!capabilities?.codecs?.length || !transceiver.setCodecPreferences) {
    return;
  }

  const h264 = capabilities.codecs.filter(codec => codec.mimeType.toLowerCase() === 'video/h264');
  const rest = capabilities.codecs.filter(codec => codec.mimeType.toLowerCase() !== 'video/h264');
  if (h264.length > 0) {
    transceiver.setCodecPreferences([...h264, ...rest]);
  }
}

function summarizeSdp(sdp) {
  return sdp
    .split(/\r?\n/)
    .filter(line =>
      line.startsWith('m=') ||
      line.startsWith('a=mid:') ||
      line.startsWith('a=setup:') ||
      line.startsWith('a=group:BUNDLE') ||
      line.startsWith('a=sendonly') ||
      line.startsWith('a=recvonly') ||
      line.startsWith('a=sendrecv') ||
      line.startsWith('a=inactive') ||
      line.startsWith('a=sctp-port:')
    )
    .join(' | ');
}
