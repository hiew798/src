"""Build source-traced vector diagrams and a PDF explanation. Does not touch firmware."""
from pathlib import Path
import math, json, hashlib, subprocess, re, zipfile
from xml.sax.saxutils import escape
from reportlab.graphics.shapes import Drawing, Rect, Line, String, Polygon, PolyLine
from reportlab.graphics import renderPDF, renderSVG
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.enums import TA_LEFT
from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, PageBreak, Table, TableStyle, KeepTogether
from reportlab.lib.pagesizes import A4

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'research' / 'architecture'
FIG = OUT / 'figures'
TMP = ROOT / 'tmp' / 'pdfs' / 'architecture'
PDF = ROOT / 'output' / 'pdf' / 'LoRa-Software-Architecture-and-Compression.pdf'
for p in (OUT, FIG, TMP, PDF.parent): p.mkdir(parents=True, exist_ok=True)
for name, path in [('Arial','arial.ttf'),('Arial-Bold','arialbd.ttf'),('Arial-Italic','ariali.ttf')]:
    pdfmetrics.registerFont(TTFont(name, str(Path('C:/Windows/Fonts') / path)))
pdfmetrics.registerFontFamily('Arial',normal='Arial',bold='Arial-Bold',italic='Arial-Italic',boldItalic='Arial-Bold')

INK = colors.HexColor('#17324d'); BLUE = colors.HexColor('#176b94')
TEAL = colors.HexColor('#147d73'); ORANGE = colors.HexColor('#a76516')
GRAY = colors.HexColor('#65758a'); LIGHT = colors.HexColor('#f1f5f9')
PALE = colors.HexColor('#eaf5f8'); GREEN = colors.HexColor('#e7f4ee')
AMBER = colors.HexColor('#fff4df'); RED = colors.HexColor('#a43942')
W = 960

class Diagram:
    def __init__(self, h=540):
        self.h=h; self.d=Drawing(W,h)
        self.d.add(Rect(0,0,W,h,fillColor=colors.white,strokeColor=None))
    def text(self,x,y,text,size=17,bold=False,color=INK,anchor='middle'):
        for i,line in enumerate(text.split('\n')):
            self.d.add(String(x,self.h-y-i*size*1.28,line,fontName='Arial-Bold' if bold else 'Arial',fontSize=size,fillColor=color,textAnchor=anchor))
    def box(self,x,y,w,h,title,body='',fill=PALE,stroke=BLUE,size=17):
        self.d.add(Rect(x,self.h-y-h,w,h,rx=9,ry=9,fillColor=fill,strokeColor=stroke,strokeWidth=1.5))
        lines=title.split('\n')
        if body:
            self.text(x+w/2,y+25,title,size,bold=True)
            self.text(x+w/2,y+29+len(lines)*size*1.28,body,size-2)
        else:
            self.text(x+w/2,y+h/2-(len(lines)-1)*size*.64+size*.32,title,size,bold=True)
    def arrow(self,pts,label='',lx=None,ly=None,color=BLUE,dash=False):
        p=[(x,self.h-y) for x,y in pts]
        kwargs={'strokeColor':color,'strokeWidth':1.8,'fillColor':None}
        if dash: kwargs['strokeDashArray']=[6,4]
        self.d.add(PolyLine([v for pair in p for v in pair],**kwargs))
        a,b=p[-2:]; theta=math.atan2(b[1]-a[1],b[0]-a[0]); size=9
        self.d.add(Polygon([b[0],b[1],b[0]-size*math.cos(theta-.45),b[1]-size*math.sin(theta-.45),b[0]-size*math.cos(theta+.45),b[1]-size*math.sin(theta+.45)],fillColor=color,strokeColor=color))
        if label: self.text(lx if lx is not None else (pts[0][0]+pts[-1][0])/2,ly if ly is not None else (pts[0][1]+pts[-1][1])/2-8,label,15,color=color)
    def diamond(self,cx,cy,w,h,text):
        self.d.add(Polygon([cx,self.h-(cy-h/2),cx+w/2,self.h-cy,cx,self.h-(cy+h/2),cx-w/2,self.h-cy],fillColor=AMBER,strokeColor=ORANGE,strokeWidth=1.5))
        self.text(cx,cy-(len(text.split('\n'))-1)*9+6,text,16,bold=True,color=ORANGE)
    def line(self,x1,y1,x2,y2,color=GRAY,dash=False):
        kw={'strokeColor':color,'strokeWidth':1.2}
        if dash: kw['strokeDashArray']=[5,5]
        self.d.add(Line(x1,self.h-y1,x2,self.h-y2,**kw))
    def note(self,x,y,w,h,text): self.box(x,y,w,h,text,fill=LIGHT,stroke=GRAY,size=15)

def overall():
    g=Diagram(525)
    g.text(480,25,'Two local Wi-Fi networks, one bidirectional LoRa link',22,True)
    for x,title,body in [(25,'Civilian phone','Camera / gallery\nBrowser UI + compression'),(655,'Rescuer phone','Browser dashboard\nPhoto display + replies')]:
        g.box(x,60,280,95,title,body)
    g.box(25,230,280,115,'Civilian ESP32','Web API + persistent queue\nLoRa transfer state machine',GREEN,TEAL)
    g.box(655,230,280,115,'Rescuer ESP32','Web API + image assembly\nPersistent receive state',GREEN,TEAL)
    for cx in (165,795):
        g.arrow([(cx-60,155),(cx-60,230)])
        g.arrow([(cx+60,230),(cx+60,155)])
        g.text(cx,195,'Wi-Fi / HTTPS',15,color=BLUE)
    g.arrow([(305,264),(655,264)],'SOS, JPEG chunks, chat',480,248)
    g.arrow([(655,317),(305,317)],'Device ACKs and reply chat',480,303,color=TEAL)
    g.box(25,400,280,75,'LittleFS: source copy','JPEG + report + delivery state',LIGHT,GRAY)
    g.box(655,400,280,75,'LittleFS: destination copy','Chunks + bitmap + final JPEG',LIGHT,GRAY)
    g.arrow([(165,345),(165,400)]);g.arrow([(795,345),(795,400)])
    g.note(342,370,276,113,'Each LoRa radio uses one antenna.\nTransmission and reception alternate.\nPhone-to-node traffic stays local.\nNo cloud service is in this path.')
    return g.d

