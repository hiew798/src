from pathlib import Path
import json, statistics
from reportlab.pdfgen import canvas
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.lib.colors import black, white, Color
import pdfplumber

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT/'research/word-figures'
OUT.mkdir(parents=True, exist_ok=True)
for name, file in [('T','times.ttf'),('TB','timesbd.ttf'),('TI','timesi.ttf')]:
    pdfmetrics.registerFont(TTFont(name, str(Path('C:/Windows/Fonts')/file)))

def start(name,h):
    return canvas.Canvas(str(OUT/(name+'.pdf')),pagesize=(252,h))
def end(c,name):
    c.save()
    with pdfplumber.open(OUT/(name+'.pdf')) as pdf:
        pdf.pages[0].to_image(resolution=300).save(OUT/(name+'.png'))
def lines(c,x,y,items,font='T',size=8.5,leading=10):
    c.setFillColor(black);c.setFont(font,size)
    for i,line in enumerate(items): c.drawCentredString(x,y-i*leading,line)
def box(c,x,y,w,h,title,detail=()):
    if isinstance(detail,str): detail=(detail,)
    c.setLineWidth(.65);c.setStrokeColor(black);c.setFillColor(Color(.965,.965,.965))
    c.rect(x,y,w,h,stroke=1,fill=1)
    n=1+len(detail); top=y+h/2+(n-1)*5-3
    lines(c,x+w/2,top,[title],'TB',9)
    lines(c,x+w/2,top-11,detail)
def arrow(c,x1,y1,x2,y2):
    c.setStrokeColor(black);c.setFillColor(black);c.setLineWidth(.65);c.line(x1,y1,x2,y2)
    import math
    a=math.atan2(y2-y1,x2-x1);s=4
    p=c.beginPath();p.moveTo(x2,y2);p.lineTo(x2-s*math.cos(a-.48),y2-s*math.sin(a-.48));p.lineTo(x2-s*math.cos(a+.48),y2-s*math.sin(a+.48));p.close();c.drawPath(p,stroke=0,fill=1)

c=start('system',255)
box(c,30,210,192,40,'Civilian phone',('Browser: SOS, photo, location'))
box(c,30,140,192,40,'Civilian node',('Local HTTPS and saved source data'))
box(c,30,70,192,40,'Rescuer node',('Saved fragments and CRC32 verification'))
box(c,30,0,192,40,'Rescuer phone',('Browser: reports, images and replies'))
for top,bot,label in [(210,180,'Local Wi-Fi / HTTPS'),(140,110,'LoRa data / acknowledgements'),(70,40,'Local Wi-Fi / HTTPS')]:
    arrow(c,110,top,110,bot);arrow(c,122,bot,122,top)
    lines(c,184,(top+bot)/2-2,[label],size=7.2)
end(c,'system')

c=start('software',240)
box(c,10,201,232,34,'Phone browser',('Input, image processing, location and polling'))
box(c,10,144,232,34,'WebServerManager',('Local requests, validation and responses'))
arrow(c,126,201,126,178);lines(c,183,187,['HTTPS'],size=8)
box(c,1,83,118,38,'StorageManager',('LittleFS records and images',))
box(c,133,83,118,38,'LoRaMeshManager',('Queues, ACKs and forwarding',))
arrow(c,62,144,62,121);arrow(c,191,144,191,121)
arrow(c,119,107,133,107);arrow(c,133,96,119,96)
box(c,1,27,118,36,'TransferPolicy',('Fixed or adaptive groups',))
box(c,133,27,118,36,'RadioLib and SX1262',('Transmit and receive events',))
arrow(c,112,63,155,83);arrow(c,191,83,191,63);arrow(c,168,83,119,63)
lines(c,126,9,['Node services share a cooperative application loop.'],size=8)
end(c,'software')

c=start('compression',318)
steps=[(277,36,'Camera or existing file',('Reject encoded input above 15 MiB',)),
       (226,36,'Decode and resize',('Try 160 x 120, 120 x 90, then 80 x 60',)),
       (175,36,'Grayscale canvas',('Y = round(0.299R + 0.587G + 0.114B)',)),
       (124,36,'Request browser JPEG encoding',('Quality: 0.60, 0.45, 0.30, then 0.20',)),
       (73,36,'Accept first output at most 8192 bytes',('Otherwise try next quality, then next size',))]
