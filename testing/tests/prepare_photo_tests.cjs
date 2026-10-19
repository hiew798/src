const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const { pathToFileURL } = require('node:url');
const { chromium } = require('playwright');

(async () => {
    const browser = await chromium.launch({ headless: true });
    try {
        const page = await browser.newPage({ viewport: { width: 390, height: 740 }, acceptDownloads: true });
        const errors = [];
        page.on('pageerror', error => errors.push(error.message));
        await page.goto(pathToFileURL(path.resolve(__dirname, '../prepare-test-photo.html')).href);
        await page.locator('#prepare').click();
        assert.match(await page.locator('#status').textContent(), /Choose a photo/);
        const svg = '<svg xmlns="http://www.w3.org/2000/svg" width="800" height="600"><rect width="800" height="600" fill="orange"/><rect x="100" y="80" width="600" height="440" fill="navy"/><text x="180" y="300" fill="white" font-size="60">Rescue test</text></svg>';
        await page.locator('#file').setInputFiles({ name: 'test.svg', mimeType: 'image/svg+xml', buffer: Buffer.from(svg) });
        await page.locator('#prepare').click();
        await page.waitForFunction(() => !document.getElementById('download').hidden);
        const result = await page.evaluate(async () => {
            const preview = document.getElementById('preview');
            await preview.decode();
            const bytes = await (await fetch(document.getElementById('download').href)).arrayBuffer();
            return { bytes: Array.from(new Uint8Array(bytes)), width: preview.naturalWidth, height: preview.naturalHeight,
                overflow: document.documentElement.scrollWidth > window.innerWidth };
        });
        const jpeg = Buffer.from(result.bytes);
        assert.ok(jpeg.length <= 8192);
        assert.equal(jpeg.readUInt16BE(0), 0xffd8);
        assert.equal(jpeg.readUInt16BE(jpeg.length - 2), 0xffd9);
        assert.ok(result.width <= 160 && result.height <= 120);
        assert.equal(result.overflow, false);
        const downloaded = page.waitForEvent('download');
        await page.locator('#download').click();
        const download = await downloaded;
        assert.equal(download.suggestedFilename(), 'test-photo.jpg');
        assert.deepEqual(fs.readFileSync(await download.path()), jpeg);
        const screenshot = path.join(os.tmpdir(), 'heltec-transfer-preparation.png');
        await page.screenshot({ path: screenshot, fullPage: true });
        assert.deepEqual(errors, []);
        console.log(`PASS: offline preparation, JPEG limits, exact downloaded bytes, phone layout; ${jpeg.length} bytes. Screenshot: ${screenshot}`);
    } finally {
        await browser.close();
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