def modules():
    g=Diagram(590)
    g.box(265,18,430,65,'Phone browser: data/app.js','HTML / CSS / JS served from WebAssets.h')
    g.arrow([(480,83),(480,120)],'Local web requests',595,108)
    g.box(270,120,420,80,'WebServerManager','Routing, sessions, JSON, JPEG upload / download')
    g.box(25,120,205,80,'Portal services','DNS + HTTP redirect',LIGHT,GRAY)
    g.arrow([(230,160),(270,160)])
    g.box(25,260,400,90,'StorageManager','Report records, JPEG fragments, CRC32\nCommit + read-back + bitmap persistence',GREEN,TEAL)
    g.box(535,260,400,90,'LoRaMeshManager','Frame queues, retries, receive / send\nDuplicate checks and authentication',GREEN,TEAL)
    g.arrow([(350,200),(350,260)]);g.arrow([(610,200),(735,200),(735,260)])
    g.arrow([(425,285),(535,285)],'Read / save',480,273)
    g.arrow([(535,325),(425,325)],color=TEAL)
    g.box(25,395,400,70,'LittleFS + flash','Persistent files',LIGHT,GRAY)
    g.box(535,395,190,70,'TransferPolicy','Mode 4 decisions',LIGHT,GRAY,size=16)
    g.box(750,395,185,70,'RadioLib / SX1262','Radio driver + SPI',LIGHT,GRAY,size=16)
    g.arrow([(225,350),(225,395)]);g.arrow([(630,350),(630,395)]);g.arrow([(842,350),(842,395)])
    g.note(25,502,910,64,'loopTask: mesh.update -> DNS -> HTTP redirect -> HTTPS -> mesh.update -> health log / yield\nRadio interrupt sets a flag; normal loop code handles frames and storage.')
    return g.d

def compression():
    g=Diagram(755)
    g.box(320,12,320,55,'Camera or gallery file')
    g.arrow([(480,67),(480,98)])
    g.diamond(480,138,295,80,'File present and\nsize <= 15 MiB?')
    g.box(730,107,210,65,'Reject input',fill=AMBER,stroke=ORANGE)
    g.arrow([(627,138),(730,138)],'No',679,125,color=ORANGE)
    g.arrow([(480,178),(480,204)],'Yes',516,196)
    g.box(285,204,390,65,'Decode original image','Browser image.decode()')
    g.arrow([(480,269),(480,297)])
    g.box(230,297,500,75,'Choose dimension bound; preserve aspect ratio','160 x 120 -> 120 x 90 -> 80 x 60')
    g.arrow([(480,372),(480,399)])
    g.box(230,399,500,75,'Draw on white canvas; convert pixels to gray','gray = round(0.299R + 0.587G + 0.114B)')
    g.arrow([(480,474),(480,501)])
    g.box(285,501,390,66,'Encode JPEG at next quality','0.60 -> 0.45 -> 0.30 -> 0.20')
    g.arrow([(480,567),(480,590)])
    g.diamond(480,628,300,76,'Blob exists and\nbytes <= 8192?')
    g.box(725,590,215,76,'Return first fit','Upload binary JPEG',GREEN,TEAL)
    g.arrow([(630,628),(725,628)],'Yes',678,614,color=TEAL)
    g.arrow([(330,628),(165,628),(165,534),(285,534)],'No: next quality',235,610,color=ORANGE)
    g.arrow([(165,534),(55,534),(55,335),(230,335)],'Qualities exhausted',135,321,color=ORANGE)
    g.note(220,702,720,40,'If all 3 bounds x 4 qualities fail: report an error; the saved SOS remains.')
    return g.d

def lifecycle():
    g=Diagram(610)
    lanes=[(110,'Phone browser'),(385,'Civilian node'),(750,'Rescuer node')]
    for x,title in lanes:
        g.box(x-90,10,180,48,title,size=17);g.line(x,58,x,575,dash=True)
    events=[(92,110,385,'POST SOS JSON'),(158,385,110,'Local save response'),(207,385,750,'SOS frame'),(265,750,385,'ACK after report save'),(333,110,385,'POST prepared JPEG bytes'),(413,385,750,'Image manifest, chunks, replies'),(496,750,385,'Final completion ACK'),(550,385,110,'State poll: received by device')]
    for y,a,b,t in events:g.arrow([(a,y),(b,y)],t,(a+b)/2,y-10,color=TEAL if b<a else BLUE)
    g.note(425,105,255,50,'SOS saved in LittleFS')
    g.note(780,205,165,45,'Save report')
    g.note(425,282,490,48,'Photo compression/upload may overlap SOS delivery.\nRadio image transfer waits for a known rescuer target.')
    g.note(425,348,290,35,'JPEG + metadata saved locally')
    g.note(560,435,375,30,'Verify complete image; save completion')
    return g.d