for y,h,t,d in steps: box(c,8,y,236,h,t,d)
for y in [277,226,175,124]: arrow(c,126,y,126,y-15)
lines(c,126,58,['After twelve unsuccessful candidates: reject the image.'],size=8)
c.setDash(2,2);c.line(8,48,244,48);c.setDash()
lines(c,126,34,['Conceptual JPEG codec inside the browser:'],'TB',8.5)
lines(c,126,22,['8 x 8 blocks > DCT > quantisation > coefficient coding'],size=8)
lines(c,126,10,['Browser selects the exact codec parameters.'],'TI',8)
end(c,'compression')

c=start('modes',246)
panels=[(0,128,'Mode 1: Baseline',['Send one chunk','Wait for chunk ACK','300 ms post-ACK delay']),
        (132,128,'Mode 2: Tuned',['Send one chunk','Wait after transmission','30 ms post-ACK delay']),
        (0,12,'Mode 3: Fixed',['Send up to four chunks','Query saved bitmap','Retry missing fragments']),
        (132,12,'Mode 4: Adaptive',['Choose bounded group','Query; recover feedback','Measure rate and probe'])]
for x,y,title,steps in panels:
    lines(c,x+60,y+107,[title],'TB',9)
    for i,text in enumerate(steps):
        yy=y+72-i*31;box(c,x+2,yy,116,24,text)
        if i<2:arrow(c,x+60,yy,x+60,yy-7)
lines(c,126,1,['All modes retain stored progress and final integrity checks.'],size=7.8)
end(c,'modes')

c=start('adaptive',251)
box(c,8,214,236,32,'Start each image at two chunks',('Apply airtime-derived group cap',))
box(c,8,162,236,32,'Transmit group and query receiver bitmap')
arrow(c,126,214,126,194)
box(c,8,92,112,43,'No matching reply',('Repeat the query first;', 'shrink on repeated silence'))
box(c,132,92,120,43,'Matching reply',('Count newly confirmed bytes;', 'measure elapsed group time'))
arrow(c,66,162,60,135);arrow(c,187,162,192,135)
box(c,8,32,236,40,'Probe and retain the better measured group',('Require greater than 5% rate improvement;', 'exclude short or urgent-interrupted groups'))
arrow(c,192,92,170,72)
c.line(60,92,60,80);c.line(60,80,3,80);c.line(3,80,3,150);c.line(3,150,24,150);arrow(c,24,150,24,162)
lines(c,126,13,['Continue until destination verifies the complete image.'],size=8)
end(c,'adaptive')

c=start('results',210)
datasets=[('Close placement',[(1,[94,90,95,98,113]),(2,[85,89,84,83,88]),(3,[75,80,77,82,73]),(4,[58,71,75,63,61])]),('One brick wall',[(3,[101,116,98,106]),(4,[76,101,85,90])])]
for j,(title,rows) in enumerate(datasets):
    left=28+j*125;right=left+94;bottom=41;top=180
    lines(c,(left+right)/2,198,[title],'TB',9)
    for tick in range(50,121,10):
        yy=bottom+(tick-50)/70*(top-bottom)
        c.setLineWidth(.35);c.setStrokeColor(Color(.8,.8,.8));c.line(left,yy,right,yy)
        if j==0: c.setFillColor(black);c.setFont('T',8);c.drawRightString(left-4,yy-2,str(tick))
    for k,(mode,values) in enumerate(rows):
        x=left+(k+.5)*(right-left)/len(rows)
        def ty(t):return bottom+(t-50)/70*(top-bottom)
        mean=statistics.mean(values);sd=statistics.stdev(values)
        c.setStrokeColor(black);c.setLineWidth(.8);c.line(x,ty(mean-sd),x,ty(mean+sd))
        for v in [mean-sd,mean+sd]: c.line(x-4,ty(v),x+4,ty(v))
        for i,v in enumerate(values):
            jitter=(i-(len(values)-1)/2)*2.1
            c.setFillColor(white);c.circle(x+jitter,ty(v),1.9,stroke=1,fill=1)
        c.setFillColor(black);c.rect(x-2.2,ty(mean)-2.2,4.4,4.4,stroke=0,fill=1)
        lines(c,x,27,[f'M{mode}'],size=8)
    c.setStrokeColor(black);c.line(left,bottom,right,bottom)
c.saveState();c.translate(7,112);c.rotate(90);lines(c,0,0,['Time (s)'],size=8);c.restoreState()
lines(c,126,10,['Open circles: runs; squares: means; bars: sample SD.'],size=8)
end(c,'results')
print('Created six publication-size figures.')
