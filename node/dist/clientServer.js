import express from 'express';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
export function startClientServer(port = 3000) {
    const __dirname = path.dirname(fileURLToPath(import.meta.url));
    const publicDir = path.resolve(__dirname, '..', 'public');
    const app = express();
    app.use(express.static(publicDir));
    app.get('/health', (_req, res) => {
        res.json({ ok: true, role: 'client-app' });
    });
    app.listen(port, '127.0.0.1', () => {
        console.log(`[client] http://127.0.0.1:${port}`);
    });
}
