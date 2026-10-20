// Browser-only baseline: synthetic filechooser upload/download and independent byte-hash oracles.
// Deliberately no native OS file dialog, WinInspect, real profile or external URL.
'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const {chromium}=require(path.join(process.env.PLAYWRIGHT_MODULE_ROOT,'node_modules','playwright'));
const sha=s=>crypto.createHash('sha256').update(s).digest('hex');
const payload=Buffer.from('synthetic-public-rdte-ไทย-日本語','utf8');
const fixture='<!doctype html><title>RDTE file transfer</title>'+
'<input id="upload" type="file"><output id="out"></output><a href="#" id="download">Download</a>'+
'<script>const input=document.querySelector("#upload");'+
'input.onchange=async()=>{document.querySelector("#out").textContent="RECEIVED:"+await input.files[0].text()};'+
'document.querySelector("#download").onclick=e=>{e.preventDefault();'+
'const b=new Blob([new TextEncoder().encode("synthetic-public-rdte-ไทย-日本語")],{type:"text/plain"});'+
'const a=document.createElement("a");a.href=URL.createObjectURL(b);a.download="synthetic-download.txt";a.click()};</script>';
const receipt={schema:'browser-native-file-transfer-baseline/v1',offlineFixture:true,
 nativeOsDialogLaunched:false,wininspectUsed:false,profileReused:false,iterations:[]};
let browser;
(async()=>{
 try{
  browser=await chromium.launch({channel:'chrome',headless:true,timeout:18000});
  receipt.browserVersion=browser.version();
  for(let i=0;i<3;i++){
   let context;const row={index:i+1,upload:'NOT_RUN',download:'NOT_RUN'};
   receipt.iterations.push(row);
   try{
    context=await browser.newContext({acceptDownloads:true});
    const page=await context.newPage();
    await page.goto('data:text/html;charset=utf-8,'+encodeURIComponent(fixture));
    let begin=Date.now();
    const choosing=page.waitForEvent('filechooser',{timeout:10000});
    await page.locator('#upload').click();
    const chooser=await choosing;
    await chooser.setFiles({name:'synthetic-upload.txt',mimeType:'text/plain',buffer:payload});
    const verified=await page.locator('#out').innerText({timeout:10000});
    row.upload=verified==='RECEIVED:'+payload.toString('utf8')?'PASS':'POSTCONDITION_MISMATCH';
    row.uploadMs=Date.now()-begin;
    begin=Date.now();
    const downloading=page.waitForEvent('download',{timeout:10000});
    await page.locator('#download').click();
    const file=await downloading,bytes=fs.readFileSync(await file.path());
    row.download=file.suggestedFilename()==='synthetic-download.txt'&&sha(bytes)===sha(payload)
      ?'PASS':'POSTCONDITION_MISMATCH';
    row.downloadMs=Date.now()-begin;
   }catch(e){row.failureClass=e?.name||'Error';}
   finally{if(context)await context.close();}
  }
 }catch(e){receipt.launchFailure=e?.name||'Error';}
 finally{
  if(browser)await browser.close().catch(()=>{receipt.closeFailure=true;});
  receipt.status=receipt.iterations.length===3&&receipt.iterations.every(
   r=>r.upload==='PASS'&&r.download==='PASS')&&!receipt.closeFailure?'PASS':'FAIL';
  fs.writeFileSync(process.env.TRANSFER_RECEIPT,JSON.stringify(receipt,null,2));
  console.log('BROWSER_TRANSFER='+receipt.status+' TRIALS='+receipt.iterations.length);
  if(receipt.status!=='PASS')process.exitCode=1;
 }
})().catch(()=>{process.exitCode=1;});
