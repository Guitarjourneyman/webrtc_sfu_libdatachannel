import { createServer } from 'node:http';
import { WebSocketServer } from 'ws';
export function startSignalingServer(sfu, port = 8000) {
    const server = createServer((_req, res) => {
        res.writeHead(200, { 'content-type': 'application/json' });
        res.end(JSON.stringify({ ok: true, role: 'signaling-ingest' }));
    });
    const signalWss = new WebSocketServer({ noServer: true });
    const ingestWss = new WebSocketServer({ noServer: true });
    const clients = new Map();
    server.on('upgrade', (request, socket, head) => {
        const pathname = new URL(request.url ?? '/', 'http://127.0.0.1').pathname;
        if (pathname === '/signal') {
            signalWss.handleUpgrade(request, socket, head, ws => {
                signalWss.emit('connection', ws, request);
            });
            return;
        }
        if (pathname === '/ingest') {
            ingestWss.handleUpgrade(request, socket, head, ws => {
                ingestWss.emit('connection', ws, request);
            });
            return;
        }
        socket.destroy();
    });
    sfu.on('localDescription', event => {
        const client = clients.get(event.peerId);
        console.log(`[sdp:${event.peerId}] local ${event.descriptionType}`);
        console.log(summarizeSdp(event.sdp));
        client?.socket.send(JSON.stringify({
            event: 'localDescription',
            data: {
                type: event.descriptionType,
                sdp: event.sdp
            }
        }));
    });
    sfu.on('localCandidate', event => {
        const client = clients.get(event.peerId);
        client?.socket.send(JSON.stringify({
            event: 'localCandidate',
            data: {
                candidate: event.candidate,
                mid: event.mid
            }
        }));
    });
    signalWss.on('connection', socket => {
        const client = { socket };
        socket.on('message', raw => {
            try {
                const request = JSON.parse(raw.toString());
                handleSignalRequest(sfu, clients, client, request);
                if (request.id) {
                    socket.send(JSON.stringify({ id: request.id, ok: true }));
                }
            }
            catch (error) {
                socket.send(JSON.stringify({
                    ok: false,
                    error: error instanceof Error ? error.message : String(error)
                }));
            }
        });
        socket.on('close', () => {
            if (client.peerId) {
                clients.delete(client.peerId);
                sfu.removeViewer(client.peerId);
            }
        });
    });
    ingestWss.on('connection', socket => {
        socket.on('message', (raw, isBinary) => {
            if (!isBinary) {
                return;
            }
            const packet = toBuffer(raw);
            sfu.ingestBrowserPacket(packet);
        });
    });
    server.listen(port, '127.0.0.1', () => {
        console.log(`[signal] ws://127.0.0.1:${port}/signal`);
        console.log(`[ingest] ws://127.0.0.1:${port}/ingest`);
    });
}
function handleSignalRequest(sfu, clients, client, request) {
    if (request.action === 'createViewer') {
        client.peerId = request.peerId;
        clients.set(request.peerId, client);
        sfu.createViewer(request.peerId);
        if (request.sdp) {
            console.log(`[sdp:${request.peerId}] remote offer`);
            console.log(summarizeSdp(request.sdp));
            sfu.setViewerOffer(request.peerId, request.sdp);
        }
        return;
    }
    if (request.action === 'viewerAnswer') {
        console.log(`[sdp:${request.peerId}] remote answer`);
        console.log(summarizeSdp(request.sdp));
        sfu.setViewerAnswer(request.peerId, request.sdp);
        return;
    }
    if (request.action === 'viewerCandidate') {
        sfu.addViewerCandidate(request.peerId, request.candidate, request.mid);
        return;
    }
    if (request.action === 'removeViewer') {
        clients.delete(request.peerId);
        sfu.removeViewer(request.peerId);
    }
}
function summarizeSdp(sdp) {
    return sdp
        .split(/\r?\n/)
        .filter(line => line.startsWith('m=') ||
        line.startsWith('a=mid:') ||
        line.startsWith('a=setup:') ||
        line.startsWith('a=group:BUNDLE') ||
        line.startsWith('a=sendonly') ||
        line.startsWith('a=recvonly') ||
        line.startsWith('a=sendrecv') ||
        line.startsWith('a=inactive') ||
        line.startsWith('a=sctp-port:') ||
        line.startsWith('a=fingerprint:'))
        .join('\n');
}
function toBuffer(raw) {
    if (Buffer.isBuffer(raw)) {
        return raw;
    }
    if (raw instanceof ArrayBuffer) {
        return Buffer.from(raw);
    }
    if (Array.isArray(raw)) {
        return Buffer.concat(raw);
    }
    return Buffer.from(raw);
}