def stopwait(tuned=False):
    g=Diagram(585)
    for x,title in [(180,'Civilian sender'),(720,'Rescuer receiver')]:
        g.box(x-120,15,240,50,title);g.line(x,65,x,548,dash=True)
    g.arrow([(180,101),(720,101)],'IMAGE manifest: bytes + CRC32')
    g.arrow([(720,153),(180,153)],'Manifest ACK',450,142,color=TEAL)
    g.arrow([(180,228),(720,228)],'CHUNK i (up to 180 JPEG bytes)')
    g.note(744,239,195,50,'Write, read back,\nsave bitmap')
    g.arrow([(720,308),(180,308)],'ACK for chunk i',450,297,color=TEAL)
    if tuned:
        g.note(15,332,330,58,'30 ms post-ACK delay\n20 ms planner; 0-39 ms image jitter')
        g.note(350,332,570,58,'ACK deadline is re-based after successful radio TX,\nusing airtime and estimated return-path length.')
    else:
        g.note(15,332,330,58,'300 ms post-ACK delay\n200 ms planner; 0-249 ms jitter')
        g.note(350,332,570,58,'Normal retry deadline: queue time + 15 s + jitter.\nThe fourth attempt uses the existing long cooldown.')
    g.arrow([(180,430),(720,430)],'CHUNK i+1; repeat until last chunk')
    g.arrow([(720,510),(180,510)],'Final chunk ACK with COMPLETE flag',450,499,color=TEAL)
    g.text(480,573,'Sequence schematic: arrow spacing is not a measured time scale.',15,color=GRAY)
    return g.d

def fixed():
    g=Diagram(650)
    for x,title in [(170,'Civilian sender'),(725,'Rescuer receiver')]:
        g.box(x-120,12,240,48,title);g.line(x,60,x,610,dash=True)
    g.arrow([(170,97),(725,97)],'IMAGE manifest')
    g.arrow([(725,130),(170,130)],'Initial bitmap reply',450,119,color=TEAL)
    g.arrow([(170,160),(725,160)],'GROUP_CHUNK 0')
    g.arrow([(170,204),(500,204)],'GROUP_CHUNK 1 (illustrative loss)',440,192,color=ORANGE)
    g.text(514,211,'X',22,True,ORANGE)
    g.arrow([(170,248),(725,248)],'GROUP_CHUNK 2')
    g.arrow([(170,292),(725,292)],'GROUP_CHUNK 3')
    g.note(752,153,185,153,'Each received chunk:\nwrite + read-back\n+ save progress.\nNo individual\nchunk ACK.')
    g.arrow([(170,349),(725,349)],'BITMAP_QUERY (unique request ID)')
    g.arrow([(725,401),(170,401)],'BITMAP_ACK: [1, 0, 1, 1] for chunks [0, 1, 2, 3]',450,389,color=TEAL)
    g.note(20,433,630,48,'Next group selects missing chunks: retry 1 plus new chunks 4, 5, 6.\nIt does not retransmit chunks already committed at the receiver.')
    g.arrow([(170,520),(725,520)],'Next group, then another query')
    g.arrow([(725,580),(170,580)],'Final bitmap includes COMPLETE after full-image verification',450,568,color=TEAL)
    g.text(480,635,'Data frames include listening gaps. Group size is four; the final group may be shorter.',15,color=GRAY)
    return g.d

def adaptive():
    g=Diagram(720)
    g.box(270,10,420,62,'Start image: preferred group = 2','Apply max-chunk and data-airtime limits')
    g.arrow([(480,72),(480,107)])
    g.box(270,107,420,65,'Plan missing chunks; start group timer','Urgent traffic may limit / shorten a group')
    g.arrow([(480,172),(480,195)])
    g.box(270,195,420,55,'Transmit the selected data group')
    g.arrow([(480,250),(480,270)])
    g.box(270,270,420,50,'Query receiver bitmap')
    g.arrow([(480,320),(480,345)])
    g.diamond(480,385,300,80,'Matching reply\nbefore deadline?')
    g.box(730,345,215,105,'Timeout count','1: retry query\n2: smaller future size\n4: pause / manifest',AMBER,ORANGE,size=16)
    g.arrow([(630,385),(730,385)],'No',680,372,color=ORANGE)
    g.arrow([(840,345),(840,295),(690,295)],'1-3',878,324,color=ORANGE)
    g.arrow([(480,425),(480,448)],'Yes',515,441,color=TEAL)
    g.box(270,448,420,60,'Save bitmap; measure confirmed bytes / time','Stale replies rejected; duplicate bytes not counted')
    g.arrow([(480,508),(480,533)])
    g.box(270,533,420,75,'Evaluate preferred size or trial','Accept a trial only above +5% measured rate.\nProbe again after 3 preferred-size samples.')
    g.arrow([(270,568),(150,568),(150,143),(270,143)],'More chunks',85,400)
    g.arrow([(480,608),(480,645)],'All chunks + COMPLETE',660,632,color=TEAL)
    g.box(270,645,420,50,'Save delivery; expose result to UI',fill=GREEN,stroke=TEAL)
    return g.d

def persistence():
    g=Diagram(670)
    g.box(255,15,450,62,'Receive addressed image chunk','Validate frame, source, index and byte count')
    g.arrow([(480,77),(480,108)])
    g.box(255,108,450,75,'Write .part at index x 180','Flush / close; read back and compare bytes',GREEN,TEAL)
    g.arrow([(480,183),(480,214)])
    g.box(255,214,450,75,'Set chunk bit; commit record metadata','Write temporary record; verify; rename into place',GREEN,TEAL)
    g.arrow([(480,289),(480,318)])
    g.diamond(480,359,300,82,'All expected\nchunk bits set?')
    g.box(20,327,230,102,'Partial receipt','Modes 1/2: chunk ACK\nModes 3/4: reply\nwhen queried',LIGHT,GRAY,size=16)
    g.arrow([(330,359),(250,359)],'No',290,345,color=GRAY)
    g.arrow([(480,400),(480,431)],'Yes',515,419)
    g.box(255,431,450,75,'Verify image size, CRC32 and JPEG boundaries','Rename .part to .jpg; save complete state',GREEN,TEAL)
    g.box(740,430,205,97,'Verification failure','Reset bitmap;\nrequest / expose\nretransmission',AMBER,ORANGE,size=16)
    g.arrow([(705,466),(740,466)],color=ORANGE)
    g.arrow([(480,506),(480,541)],'Success',533,529,color=TEAL)
    g.box(255,541,450,66,'Destination completion acknowledgement','Sender saves received status before reporting delivery')
    g.note(40,627,880,30,'Any failed write or metadata commit prevents a successful receipt for that operation.')
    return g.d

