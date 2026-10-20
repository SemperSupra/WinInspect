// Disposable public Playwright browser qualification; no real websites or profiles.
'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const {chromium,firefox}=require(path.join(process.env.PLAYWRIGHT_MODULE_ROOT,'node_modules','playwright'));
const wininspect=require('./public-browser-wininspect-observe.cjs');
let wininspectReady=false;
const pf=process.env.ProgramFiles||'C:\\Program Files',pfx=process.env['ProgramFiles(x86)']||'C:\\Program Files (x86)';
const local=process.env.LOCALAPPDATA||'';
const available=(...p)=>p.find(x=>x&&fs.existsSync(x))||null;
const stockFirefox=available(path.join(pf,'Mozilla Firefox/firefox.exe'),path.join(pfx,'Mozilla Firefox/firefox.exe'));
const stockOpera=available(path.join(local,'Programs/Opera/launcher.exe'),path.join(pf,'Opera/launcher.exe'));
const browsers=[
 ['chrome',chromium,{channel:'chrome',headless:true},true],
 ['edge',chromium,{channel:'msedge',headless:false},true],
 ['edge-accessibility',chromium,{channel:'msedge',headless:false,args:['--force-renderer-accessibility']},true],
 ['firefox-playwright',firefox,{headless:true},true],
 ['firefox-stock',firefox,{headless:true,executablePath:stockFirefox},!!stockFirefox],
 ['opera-stock',chromium,{headless:true,executablePath:stockOpera},!!stockOpera]
];
const fixture='<!doctype html><title>RDTE</title><input id="f"><button id="b">Go</button><output id="o"></output><script>document.querySelector("#b").onclick=()=>document.querySelector("#o").textContent=document.querySelector("#f").value;</script>';
async function main(){
 const receipt={schema:'public-browser-rdte/v1',source:'approved-public-projection',offline:true,stockProfilesUsed:false,browsers:[]};
 receipt.wininspect=await wininspect.start();
 wininspectReady=receipt.wininspect.status==='PASS';
 for(const [name,engine,opts,installed] of browsers){
  const r={name,installed,dom:'NOT_RUN'};receipt.browsers.push(r);
  if(!installed){r.dom='NOT_INSTALLED';continue;}
  let browser,server;
  try{
   if(name.startsWith('edge')){
    server=await engine.launchServer({...opts,timeout:18000});
    browser=await engine.connect(server.wsEndpoint());
   }else browser=await engine.launch({...opts,timeout:18000});
   r.version=browser.version();
   const context=await browser.newContext(),page=await context.newPage();
   await page.goto('data:text/html;charset=utf-8,'+encodeURIComponent(fixture));
   await page.locator('#f').fill('hello-ไทย-日本語');
   await page.locator('#b').click();
   r.dom=(await page.locator('#o').innerText())==='hello-ไทย-日本語'?'PASS':'FAIL';
   r.browserPid=name.startsWith('edge')?server.process()?.pid||null:null;
   if(name.startsWith('edge')&&wininspectReady&&r.browserPid)r.native=await wininspect.observe(r.browserPid);
   await context.close();
  }catch(e){r.dom='UNSUPPORTED_OR_FAILED';r.failureClass=e.name||'Error';}
  finally{if(browser)await browser.close().catch(()=>{});if(server)await server.close().catch(()=>{});}
  console.log('RDTE_BROWSER='+name+' DOM='+r.dom);
 }
 // Browser PIDs are transient execution evidence; do not publish machine process IDs.
 for(const r of receipt.browsers)delete r.browserPid;
 receipt.verdict=receipt.browsers.some(r=>r.dom==='PASS')?'PLAYWRIGHT_DOM_PROVEN':'NO_BROWSER_PASS';
 const file=process.env.RDTE_RECEIPT;fs.writeFileSync(file,JSON.stringify(receipt,null,2));
 wininspect.stop();
 if(receipt.verdict==='NO_BROWSER_PASS')process.exitCode=1;
}
main().catch(()=>{wininspect.stop();process.exitCode=1;});
