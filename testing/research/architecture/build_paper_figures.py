"""Original manuscript figures. Reuses drawing primitives; never runs the guide build."""
from pathlib import Path
import subprocess
from reportlab.graphics import renderSVG, renderPDF
from build_architecture_guide import Diagram, BLUE, TEAL, GRAY, LIGHT, PALE, GREEN, AMBER, ORANGE

ROOT = Path(__file__).resolve().parents[2]
FIG = ROOT / 'research' / 'architecture' / 'figures'
TMP = ROOT / 'tmp' / 'pdfs' / 'paper-figures'
POPPLER = Path('C:/Users/cyftl/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/poppler/Library/bin/pdftoppm.exe')

def compression():
    g = Diagram(805)
    g.text(250, 28, '(a) Implemented browser preprocessing', 20, True)
    g.text(735, 28, '(b) JPEG encoding model [6], [7]', 20, True)
    g.line(495, 48, 495, 748, dash=True)
    g.box(45, 64, 410, 62, 'Camera or gallery file', 'Reject absent input or file size > 15 MiB')
    g.arrow([(250, 126), (250, 149)])
    g.box(45, 149, 410, 62, 'Decode original image', 'Decoded pixels occupy phone memory')
    g.arrow([(250, 211), (250, 235)])
    g.box(45, 235, 410, 72, 'Resize within next bound', '160 x 120, then 120 x 90, then 80 x 60\nPreserve aspect ratio; no enlargement', size=16)
    g.arrow([(250, 307), (250, 332)])
    g.box(45, 332, 410, 72, 'White canvas; grayscale pixels', 'Y = round(0.299R + 0.587G + 0.114B)\nWrite R = G = B = Y', size=16)
    g.arrow([(250, 404), (250, 428)])
    g.box(45, 428, 410, 72, 'Request JPEG encoding at next q', 'q = 0.60, 0.45, 0.30, 0.20\nBrowser codec: conceptual detail in (b)', size=16)
    g.arrow([(250, 500), (250, 527)])
    g.diamond(250, 569, 305, 84, 'Valid output and\nsize <= 8192 bytes?')
    g.arrow([(250, 611), (250, 647)], 'Yes', 282, 636, color=TEAL)
    g.box(45, 647, 410, 60, 'Accept first fitting JPEG', 'Upload binary bytes to the civilian node', GREEN, TEAL)
    g.arrow([(98, 569), (18, 569), (18, 464), (45, 464)], color=ORANGE)
    g.text(62, 603, 'No: next q*', 14, color=ORANGE)
    g.note(45, 724, 410, 60, '* After four failures, resize at the next bound.\nReject if all 12 candidate encodings fail.')
    g.box(535, 64, 390, 62, 'Prepare component sample blocks', 'Baseline illustration: 8 x 8 samples', LIGHT, GRAY)
    g.arrow([(730, 126), (730, 172)])
    g.box(535, 172, 390, 72, 'Discrete cosine transform', 'Pixel values -> spatial-frequency coefficients', LIGHT, GRAY)
    g.arrow([(730, 244), (730, 290)])
    g.box(535, 290, 390, 72, 'Quantise coefficients', 'Divide by quantisation steps; round', AMBER, ORANGE)
    g.arrow([(730, 362), (730, 408)])
    g.box(535, 408, 390, 72, 'Order and represent coefficients', 'DC differences; zigzag AC / zero runs', LIGHT, GRAY)
    g.arrow([(730, 480), (730, 526)])
    g.box(535, 526, 390, 72, 'Entropy coding', 'Huffman coding in the baseline model', LIGHT, GRAY)
    g.arrow([(730, 598), (730, 647)])
    g.box(535, 647, 390, 60, 'JPEG byte stream', 'Return encoded Blob to the size check', GREEN, TEAL)
    g.note(535, 724, 390, 60, 'The application supplies pixels and q.\nCodec details are selected by the browser.')
    return g.d

def modes():
    g = Diagram(595)
    titles = ['(a) Mode 1\nBaseline', '(b) Mode 2\nTuned', '(c) Mode 3\nFixed', '(d) Mode 4\nAdaptive']
    for x, title in zip((120, 360, 600, 840), titles):
        g.text(x, 24, title, 19, True)
    g.box(20, 82, 920, 55, 'Common entry: associated SOS acknowledged; image manifest accepted', size=18)
    rows = [
        [('Send chunk i', 'Up to 180 image bytes'), ('Wait for chunk ACK', 'Normal timer starts at queueing'), ('Schedule next chunk', '300 ms post-ACK delay'), ('Repeat / retry missing', '200 ms planning interval')],
        [('Send chunk i', 'Same chunk format'), ('Wait for chunk ACK', 'Image timer starts after TX'), ('Schedule next chunk', '30 ms post-ACK delay'), ('Repeat / retry missing', '20 ms planning interval')],
        [('Send group', '4 missing chunks'), ('Query receiver bitmap', 'One reply reports saved chunks'), ('Select remaining chunks', 'Retry missing; include new'), ('Repeat group', 'Fixed cap; shorter final group')],
        [('Choose group size', 'Start at 2; airtime-bounded cap'), ('Send group; query bitmap', 'Query retry on first timeout'), ('Measure confirmed rate', 'New bytes / elapsed time'), ('Probe / retain group size', 'Accept trial above +5% rate')],
    ]
    for col, x in enumerate((20, 260, 500, 740)):
        g.arrow([(x+100, 137), (x+100, 164)])
        for row, (title, body) in enumerate(rows[col]):
            y = 164 + 82 * row
            g.box(x, y, 200, 60, title, body, fill=PALE if col < 2 else GREEN, stroke=BLUE if col < 2 else TEAL, size=15)
            if row < 3:
                g.arrow([(x+100, y+60), (x+100, y+82)])
        g.arrow([(x+100, 470), (x+100, 506)])
    g.box(20, 506, 920, 62, 'Common completion: persisted image, expected size and CRC32 verified, completion ACK', 'Sender saves delivery state; phone observes the next status update.', GREEN, TEAL, size=17)
    g.text(480, 589, 'Logical paths only. Retry loops and the common manifest exchange are abbreviated.', 15, color=GRAY)
    return g.d

def build():
    TMP.mkdir(parents=True, exist_ok=True)
    for name, drawing in [('10-paper-compression-architecture', compression()), ('11-paper-four-mode-architecture', modes())]:
        svg = renderSVG.drawToString(drawing)
        if isinstance(svg, bytes): svg = svg.decode('utf-8')
        (FIG / (name + '.svg')).write_text(svg, encoding='utf-8')
        pdf = TMP / (name + '.pdf')
        renderPDF.drawToFile(drawing, str(pdf))
        subprocess.run([str(POPPLER), '-png', '-singlefile', '-scale-to', '3000', str(pdf), str(FIG/name)], check=True, capture_output=True)
        print(name)

if __name__ == '__main__':
    build()