FIGURES=[
 ('01-system-architecture',overall),('02-software-components',modules),
 ('03-image-compression',compression),('04-sos-image-lifecycle',lifecycle),
 ('05-mode-1-baseline',lambda:stopwait(False)),('06-mode-2-tuned',lambda:stopwait(True)),
 ('07-mode-3-fixed',fixed),('08-mode-4-adaptive',adaptive),('09-storage-acknowledgement',persistence)]

SECTIONS = [
 dict(title='1. System architecture',fig=0,caption='Figure 1. Deployment and data paths of the current two-node system. The radio link carries both application data and destination acknowledgements.',
 paragraphs=[
 'The civilian phone connects to the civilian node and the rescuer phone connects to the rescuer node. Both nodes host their own local web interface. The phones do not communicate directly across the LoRa link: the ESP32 firmware stores application records, creates radio frames and interprets replies.',
 'A single firmware codebase is compiled with a civilian or rescuer role. The civilian can originate SOS reports and attach one image per report. Both roles can originate chat within an established report conversation. The rescuer receives and reconstructs photos, replies to chat and can mark a report resolved.',
 'The current server code uses HTTPS on port 443, an HTTP-to-HTTPS redirect on port 80, and a DNS portal service. Wi-Fi association is open; the rescuer dashboard has a separate application login. These diagrams follow the active server code even where older interface wording mentions HTTP.',
 'The radio is half duplex: it alternates between transmit and receive using the same LoRa antenna. A radio transmission finishing locally only means the frame left the sender. Destination acknowledgement is a later event, after the receiver processes and saves the data.'
 ],source='testing.ino:17; WebServerManager.h:270; LoRaMeshManager.h:304'),
 dict(title='2. Software modules and runtime',fig=1,caption='Figure 2. Software responsibilities and cooperative execution. Boxes are logical modules, not separate parallel threads.',
 paragraphs=[
 'The editable web source is in data/index.html, data/app.js and data/style.css. An embedding utility generates WebAssets.h, placing those resources in firmware flash. The ESP32 serves the JavaScript, but the connected browser executes it. WebServerManager validates requests, checks session access and exposes reports, image uploads and state queries.',
 'StorageManager owns persistent records and image files. LoRaMeshManager owns a 16-slot transient frame queue, retry state, duplicate suppression, destination routing and radio operation. PacketDefs specifies binary fields and frame validation; TransferConfig selects a mode; TransferPolicy supplies airtime estimates and adaptive decisions. RadioLib controls the SX1262 through SPI.',
 'Application handlers execute sequentially on loopTask. web.update calls mesh.update before and after DNS/web service. Long image reads and writes also call mesh.update at buffer boundaries. The radio interrupt sets a flag; the normal loop reads the radio and processes frames. The update loop additionally polls radio interrupt status to recover missed notification edges.',
 'This shared execution model explains why a slow web operation can delay radio service. Cooperative calls reduce that delay but do not create an independent radio task or a hard latency guarantee. The loop stack is configured to 16 KiB; health logs expose minimum stack headroom, heap availability and the longest observed web/radio service gaps.'
 ],source='data/app.js:1; WebServerManager.h:22,316; LoRaMeshManager.h:18,552; Config.h:37'),
 dict(title='3. Image-compression architecture',fig=2,caption='Figure 3. Exact search order in compressPhoto(). The outer loop reduces dimensions; the inner loop tries JPEG quality hints. Every dimension candidate is drawn from the decoded original.',
 paragraphs=[
 'Compression happens inside JavaScript running in the phone browser. The file picker returns a camera or gallery File. The function rejects missing input and files larger than 15 x 1024 x 1024 bytes, then creates an object URL and awaits image.decode(). It revokes the URL after decode; the decoded image remains available for drawing.',
 'The outer loop tries bounding boxes of 160 x 120, 120 x 90 and 80 x 60 pixels. At each size it draws the image on a white canvas, converts the pixels to gray, and tries JPEG quality hints of 0.60, 0.45, 0.30 and 0.20. The first non-null Blob of at most 8192 bytes is returned. If every candidate fails, the photo operation reports an error; an SOS already saved locally remains stored.',
 'The loop therefore performs at most twelve JPEG encoding attempts. It favours the larger bounding box before trying the next smaller one. It stops at the first acceptable file rather than comparing every candidate for perceptual quality. The ESP32 receives the final binary JPEG and does not perform this resizing or JPEG encoding.'
 ],source='data/app.js:60-83; WebAssets.h:78-101'),
 dict(title='4. Compression: pixels, bytes and memory',fig=None,paragraphs=[
 '<b>Resize rule.</b> For original dimensions W x H and a candidate bound Bw x Bh, the scale is s = min(Bw/W, Bh/H, 1). Output dimensions are max(1, round(Ws)) x max(1, round(Hs)). The aspect ratio is preserved and smaller images are not enlarged. A 4000 x 3000 photo becomes 160 x 120 at the first bound. A 3000 x 4000 portrait becomes 90 x 120, not 160 x 120.',
 '<b>Pixel conversion.</b> After drawing, the code calculates gray = round(0.299R + 0.587G + 0.114B) for each canvas pixel and writes that value into R, G and B. The white background handles transparent input. The canvas still uses RGBA pixel data; equal colour channels do not turn it into a one-byte-per-pixel buffer.',
 '<b>JPEG encoding.</b> canvas.toBlob delegates encoding to the browser. The quality argument is an encoder hint: 0.60 does not mean that the file will be 60% of the original size or have a measured 60% visual quality. The application checks the actual Blob size after encoding. It does not configure quantisation tables or chroma-subsampling settings, and equal R/G/B input does not itself prove a single-component grayscale JPEG file.',
 '<b>Why a small output can still need substantial memory.</b> The full original is decoded before resizing. A 4000 x 3000 image represented as four bytes per pixel occupies approximately 48,000,000 bytes, or 45.8 MiB, for one pixel buffer alone. Browser, camera, encoder and duplicate buffers add overhead. The 15 MiB input check limits compressed file bytes, not decoded pixel count. A low-memory error after camera capture can therefore arise on the phone before the board sees the final JPEG; this code trace does not diagnose every such browser failure.',
 '<b>Where the savings occur.</b> Fewer pixels and lossy JPEG encoding reduce the bytes placed on the LoRa link. The receiver reconstructs the same small JPEG. It does not restore discarded resolution or colour detail. This is appropriate to evaluate as a constrained situational photo, not as preservation of the original full-resolution image.'
 ],table=[['Representation','Illustrative size','Location'],['Original 4000 x 3000 pixels','Compressed file size varies','Phone file / image decoder'],['One decoded RGBA buffer','48,000,000 bytes (45.8 MiB)','Phone browser memory'],['160 x 120 RGBA canvas','76,800 bytes (75 KiB)','Phone browser memory'],['Accepted JPEG Blob','At most 8192 bytes (8 KiB)','Uploaded to civilian ESP32']],source='data/app.js:60-77; WebServerManager.h:170-199'),
 dict(title='5. SOS, image upload and delivery',fig=3,caption='Figure 4. The local web response, rescuer SOS acknowledgement and final image acknowledgement are separate milestones. Compression and upload can overlap SOS delivery.',
 paragraphs=[
 'Pressing Save and send SOS first sends JSON to the civilian web API. After the report is saved, the browser receives a local response. If a photo was selected, the browser then compresses and uploads it. A local upload response means the source JPEG and metadata were saved on the civilian node; it does not establish radio delivery.',
 'The upload handler accepts image/jpeg, checks the 8192-byte limit and available storage, and reads the request in 512-byte buffers. It computes CRC32 while writing a temporary .part file, checks the stored file, renames it to .jpg and saves the image record. The report can have one immutable image; retrying an already-existing attachment returns that image record.',
 'The image scheduler waits until the associated SOS has been acknowledged and a rescuer target is known. It then exchanges the image manifest and chunks using the selected transfer mode. The source record can already say that its local file is complete while delivery is still pending: local file completeness and remote delivery are different fields.',
 'After the rescuer verifies and saves the final image, its completion acknowledgement lets the sender mark delivery received. The browser periodically requests state; normal visible-page polling is scheduled every five seconds. A UI stopwatch can therefore include polling delay and web latency in addition to radio delivery time. Serial START/DELIVERED events provide a more precise protocol measurement.'
 ],source='data/app.js:216-224,260; WebServerManager.h:170-199; LoRaMeshManager.h:416-421'),
 dict(title='6. Mode 1: baseline stop-and-wait',fig=4,caption='Figure 5. Mode 1 sends one image chunk and waits for its acknowledgement before advancing. Manifest and final-completion exchanges are part of the image transfer.',
 paragraphs=[
 'An IMAGE manifest identifies the associated report, expected JPEG byte count and CRC32. After the receiver accepts it, the sender transmits the lowest unacknowledged chunk. A normal chunk acknowledgement advances the stored bitmap. The final chunk acknowledgement carries COMPLETE only after image verification and completion-state persistence.',
 'The baseline retains a 200 ms planning interval, a 300 ms post-image-ACK delay and transmit jitter in the range 0-249 ms. Its ordinary retry deadline is set from queueing, using 15 s plus 0-1999 ms jitter. The fourth queued attempt enters the existing approximately 120 s cooldown. An ACK can still complete the pending operation during that cooldown.',
 'Stop-and-wait is simple because only one chunk is awaiting acknowledgement. Its cost is repeated turnaround: data airtime, receiver processing, ACK airtime, scheduling delay and any retry wait are paid for each chunk. A lost acknowledgement can produce a repeated chunk even when the receiver already stored it.',
 'For an 8192-byte JPEG, 46 chunks are needed: 45 full chunks of 180 bytes and a final 92-byte chunk. This is an arithmetic example, not the byte count of the user\'s earlier photographs. One-at-a-time acknowledgement is unchanged by the image\'s visual contents.'
 ],source='LoRaMeshManager.h:154-185,405-457; Config.h:44-47'),
 dict(title='7. Mode 2: tuned stop-and-wait',fig=5,caption='Figure 6. Mode 2 keeps individual chunk acknowledgements and shortens scheduling delays. The sequence is unchanged; timing and receive handling differ.',
 paragraphs=[
 'Mode 2 preserves the manifest, individual chunk ACK and final verification path. The planner runs on a 20 ms interval; image progress uses a 30 ms post-ACK delay and image-related enqueue jitter of 0-39 ms. These settings reduce avoidable idle time without changing the JPEG or radio modulation.',
 'After a successful image-frame transmission, the acknowledgement deadline is re-based on calculated request/ACK airtime and the estimated route length. The return-path estimate comes from ACK TTL with a configured lower bound. A four-attempt cooldown remains. SOS and chat do not automatically inherit every image-specific timing rule.',
 'Modes 2-4 also share receive-in-progress protection: before starting channel assessment, the scheduler checks whether a valid incoming packet is still being received and allows it time to finish. Channel assessment can otherwise switch the radio out of receive mode. A busy channel defers the queued frame.',
 'This mode tests whether delay reduction alone helps. It still pays an acknowledgement exchange per chunk, so it cannot remove the per-chunk round-trip structure. Faster timing can help on a stable link, while real delays from web service, storage, radio contention and retransmission still remain.'
 ],source='TransferPolicy.h:20-27; LoRaMeshManager.h:142-144,459-474,614-629'),
 dict(title='8. Mode 3: fixed groups and bitmap ACKs',fig=6,caption='Figure 7. Illustrative loss of chunk 1 in a four-chunk group. Bitmap positions shown in brackets follow the labelled chunk order, not a printed integer\'s bit order.',
 paragraphs=[
 'After the initial manifest/bitmap exchange, the sender selects up to four currently missing chunks. GROUP_CHUNK frames are written and committed by the receiver without an individual ACK. The sender then issues BITMAP_QUERY; the receiver returns a full image bitmap and the exact query packet ID.',
 'In the example, chunks 0, 2 and 3 are committed, while chunk 1 is absent. The next group can contain the missing chunk 1 together with new chunks 4, 5 and 6. Selective retransmission avoids re-sending chunks already confirmed. A matching reply can also reveal a receiver progress reset after failed CRC verification.',
 'Grouped transfer is not continuous transmit-only operation. Each local data frame is followed by at least a 200 ms listening gap, with additional spacing for estimated relay hops. The four-chunk size stays fixed even when reception quality changes; the last group may contain fewer chunks.',
 'If a reply is lost, the current group\'s bitmap is queried again. After four unanswered requests, the sender pauses and later recovers through a manifest exchange. Final delivery requires a full bitmap and COMPLETE, not merely successful transmission of the last radio frame. Grouping reduces ACK frequency but adds query frames and retains storage and listening costs.'
 ],source='TransferConfig.h:15-27; LoRaMeshManager.h:187-244,351-403; StorageManager.h:224-253'),
 dict(title='9. Mode 4: adaptive policy',fig=7,caption='Figure 8. Mode 4 uses the same grouped receiver as mode 3. Adaptation is a sender-side policy over group size, measured delivery rate and acknowledgement waiting.',
 paragraphs=[
 'Each image starts with a preferred group size of two. A group\'s rate is newly confirmed JPEG bytes divided by elapsed time from planning until the sender saves the matching bitmap. Waiting for replies counts in that elapsed time. Already-confirmed bytes are excluded, and partial chunks contribute only their actual bytes.',
 'After an initial sample, the policy tries a neighbouring size by doubling or halving it within bounds. It adopts a trial only when its measured rate exceeds the preferred estimate by more than 5%. Preferred-size samples update a weighted estimate (75% previous, 25% new), and another probe occurs after three such samples. Missing chunks prompt a smaller trial.',
 'One unanswered request retries the query without shrinking the group; two permit a smaller preferred size for later groups. The already-sent group still needs bitmap recovery. Four unanswered requests enter pause/manifest recovery. The reply timeout tracks smoothed delay and variation, with a path/airtime floor and a 60 s ceiling.',
 'Group size is capped by eight configured chunks and an 8 s data-airtime budget. At the current full-frame airtime this gives five chunks. Urgent traffic can limit the next group or close it at a frame boundary. Interrupted or short final groups do not train the policy. A valid full COMPLETE reply still ends the transfer even if its sample is ineligible for learning.'
 ],source='TransferPolicy.h:38-155; TransferConfig.h:15-31; LoRaMeshManager.h:197-244,351-403'),
 dict(title='10. Persistent reception and ACK semantics',fig=8,caption='Figure 9. Successful reception is tied to saved state. Full image verification and committed completion are required before final acknowledgement.',
 paragraphs=[
 'A received frame is validated for packet structure, protocol version, destination and authentication before application processing. The image receiver checks the source, chunk index and expected byte count. The file offset is index x 180. A successfully written chunk is flushed, closed and read back before its bit is added to the record.',
 'Record saving writes a temporary metadata file containing its own checksum, reads it back and replaces the destination record by rename. If this save fails, the operation does not receive a success acknowledgement. A duplicate committed chunk can be acknowledged without writing its image bytes again.',
 'Once all expected chunks exist, the receiver checks byte length, CRC32 and JPEG start/end marker bytes. These checks establish expected file integrity and basic JPEG boundaries; they do not decode the image or prove perceptual usefulness. On a CRC failure, progress is reset and data is requested again. On success, the file is renamed to .jpg and completion metadata is saved.',
 'The receiver can repair an interrupted finalisation where the completed JPEG exists but completion metadata is absent. Sender copies and receiver progress support retry/recovery, but finite storage, hardware faults and explicit Clear all operations remain relevant. Persistent acknowledgement is a defined delivery condition, not proof of zero data loss under every failure.'
 ],source='StorageManager.h:154-171,180-253; LoRaMeshManager.h:154-244,326-344'),
 dict(title='11. Timing, evidence and research interpretation',fig=None,paragraphs=[
 '<b>Separate the time intervals.</b> Phone capture/decode/compression, Wi-Fi upload, waiting for the SOS target, LoRa image transfer and browser refresh are distinct stages. A user-facing stopwatch can include all of them. Compare protocols with identical saved JPEG bytes and a consistent start/end definition; record compression and upload separately.',
 '<b>Physical airtime remains.</b> A full current chunk frame contains a 39-byte application header, 3-byte chunk prefix, 180 JPEG bytes and an 8-byte authentication tag: 230 bytes supplied to the radio. At SF9, 125 kHz, coding rate 4/7 and an eight-symbol preamble, the code\'s airtime model gives approximately 1.574 s for that frame. For an 8192-byte image, data-frame airtime alone is approximately 71.8 s. This calculated example excludes manifests, queries, ACKs, listening gaps, retries and software delays.',
 '<b>Why adaptive need not always win.</b> A short image may finish before enough groups are observed. A probe can cost time, and changing contention can make a previously faster size slower. Fixed groups may already suit a particular link. The 8 s cap applies to data airtime only, so it is not an upper bound on complete group time or urgent-message latency.',
 '<b>Position the contribution accurately.</b> IEEE work already covers compression [1], a dual-receiver image architecture [2], multi-packet transmission [3] and bitmap-ACK image transfer [4]. The project-specific subject to evaluate is its measured-goodput group policy, query-first reply recovery and emergency-message scheduling on the present hardware. Architecture diagrams document the implementation; they do not prove novelty or superior performance.',
 '<b>Current evidence.</b> The earlier 100 s and 74 s runs used different compressed images. No percentage improvement is established by that pair. Figures in this pack are explanatory schematics, and the lost packet in Figure 7 is an example rather than an observed test trace.'
 ],source='PacketDefs.h:10-45; TransferPolicy.h:8-14; research/Methodology-IEEE-HD-draft.md'),
]

