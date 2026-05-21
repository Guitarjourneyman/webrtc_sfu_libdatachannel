import { startClientServer } from './clientServer.js';
import { NativeSfuBridge } from './nativeSfu.js';
import { startSignalingServer } from './signalingServer.js';
const clientPort = Number(process.env.CLIENT_PORT ?? 3000);
const signalingPort = Number(process.env.SIGNALING_PORT ?? 8000);
const sfu = new NativeSfuBridge();
startClientServer(clientPort);
startSignalingServer(sfu, signalingPort);
