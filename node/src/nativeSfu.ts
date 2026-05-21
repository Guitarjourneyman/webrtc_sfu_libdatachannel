import { createRequire } from 'node:module';
import { EventEmitter } from 'node:events';

type NativeEvent =
  | {
      type: 'localDescription';
      peerId: string;
      descriptionType: 'offer' | 'answer';
      sdp: string;
    }
  | {
      type: 'localCandidate';
      peerId: string;
      candidate: string;
      mid: string;
    };

type NativeSfuBinding = {
  createViewer(peerId: string): void;
  setViewerOffer(peerId: string, sdp: string): void;
  setViewerAnswer(peerId: string, sdp: string): void;
  addViewerCandidate(peerId: string, candidate: string, mid: string): void;
  removeViewer(peerId: string): void;
  ingestBrowserPacket(packet: Buffer): void;
  close(): void;
};

type NativeConstructor = new (callback: (event: NativeEvent) => void) => NativeSfuBinding;

export class NativeSfuBridge extends EventEmitter {
  private readonly native: NativeSfuBinding;

  constructor() {
    super();
    const NativeSfu = loadNativeConstructor();
    this.native = new NativeSfu(event => this.emit(event.type, event));
  }

  createViewer(peerId: string): void {
    this.native.createViewer(peerId);
  }

  setViewerOffer(peerId: string, sdp: string): void {
    this.native.setViewerOffer(peerId, sdp);
  }

  setViewerAnswer(peerId: string, sdp: string): void {
    this.native.setViewerAnswer(peerId, sdp);
  }

  addViewerCandidate(peerId: string, candidate: string, mid: string): void {
    this.native.addViewerCandidate(peerId, candidate, mid);
  }

  removeViewer(peerId: string): void {
    this.native.removeViewer(peerId);
  }

  ingestBrowserPacket(packet: Buffer): void {
    this.native.ingestBrowserPacket(packet);
  }

  close(): void {
    this.native.close();
  }
}

function loadNativeConstructor(): NativeConstructor {
  const require = createRequire(import.meta.url);

  const binding = require('../build/Release/ldc_sfu_node.node') as {
    NativeSfu: NativeConstructor;
  };
  return binding.NativeSfu;
}