REFERENCES = [
 ('[1] Y. Onykiienko, S. Naida, M. Mazin, I. Kyrylenko, and P. Popovych, "An Efficient Image Compression Method for LoRa Networks," IEEE STEE, 2026, pp. TT2.14.1-TT2.14.5.', 'https://doi.org/10.1109/STEE63704.2026.11663995'),
 ('[2] S. De et al., "Interleaved Reception in LoRa Using IFT: A Hardware-Efficient Architecture for Fast and Low-Power Image Transmission," IEEE Internet of Things Journal, vol. 12, no. 19, pp. 39777-39787, 2025.', 'https://doi.org/10.1109/JIOT.2025.3588064'),
 ('[3] J. Torres, M. Nunez, and J. M. Mauricio Villanueva, "LoRa Multi-Packet Protocol for Image Transmission," IEEE INSCIT, 2024, pp. 1-6.', 'https://doi.org/10.1109/INSCIT62583.2024.10693388'),
 ('[4] T. Chen, D. Eager, and D. Makaroff, "Efficient Image Transmission Using LoRa Technology In Agricultural Monitoring IoT Systems," IEEE iThings/GreenCom/CPSCom/SmartData, 2019, pp. 937-944.', 'https://doi.org/10.1109/iThings/GreenCom/CPSCom/SmartData.2019.00166'),
]

