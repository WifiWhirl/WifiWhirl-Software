import { defineConfig } from 'vite';
import preact from '@preact/preset-vite';
import { existsSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';

// Dev target: a real WifiWhirl module. Set WIFIWHIRL_HOST for `npm run dev`.
const host = process.env.WIFIWHIRL_HOST || '192.168.4.1';
const target = host.startsWith('http') ? host : `http://${host}`;

// REST endpoints exposed by the firmware (http_routes.cpp). Proxy them so the
// dev server talks to a live module while serving the SPA locally.
const apiPaths = [
  '/getconfig', '/setconfig', '/getcommands', '/addcommand', '/editcommand',
  '/delcommand', '/getwebconfig', '/setwebconfig', '/getdevice', '/setdevice',
  '/getwifi', '/setwifi', '/scanwifi', '/resetwifi', '/getmqtt', '/setmqtt',
  '/getweather', '/getstates', '/gettemps', '/restart', '/sethardware',
  '/gethardware', '/getsmartschedule', '/setsmartschedule',
  '/gethlcal', '/starthlcal', '/cancelhlcal', '/setheatloss',
  '/updatesmartschedule', '/cancelsmartschedule', '/getpolldata',
  '/sendcommand', '/cmdq_file', '/auth/status', '/login', '/logout', '/update', '/support',
  '/logo.png', '/icon-192.png', '/icon-512.png', '/maskable-icon-192.png',
  '/maskable-icon-512.png', '/apple-touch-icon.png', '/favicon-light.png',
  '/favicon-dark.png', '/favicon.ico', '/manifest.json',
];

const proxy = Object.fromEntries(
  apiPaths.map((p) => [p, { target, changeOrigin: true }])
);

const generatedBundlePattern = /^(app|chunk|index|style)-[\w-]+\.(js|css)$/;

function cleanGeneratedBundles() {
  let outDir = '';

  return {
    name: 'clean-generated-bundles',
    apply: 'build' as const,
    configResolved(config: import('vite').ResolvedConfig) {
      outDir = resolve(config.root, config.build.outDir);
      if (!existsSync(outDir)) return;

      for (const entry of readdirSync(outDir, { withFileTypes: true })) {
        if (entry.isFile() && generatedBundlePattern.test(entry.name)) {
          rmSync(resolve(outDir, entry.name));
        }
      }
    },
    closeBundle() {
      // Keep raster source SVGs in frontend/public without shipping them in
      // the embedded firmware payload.
      if (!outDir) return;

      for (const sourceOnlyAsset of ['logo.svg', 'maskable_icon.svg', 'maskable_icon_dark.svg']) {
        rmSync(resolve(outDir, sourceOnlyAsset), { force: true });
      }

      const indexPath = resolve(outDir, 'index.html');
      if (!existsSync(indexPath)) return;

      const html = readFileSync(indexPath, 'utf8');
      const ordered = html.replace(
        /(\n\s*<script type="module"[^>]*><\/script>)\n(\s*<link rel="stylesheet"[^>]*>)/g,
        '\n$2$1',
      );
      if (ordered !== html) writeFileSync(indexPath, ordered);
    },
  };
}

export default defineConfig({
  plugins: [preact(), cleanGeneratedBundles()],
  base: '/',
  resolve: {
    alias: [
      { find: /^npm:preact@[^/]+\/jsx-runtime$/, replacement: 'preact/jsx-runtime' },
      { find: /^npm:preact@[^/]+\/hooks$/, replacement: 'preact/hooks' },
      { find: /^npm:preact@[^/]+$/, replacement: 'preact' },
    ],
  },
  build: {
    outDir: '../data',
    emptyOutDir: false, // keep runtime json + static assets already in data/
    target: 'es2018',
    minify: 'esbuild',
    cssCodeSplit: false,
    assetsDir: '',
    assetsInlineLimit: 0,
    rollupOptions: {
      output: {
        entryFileNames: 'app-[hash].js',
        chunkFileNames: 'chunk-[hash].js',
        assetFileNames: 'app-[hash][extname]',
        codeSplitting: false,
      },
    },
  },
  server: {
    port: 5173,
    proxy,
  },
});
