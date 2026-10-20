// Public synthetic Firefox stock-browser WebDriver test. No external URLs or real profiles.
'use strict';
const fs=require('node:fs'),path=require('node:path'),net=require('node:net');
const {spawn,spawnSync}=require('node:child_process');
const delay=ms=>new Promise(r=>setTimeout(r,ms));
function exists(p){return p&&fs.existsSync(p)?p:null;}
const pf=process.env.ProgramFiles||'C:\\Program Files';
const pfx=process.env['ProgramFiles(x86)']||'C:\\Program Files (x86)';
function findBinary(){
 const from=process.env.GECKOWEBDRIVER||'';
 const dirs=[from,path.join(from,'geckodriver.exe'),'C:\\SeleniumWebDrivers\\GeckoDriver\\geckodriver.exe'];
 return dirs.map(p=>p.toLowerCase().endsWith('.exe')?p:path.join(p,'geckodriver.exe')).find(exists)||null;
}
async function reservePort(){
 const server=net.createServer();
 await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(0,'127.0.0.1',resolve);});
 const port=server.address().port;
 await new Promise(resolve=>server.close(resolve));
 return port;
}
async function request(base,method,uri,data){
 const timeoutMs=uri==='/session'&&method==='POST'?45000:12000;
 const ctrl=new AbortController(),timer=setTimeout(()=>ctrl.abort(),timeoutMs);
 try{
  const r=await fetch(base+uri,{method,headers:{'Content-Type':'application/json'},body:data===undefined?undefined:JSON.stringify(data),signal:ctrl.signal});
  const body=await r.json();
  if(!r.ok||body.value?.error)throw Object.assign(new Error('webdriver failure'),{stage:'WEBDRIVER_RESPONSE'});
  return body.value;
 }finally{clearTimeout(timer);}
}
async function qualify(){
 const result={name:'firefox-stock-webdriver',installed:false,status:'NOT_RUN',transport:'W3C_WEBDRIVER',fixture:'offline-data-uri'};
 const binary=exists(path.join(pf,'Mozilla Firefox','firefox.exe'))||exists(path.join(pfx,'Mozilla Firefox','firefox.exe'));
 const driver=findBinary();
 result.installed=!!binary;
 if(!binary){result.status='NOT_INSTALLED';return result;}
 if(!driver){result.status='DRIVER_NOT_PRESENT';return result;}
 let proc=null,session=null,base='',stage='DRIVER_START';
 try{
  const port=await reservePort();
  base='http://127.0.0.1:'+port;
  proc=spawn(driver,['--host','127.0.0.1','--port',String(port)],{stdio:'ignore',windowsHide:true});
  let ready=false;
  for(let i=0;i<50;i++){
   if(proc.exitCode!==null)break;
   try{const ctrl=new AbortController(),timer=setTimeout(()=>ctrl.abort(),800);
    const r=await fetch(base+'/status',{signal:ctrl.signal});
    clearTimeout(timer);if(r.ok){ready=true;break;}}
   catch{}
   await delay(100);
  }
  if(!ready)throw Error('driver unavailable');
  stage='SESSION_NEW';
  const startTime=Date.now();
  const value=await request(base,'POST','/session',{capabilities:{alwaysMatch:{browserName:'firefox','moz:firefoxOptions':{binary,args:['-headless']}}}});
  result.sessionStartMs=Date.now()-startTime;
  session=value.sessionId;
  if(!session)throw Error('session missing');
  result.version=value.capabilities?.browserVersion||null;
  const prefix='/session/'+encodeURIComponent(session);
  const markup='<!doctype html><title>WebDriver fixture</title><input id="f"><button id="b" onclick="document.querySelector(\'#o\').textContent=document.querySelector(\'#f\').value">Go</button><output id="o"></output>';
  stage='NAVIGATE';
  await request(base,'POST',prefix+'/url',{url:'data:text/html;charset=utf-8,'+encodeURIComponent(markup)});
  stage='DOM_ACTIONS';
  const key='element-6066-11e4-a52e-4f735466cecf';
  const field=await request(base,'POST',prefix+'/element',{using:'css selector',value:'#f'});
  const text='hello-ไทย-日本語';
  await request(base,'POST',prefix+'/element/'+encodeURIComponent(field[key])+'/value',{text,value:[...text]});
  const button=await request(base,'POST',prefix+'/element',{using:'css selector',value:'#b'});
  await request(base,'POST',prefix+'/element/'+encodeURIComponent(button[key])+'/click',{});
  const output=await request(base,'POST',prefix+'/element',{using:'css selector',value:'#o'});
  const observed=await request(base,'GET',prefix+'/element/'+encodeURIComponent(output[key])+'/text');
  result.status=observed===text?'PASS':'POSTCONDITION_FAILED';
 }catch(e){result.status='FAILED';result.failureStage=stage;result.failureClass=e?.name||'Error';result.driverExited=proc?.exitCode!==null;}
 finally{
  if(session){try{await request(base,'DELETE','/session/'+encodeURIComponent(session));result.sessionDeleted=true;}catch{result.sessionDeleted=false;}}
  if(proc?.pid){const killed=spawnSync('taskkill',['/PID',String(proc.pid),'/T','/F'],{timeout:4000,windowsHide:true,stdio:'ignore'});result.driverStopped=killed.status===0||proc.exitCode!==null;}
 }
 return result;
}
module.exports={qualify};