STYLES={
 'title':ParagraphStyle('title',fontName='Arial-Bold',fontSize=28,leading=33,textColor=INK,spaceAfter=18),
 'heading':ParagraphStyle('heading',fontName='Arial-Bold',fontSize=19,leading=23,textColor=INK,spaceAfter=12),
 'body':ParagraphStyle('body',fontName='Arial',fontSize=10.1,leading=14.0,textColor=INK,spaceAfter=9),
 'caption':ParagraphStyle('caption',fontName='Arial-Italic',fontSize=9,leading=12,textColor=GRAY,spaceAfter=12),
 'small':ParagraphStyle('small',fontName='Arial',fontSize=8.2,leading=11,textColor=GRAY,spaceAfter=6),
 'cell':ParagraphStyle('cell',fontName='Arial',fontSize=9.3,leading=12,textColor=INK),
}

def table(rows,widths):
    t=Table([[Paragraph(escape(str(c)),STYLES['cell']) for c in row] for row in rows],colWidths=widths,hAlign='LEFT')
    t.setStyle(TableStyle([('BACKGROUND',(0,0),(-1,0),PALE),('ROWBACKGROUNDS',(0,1),(-1,-1),[colors.white,LIGHT]),('VALIGN',(0,0),(-1,-1),'TOP'),('LEFTPADDING',(0,0),(-1,-1),9),('RIGHTPADDING',(0,0),(-1,-1),9),('TOPPADDING',(0,0),(-1,-1),9),('BOTTOMPADDING',(0,0),(-1,-1),9),('LINEBELOW',(0,0),(-1,0),1,BLUE)]))
    return t

