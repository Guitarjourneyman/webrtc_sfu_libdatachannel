import { createRequire } from 'node:module';
import { EventEmitter } from 'node:events';
export class NativeSfuBridge extends EventEmitter {
    native;
    constructor() {
        super();
        const NativeSfu = loadNativeConstructor();
        this.native = new NativeSfu(event => this.emit(event.type, event));
    }
    createViewer(peerId) {
        this.native.createViewer(peerId);
    }
    setViewerOffer(peerId, sdp) {
        this.native.setViewerOffer(peerId, sdp);
    }
    setViewerAnswer(peerId, sdp) {
        this.native.setViewerAnswer(peerId, sdp);
    }
    addViewerCandidate(peerId, candidate, mid) {
        this.native.addViewerCandidate(peerId, candidate, mid);
    }
    removeViewer(peerId) {
        this.native.removeViewer(peerId);
    }
    ingestBrowserPacket(packet) {
        this.native.ingestBrowserPacket(packet);
    }
    close() {
        this.native.close();
    }
}
function loadNativeConstructor() {
    const require = createRequire(import.meta.url);
    const binding = require('../build/Release/ldc_sfu_node.node');
    return binding.NativeSfu;
}
