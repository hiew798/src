const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const { chromium } = require('playwright');
const root = path.resolve(__dirname, '..');
const portalURL = 'http://192.168.4.1/';
let role = 'civilian', records = [], calls = [], loseSOSResponse = true, imageBytes = null;
let failClear = false, holdState = false, heldState = null, holdPhoto = false, heldPhoto = null;
const server = http.createServer(async (req,res) => {
    const url = new URL(req.url, 'http://localhost');
    if (!url.pathname.startsWith('/api/')) {
        const file = {'/':'index.html','/app.js':'app.js','/style.css':'style.css'}[url.pathname];
        if (!file) {res.writeHead(404).end();return;}
        res.setHeader('Content-Type',file.endsWith('.js')?'application/javascript':file.endsWith('.css')?'text/css':'text/html');
        res.end(fs.readFileSync(path.join(root,'data',file)));return;
    }
    res.setHeader('Content-Type','application/json');
    if(url.pathname==='/api/status'){res.end(JSON.stringify({role,node:'0000000000123456',radio:true,storage:true,canClear:true,records:records.length,limit:96,tx:1,rx:1,transferMode:3,transferName:'fixed',protocol:4}));return;}
    assert.match(req.headers['x-session'],/^[a-f0-9]{32}$/);
    if(role==='rescuer' && req.headers.authorization!=='Basic '+Buffer.from('rescuer:test-password').toString('base64')){res.writeHead(401).end(JSON.stringify({error:'Login required'}));return;}
    if(url.pathname==='/api/state'){const body=JSON.stringify({records});if(holdState){heldState=()=>res.end(body);return;}res.end(body);return;}
    let chunks=[];for await(const data of req)chunks.push(data);const raw=Buffer.concat(chunks);
    if(url.pathname==='/api/image'){
        if(req.method==='GET'){res.setHeader('Content-Type','image/jpeg');if(holdPhoto){const bytes=imageBytes;heldPhoto=()=>res.end(bytes);return;}res.end(imageBytes);return;}
        assert.equal(req.headers['content-type'],'image/jpeg');assert.ok(raw.length<=8192);assert.equal(raw.readUInt16BE(0),0xffd8);assert.equal(raw.readUInt16BE(raw.length-2),0xffd9);
        imageBytes=raw;const id=url.searchParams.get('id');records.push({id,kind:3,link:id,mine:true,complete:true,done:0,chunks:Math.ceil(raw.length/180),delivery:'pending',order:records.length+1});res.end(JSON.stringify({id}));return;
    }
    const body=JSON.parse(raw.toString()||'{}');calls.push({path:url.pathname,body});
    if(url.pathname==='/api/clear'){
        assert.equal(req.method,'POST');assert.deepEqual(body,{confirm:'CLEAR_ALL',node:'0000000000123456'});
        if(failClear){res.writeHead(503).end(JSON.stringify({error:'Clear did not finish. Retry Clear all.'}));return;}
        records=[];imageBytes=null;
        await new Promise(r=>setTimeout(r,100));res.end('{"ok":true,"radio":true}');return;
    }
    if(url.pathname==='/api/sos'){
        if(!records.some(r=>r.id===body.id && r.kind===1))records.push({lat:null,lon:null,...body,kind:1,link:'0000000000000000',source:'0000000000123456',mine:true,delivery:'pending',order:records.length+1});
        if(loseSOSResponse){loseSOSResponse=false;res.writeHead(504).end(JSON.stringify({error:'Response lost after saving'}));return;}
        res.end(JSON.stringify({id:body.id}));return;
    }
    if(url.pathname==='/api/chat'){records.push({...body,kind:2,link:body.report,mine:true,delivery:'pending',order:records.length+1});res.end(JSON.stringify({id:body.id}));return;}
    if(url.pathname==='/api/resolve'){records.find(r=>r.id===body.report).resolved=true;records.push({...body,kind:5,link:body.report,mine:true,delivery:'pending',order:records.length+1});res.end(JSON.stringify({id:body.id}));return;}
    res.end('{"ok":true}');
});
(async()=>{
    await new Promise(r=>server.listen(0,'127.0.0.1',r));
    const browser=await chromium.launch({headless:true,executablePath:process.env.HELTEC_CHROMIUM});
    let closing=false;
    try {
        const errors=[];
        async function portalPage(blockStorage=false) {
            const page=await browser.newPage({viewport:{width:390,height:844}});
            page.on('pageerror',e=>errors.push(e.message));
            await page.addInitScript(({blockStorage})=>{
                window.gpsCalls=0;
                if(navigator.geolocation)navigator.geolocation.getCurrentPosition=()=>{window.gpsCalls++;};
                if(blockStorage)Object.defineProperty(window,'localStorage',{get(){throw new DOMException('Storage disabled','SecurityError');}});
            },{blockStorage});
            await page.route('http://192.168.4.1/**',async route=>{
                const url=new URL(route.request().url());
                try {
                    const request=route.request(),headers={...request.headers()};delete headers.host;
                    const response=await fetch(`http://127.0.0.1:${server.address().port}${url.pathname}${url.search}`,{
                        method:request.method(),headers,
                        body:['GET','HEAD'].includes(request.method())?undefined:request.postDataBuffer()
                    });
                    const body=Buffer.from(await response.arrayBuffer()),replyHeaders=Object.fromEntries(response.headers);
                    delete replyHeaders['transfer-encoding'];delete replyHeaders['content-length'];
                    if(!page.isClosed() && !closing)await route.fulfill({status:response.status,headers:replyHeaders,body});
                } catch(error) {
                    if(!page.isClosed() && !closing && !page.testClosing){errors.push(error.message);await route.abort().catch(()=>{});}
                }
            });
            return page;
        }
        async function clearChecks(page, delayed=false) {
            const savedSession=await page.evaluate(()=>browserStorage.getItem('heltec-session'));
            const count=records.length, before=calls.filter(c=>c.path==='/api/clear').length;
            assert.ok(count>0);
            assert.ok((await page.locator('#node').textContent()).includes('Mode 3 (fixed) · Protocol 4'));
            assert.equal(await page.locator('#connectionInfo').textContent(),'Protocol 4 · Open Wi-Fi · Local HTTP · No internet required');
            page.once('dialog',dialog=>{assert.ok(dialog.message().includes('other users'));dialog.dismiss();});
            await page.locator('#clearAll').click();assert.equal(records.length,count);
            assert.equal(calls.filter(c=>c.path==='/api/clear').length,before);
            failClear=true;page.once('dialog',dialog=>dialog.accept());await page.locator('#clearAll').click();
            await page.waitForFunction(()=>document.getElementById('feedback').textContent.includes('Clear all was not confirmed'));
            await page.waitForFunction(()=>!document.getElementById('clearAll').disabled);
            assert.equal(records.length,count);failClear=false;
            await page.evaluate(()=>{
                browserStorage.setItem(`heltec-sos-draft-${status.node}`,'{"id":"old"}');
                browserStorage.setItem(`heltec-chat-draft-${selected}`,'{"id":"old"}');
                browserStorage.setItem(`heltec-resolve-${selected}`,'{"id":"old"}');
                browserStorage.setItem('heltec-sos-draft-another-node','keep');
            });
            const photoId=records.find(r=>r.kind===3)?.id;
            if(delayed){
                await page.waitForFunction(()=>!polling && photosLoading.size===0);
                holdState=true;await page.evaluate(()=>{void refresh();});
                while(!heldState)await new Promise(r=>setTimeout(r,10));
                if(photoId){holdPhoto=true;await page.evaluate(id=>{void loadPhoto(id);},photoId);while(!heldPhoto)await new Promise(r=>setTimeout(r,10));}
            }
            page.once('dialog',dialog=>dialog.accept());await page.locator('#clearAll').click();
            await page.waitForFunction(()=>document.getElementById('feedback').textContent.includes('All reports, messages and photos cleared'));
            assert.equal(records.length,0);
            if(delayed){
                holdState=false;heldState();heldState=null;
                if(heldPhoto){holdPhoto=false;heldPhoto();heldPhoto=null;}
                await page.waitForFunction(()=>!polling && photosLoading.size===0);
            }
            assert.equal(await page.locator('#reports button').count(),0);
            assert.equal(await page.locator('#conversation').isVisible(),false);
            assert.equal(await page.evaluate(()=>photoURLs.size),0);
            assert.equal(await page.evaluate(()=>browserStorage.getItem('heltec-session')),savedSession);
            assert.equal(await page.evaluate(()=>browserStorage.getItem('heltec-selected')),null);
            assert.equal(await page.evaluate(()=>browserStorage.getItem(`heltec-sos-draft-${status.node}`)),null);
            assert.equal(await page.evaluate(()=>browserStorage.getItem('heltec-sos-draft-another-node')),'keep');
            await page.evaluate(()=>refresh());assert.equal(await page.locator('#reports button').count(),0);
            assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
        }
        const page=await portalPage();
        await page.goto(portalURL);
        await page.waitForFunction(()=>document.getElementById('health').textContent.includes('Radio ready'));
        assert.equal(await page.evaluate(()=>window.isSecureContext),false);
        await page.locator('#latitude').fill('0');
        await page.locator('#sendSOS').click();
        await page.waitForFunction(()=>document.getElementById('feedback').textContent.includes('Enter both latitude'));
        assert.equal(calls.filter(c=>c.path==='/api/sos').length,0);
        await page.locator('#longitude').fill('101.6869');
        await page.locator('#gpsButton').click();
        assert.equal(await page.evaluate(()=>window.gpsCalls),0);
        assert.equal(await page.locator('#latitude').inputValue(),'0');
        assert.equal(await page.locator('#longitude').inputValue(),'101.6869');
        assert.ok((await page.locator('#gpsStatus').textContent()).includes('HTTPS'));
        const xss='<img src=x onerror=alert(1)>';
        await page.locator('#name').fill(xss);await page.locator('#location').fill('Block A / floor 2');await page.locator('#sosText').fill('Need help');
        await page.locator('#sendSOS').click();await page.waitForFunction(()=>document.getElementById('feedback').textContent.includes('connection dropped'));
        await page.locator('#sendSOS').click();await page.waitForFunction(()=>document.getElementById('feedback').textContent.includes('SOS saved'));
        assert.equal(records.filter(r=>r.kind===1).length,1);const sosCalls=calls.filter(c=>c.path==='/api/sos');assert.equal(sosCalls[0].body.id,sosCalls[1].body.id);
        assert.equal(sosCalls[0].body.lat,0);assert.equal(sosCalls[0].body.lon,101.6869);
        await page.waitForFunction(()=>document.getElementById('detail').textContent.includes('Latitude: 0.000000'));
        assert.ok((await page.locator('#detail').textContent()).includes('Latitude: 0.000000'));
        assert.ok((await page.locator('#detail').textContent()).includes('Longitude: 101.686900'));
        assert.equal(await page.locator('#reports img').count(),0);assert.ok((await page.locator('#reports').textContent()).includes(xss));
        await page.locator('#chatText').fill('We are in the stairwell');await page.locator('#sendChat').click();await page.waitForFunction(()=>document.getElementById('messages').textContent.includes('We are in the stairwell'));
        const png=await page.evaluate(()=>{const c=document.createElement('canvas');c.width=400;c.height=300;const ctx=c.getContext('2d');ctx.fillStyle='red';ctx.fillRect(0,0,400,300);return c.toDataURL('image/png').split(',')[1];});
        await page.locator('#attachPhoto').setInputFiles({name:'test.png',mimeType:'image/png',buffer:Buffer.from(png,'base64')});await page.locator('#attachButton').click();
        await page.waitForFunction(()=>document.getElementById('photoView').textContent.includes('chunks acknowledged'));
        assert.ok(imageBytes && imageBytes.length<=8192);
        const report=records.find(r=>r.kind===1);report.delivery='received';records.push({id:'0000000000000099',kind:2,link:report.id,text:'Rescuers are on the way',mine:false,delivery:'received',order:90});
        await page.locator('#retry').click();await page.waitForFunction(()=>document.getElementById('messages').textContent.includes('Rescuers are on the way'));
        assert.ok((await page.locator('#reports').textContent()).includes('Received by device'));
        assert.equal(await page.locator('#resolve').isVisible(),false);
        const screenshot=process.env.HELTEC_SCREENSHOT;if(screenshot)await page.screenshot({path:screenshot,fullPage:true});
        await page.locator('#name').fill('No coordinates');await page.locator('#sendSOS').click();
        await page.waitForFunction(()=>document.getElementById('detail').textContent.includes('Latitude: Not supplied'));
        assert.ok((await page.locator('#detail').textContent()).includes('Longitude: Not supplied'));
        assert.equal(records.find(r=>r.name==='No coordinates').lat,null);
        const captive=await portalPage(true);await captive.goto(portalURL);
        await captive.waitForFunction(()=>document.getElementById('health').textContent.includes('Radio ready'));
        assert.equal(await captive.locator('#sessionNotice').isVisible(),true);
        await captive.locator('#name').fill('Captive session');await captive.locator('#sendSOS').click();
        await captive.waitForFunction(()=>document.getElementById('feedback').textContent.includes('SOS saved'));
        captive.testClosing=true;await captive.close();
        const otherNodeRecords=structuredClone(records), otherNodeImage=imageBytes;
        await clearChecks(page,true);
        await page.locator('#name').fill('After clear');await page.locator('#sendSOS').click();
        await page.waitForFunction(()=>document.getElementById('feedback').textContent.includes('SOS saved'));
        assert.equal(records.length,1); // Sending works again after clearing.
        page.testClosing=true;await page.close();records=otherNodeRecords;imageBytes=otherNodeImage;
        role='rescuer';records.forEach(r=>{r.mine=false;});
        const rescue=await portalPage();await rescue.goto(portalURL);
        await rescue.waitForFunction(()=>document.getElementById('login').hidden===false);
        assert.equal(await rescue.locator('#clearAll').isDisabled(),true);
        await rescue.locator('#loginPassword').fill('test-password');await rescue.locator('#loginButton').click();await rescue.waitForFunction(()=>document.getElementById('feedback').textContent.includes('login successful'));
        await rescue.locator('#reports button').filter({hasText:xss}).click();
        assert.ok((await rescue.locator('#detail').textContent()).includes('Latitude: 0.000000'));
        assert.ok((await rescue.locator('#detail').textContent()).includes('Longitude: 101.686900'));
        assert.equal(await rescue.locator('#civilian').isVisible(),false);assert.equal(await rescue.locator('#beacon').isVisible(),true);
        await rescue.locator('#chatText').fill('Please stay where you are');await rescue.locator('#sendChat').click();await rescue.waitForFunction(()=>document.getElementById('messages').textContent.includes('Please stay where you are'));
        rescue.once('dialog',dialog=>dialog.accept());await rescue.locator('#resolve').click();await rescue.waitForFunction(()=>document.getElementById('reports').textContent.includes('Resolved'));
        assert.equal(await rescue.locator('#chatForm').isVisible(),false);assert.deepEqual(errors,[]);
        await clearChecks(rescue);
        if(process.env.HELTEC_CLEAR_SCREENSHOT)await rescue.screenshot({path:process.env.HELTEC_CLEAR_SCREENSHOT,fullPage:true});
        assert.deepEqual(errors,[]);
        console.log('PASS: civilian/rescuer clear confirmation, failed clear, stale state/photo responses, preserved session, fresh SOS after clear, mobile UI, coordinates, photos and chat (mock API).');
    } finally {closing=true;await browser.close();server.close();}
})().catch(e=>{console.error(e);server.close();process.exitCode=1;});