def footer(c,doc):
    c.setStrokeColor(colors.HexColor('#d6e0e8'));c.line(42,40,A4[0]-42,40)
    c.setFont('Arial',8);c.setFillColor(GRAY)
    c.drawString(42,27,'LoRa software architecture | Source inspection: 18 October 2026')
    c.drawRightString(A4[0]-42,27,str(doc.page))

def clean_markup(s):
    return re.sub(r'</?b>','**',s)

def source_links(s):
    parts=[]
    for item in s.split('; '):
        match=re.match(r'([^:]+):(\d+)',item)
        if match:
            parts.append(f'[{item}]({(ROOT/match[1]).as_posix()}:{match[2]})')
        else: parts.append(f'[{item}]({(ROOT/item).as_posix()})')
    return '; '.join(parts)

def build():
    drawings=[]
    poppler=Path('C:/Users/cyftl/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/poppler/Library/bin/pdftoppm.exe')
    for name,fn in FIGURES:
        d=fn();drawings.append(d)
        svg=renderSVG.drawToString(d)
        if isinstance(svg,bytes):svg=svg.decode('utf-8')
        svg_file=FIG/(name+'.svg')
        changed=not svg_file.exists() or svg_file.read_text(encoding='utf-8')!=svg or not (FIG/(name+'.png')).exists()
        if changed:
            svg_file.write_text(svg,encoding='utf-8')
            renderPDF.drawToFile(d,str(TMP/(name+'.pdf')))
            subprocess.run([str(poppler),'-png','-singlefile','-scale-to','2400',str(TMP/(name+'.pdf')),str(FIG/name)],check=True,capture_output=True)
    story=[Paragraph('Software architecture<br/>and image compression',STYLES['title']),Paragraph('An implementation guide for the four-mode LoRa emergency communication prototype',STYLES['heading'])]
    intro=[
      'Prepared 18 October 2026 from the local source. Nine original vector diagrams explain the system, software modules, compression pipeline, message lifecycle, four transfer modes and persistent acknowledgement path.',
      'The diagrams represent the current implementation, not a proposed redesign. Radio settings are 915 MHz, 125 kHz bandwidth, SF9, coding rate 4/7, configured output power 22 dBm and an eight-symbol preamble. The current adaptive revision is adaptive-goodput-1.',
      'This is a supporting explanation pack, not the complete ten-page FYP paper. For the paper, select the system architecture, compression flow and adaptive policy figures; use the mode comparison and storage details where they support the argument. Captions and editable SVG files are supplied for reuse.',
      'Completed experiments and performance gains are not invented here. The two previously reported image times used unequal payloads and are unsuitable for attributing a speedup to the protocol. No firmware changes, board uploads or radio tests were performed for this diagram task.'
    ]
    for p in intro:story.append(Paragraph(p,STYLES['body']))
    story.append(Spacer(1,12))
    story.append(table([['Reading path','What it explains'],['Sections 1-2','Deployment, modules and runtime scheduling'],['Sections 3-4','Exact compression decisions, pixel math and phone memory'],['Section 5','Local save versus destination delivery'],['Sections 6-9','Baseline, tuned, fixed and adaptive transfer'],['Sections 10-11','Persistent ACKs, timing boundaries and research interpretation']],[110,400]))
    md=['# Software architecture and image compression\n','Prepared 18 October 2026. Nine diagrams traced to the current implementation. This is supporting material for the methodology, not a claim of completed controlled experiments.\n']
    for sec in SECTIONS:
        story.append(PageBreak());story.append(Paragraph(sec['title'],STYLES['heading']))
        md.append('## '+sec['title']+'\n')
        if sec.get('fig') is not None:
            original=drawings[sec['fig']];d=original.copy();scale=min(510/d.width,410/d.height)
            d.scale(scale,scale);d.width*=scale;d.height*=scale
            story.append(d);story.append(Spacer(1,7));story.append(Paragraph(escape(sec['caption']),STYLES['caption']))
            md.append(f"![{sec['caption']}]({(FIG/(FIGURES[sec['fig']][0]+'.png')).as_posix()})\n")
            md.append('*'+sec['caption']+'*\n')
        for p in sec['paragraphs']:
            story.append(Paragraph(p,STYLES['body']));md.append(clean_markup(p)+'\n')
        if sec.get('table'):
            story.append(Spacer(1,4));story.append(table(sec['table'],[175,155,180]));story.append(Spacer(1,9))
            rows=sec['table'];md+=['| '+' | '.join(rows[0])+' |','| --- | --- | --- |']+['| '+' | '.join(r)+' |' for r in rows[1:]]+['']
        story.append(Paragraph('Source trace: '+escape(sec['source']),STYLES['small']))
        md.append('Source trace: '+source_links(sec['source'])+'\n')
    story.append(PageBreak());story.append(Paragraph('12. References and reproducibility',STYLES['heading']))
    md.append('## 12. References and reproducibility\n')
    for citation,url in REFERENCES:
        story.append(Paragraph(escape(citation)+f' <link href="{url}" color="#176b94">DOI / IEEE publication</link>',STYLES['body']))
        md.append(citation+f' [DOI / IEEE publication]({url}).\n')
    note='All academic references are IEEE publications. References [1]-[3] were verified through publisher/institution abstracts and DOI metadata; the full author-hosted paper was reviewed for [4]. Only the verified broad scope of the recent papers is used. Detailed architecture descriptions come from this project\'s source, not from those papers.'
    story.append(Paragraph(note,STYLES['body']));md.append(note+'\n')
    trace='The source manifest records SHA-256 hashes for the non-secret files inspected. It deliberately excludes certificate/private-key headers and NetworkSecrets.h. Source references identify the implementation inspected, not an assurance that a particular connected board is running that revision.'
    story.append(Paragraph(trace,STYLES['body']));md.append(trace+'\n')
    story.append(Paragraph('Reuse in the paper',STYLES['heading']))
    use='Number the selected figures consistently with the institutional template and introduce each in the text. Keep the definitions of saved locally, received by destination and human rescue response distinct. Include the rubric-required acknowledgement of AI assistance in diagram preparation, writing and code work, using the institution\'s required wording.'
    story.append(Paragraph(use,STYLES['body']));md.append(use+'\n')
    SimpleDocTemplate(str(PDF),pagesize=A4,rightMargin=42,leftMargin=42,topMargin=43,bottomMargin=53,title='LoRa Software Architecture and Image Compression',author='Project architecture documentation',pageCompression=1).build(story,onFirstPage=footer,onLaterPages=footer)
    (OUT/'Architecture-and-Compression-Guide.md').write_text('\n'.join(md),encoding='utf-8')
    files=['testing.ino','Config.h','TransferConfig.h','TransferPolicy.h','PacketDefs.h','StorageManager.h','LoRaMeshManager.h','WebServerManager.h','WebAssets.h','data/app.js','data/index.html','data/style.css','tools/embed_web_assets.py']
    manifest={'inspection_date':'2026-10-18','files':[{'path':f,'sha256':hashlib.sha256((ROOT/f).read_bytes()).hexdigest()} for f in files],'figures':[name for name,_ in FIGURES]}
    (OUT/'source-manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    (OUT/'figure-captions.txt').write_text('\n\n'.join(sec['caption'] for sec in SECTIONS if sec.get('fig') is not None),encoding='utf-8')
    with zipfile.ZipFile(OUT/'LoRa-architecture-figures.zip','w',zipfile.ZIP_DEFLATED) as z:
        for p in sorted(FIG.iterdir()):z.write(p,'figures/'+p.name)
        for f in ['figure-captions.txt','source-manifest.json']:z.write(OUT/f,f)
    print(json.dumps({'pdf':str(PDF),'figures':len(FIGURES),'markdown':str(OUT/'Architecture-and-Compression-Guide.md'),'zip':str(OUT/'LoRa-architecture-figures.zip')},indent=2))

if __name__=='__main__':build()
