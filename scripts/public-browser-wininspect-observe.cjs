// Public WinInspect read-only observation of a disposable Playwright browser process.
'use strict';
const net=require('node:net'),fs=require('node:fs'),os=require('node:os'),path=require('node:path');
const {spawn,spawnSync}=require('node:child_process'),{randomUUID}=require('node:crypto');
const work=fs.mkdtempSync(path.join(os.tmpdir(),'wininspect-rdte-'));
const pipe='wi-rdte-'+randomUUID().replaceAll('-','');
let daemon=null;
const nap=ms=>new Promise(r=>setTimeout(r,ms));
function rpc(method,params={}){
 return new Promise((resolve,reject)=>{
  const id=randomUUID(),p=Buffer.from(JSON.stringify({id,method,params}),'utf8');
  if(p.length>4096)return reject(Error('request bounds'));
  const h=Buffer.alloc(4);h.writeUInt32LE(p.length);
  let buf=Buffer.alloc(0),length=null,closed=false;
  const socket=net.createConnection('\\\\.\\pipe\\'+pipe);
  const timeout=setTimeout(()=>done(Error('timeout')),6000);
  function done(err,value){if(closed)return;closed=true;clearTimeout(timeout);socket.destroy();err?reject(err):resolve(value);}
  socket.on('connect',()=>socket.write(Buffer.concat([h,p])));
  socket.on('data',chunk=>{
   buf=Buffer.concat([buf,chunk]);
   if(length===null&&buf.length>=4){length=buf.readUInt32LE(0);if(length<2||length>262144)return done(Error('frame bounds'));}
   if(length!==null&&buf.length>=length+4){
    if(buf.length!==length+4)return done(Error('extra bytes'));
    try{const r=JSON.parse(buf.subarray(4).toString('utf8'));done(null,{ok:r.ok===true,idMatched:r.id===id,result:r.result});}
    catch{done(Error('invalid response'));}
   }
  });
  socket.on('error',e=>done(e));socket.on('end',()=>{if(!closed)done(Error('early EOF'));});
 });
}
async function start(){
 const cfg={config_version:1,bind:[{address:'127.0.0.1',family:'ipv4'}],port:0,http_port:0,https_port:0,tls_port:0,rendezvous:[],enable_update_check:false,enable_mdns:false,enable_discovery:false};
 const file=path.join(work,'config.json');fs.writeFileSync(file,JSON.stringify(cfg));
 daemon=spawn(process.env.WININSPECT_DAEMON,['--config',file,'--headless','--read-only','--no-clipboard','--no-discovery','--no-mdns','--bind','127.0.0.1','--ipv4','--port','0','--pipe-name',pipe],{cwd:work,stdio:'ignore',windowsHide:true,env:{...process.env,APPDATA:work,WININSPECT_BIND:'127.0.0.1'}});
 for(let i=0;i<20;i++){await nap(200);if(daemon.exitCode!==null)break;try{const x=await rpc('daemon.health');return {status:x.ok?'PASS':'DENIED',idMatched:x.idMatched};}catch{}}
 return {status:'UNAVAILABLE'};
}
async function observe(pid){
 const out={status:'NO_PID_MATCH',uia:'NOT_RUN',pidWindowCount:0,visiblePidWindowCount:0};
 try{
  const x=await rpc('window.listTop');out.idMatched=x.idMatched;
  if(!x.ok||!Array.isArray(x.result))throw Error('enumeration unavailable');
  const matches=[];
  for(const row of x.result.slice(0,120)){
   if(typeof row.hwnd!=='string'||!/^0x[\da-f]+$/i.test(row.hwnd))continue;
   const w=await rpc('window.getInfo',{hwnd:row.hwnd});
   if(!w.ok||Number(w.result?.pid)!==pid)continue;
   const box=w.result.screen_rect||{};
   const width=Math.max(0,Number(box.right)-Number(box.left));
   const height=Math.max(0,Number(box.bottom)-Number(box.top));
   matches.push({hwnd:row.hwnd,visible:w.result.visible===true,
     area:Number.isFinite(width*height)?width*height:0,
     browserClass:String(w.result.class_name||'').startsWith('Chrome_WidgetWin')});
  }
  out.pidWindowCount=matches.length;
  out.visiblePidWindowCount=matches.filter(v=>v.visible).length;
  matches.sort((a,b)=>Number(b.visible)-Number(a.visible)
    ||Number(b.browserClass)-Number(a.browserClass)||b.area-a.area);
  const chosen=matches.find(v=>v.visible&&v.area>20000);
  if(!chosen){out.status=matches.length?'NO_VISIBLE_MAIN_WINDOW':'NO_PID_MATCH';return out;}
  out.status='VISIBLE_WINDOW_MATCH';
  out.selectedAreaBand=chosen.area>=250000?'LARGE':'SMALL';
  out.selectedBrowserClass=chosen.browserClass;
  try{
   const u=await rpc('ui.inspect',{hwnd:chosen.hwnd});
   out.uia=u.ok&&Array.isArray(u.result)&&u.result.length>0?'UIA_NONEMPTY':(u.ok?'UIA_EMPTY':'UIA_UNAVAILABLE');
   out.uiaCount=u.ok&&Array.isArray(u.result)?u.result.length:0;
   out.uiaIdMatched=u.idMatched;
  }catch{out.uia='UIA_TIMEOUT';}
  const exact=spawnSync('pwsh',['-NoProfile','-NonInteractive','-File',
   path.join(__dirname,'public-browser-uia-same-hwnd.ps1'),
   '-Hwnd',chosen.hwnd,'-ExpectedPid',String(pid)],
   {encoding:'utf8',windowsHide:true,timeout:10000,maxBuffer:8192,stdio:['ignore','pipe','pipe']});
  if(exact.status===0){
   try{
    const direct=JSON.parse(exact.stdout.trim());
    out.independent={status:direct.status,childCount:direct.childCount||0,
     rootFound:direct.rootFound===true,pidMatched:direct.pidMatched===true,
     errorClass:direct.errorClass||null};
   }catch{out.independent={status:'MALFORMED_INDEPENDENT_RECEIPT'};}
  }else out.independent={status:'INDEPENDENT_PROBE_FAILED',
    failureClass:exact.error?.code||('EXIT_'+String(exact.status))};
  if(process.env.WINAPP_EXE&&process.env.WINAPP_EGRESS_DENIED==='1'){
   const start=Date.now();
   const v=spawnSync(process.env.WINAPP_EXE,
    ['ui','inspect','-w',BigInt(chosen.hwnd).toString(10),'--depth','1','--json'],
    {encoding:'utf8',windowsHide:true,timeout:20000,maxBuffer:262144,stdio:['ignore','pipe','pipe'],
     env:{...process.env,WINAPP_CLI_TELEMETRY_OPTOUT:'1',WINAPP_CLI_UPDATE_CHECK:'0'}});
   out.winapp={status:'NOT_QUALIFIED',durationMs:Date.now()-start};
   if(v.error){out.winapp.status=v.error.code==='ETIMEDOUT'?'TIMEOUT':'PROCESS_ERROR';}
   else if(v.status!==0){out.winapp.status='COMMAND_FAILED';}
   else{
    try{
     const parsed=JSON.parse(v.stdout.trim());
     const matches=Array.isArray(parsed.windows)?parsed.windows.filter(w=>{
      try{return BigInt(w.hwnd)===BigInt(chosen.hwnd);}catch{return false;}
     }):[];
     out.winapp.status=matches.length===1?'WINDOW_MATCH':'JSON_TARGET_MISMATCH';
     if(matches.length===1)out.winapp.elementCount=Number(matches[0].elementCount)||0;
    }catch{out.winapp.status='JSON_PARSE_FAILED';}
   }
  }else out.winapp={status:'NOT_ADMITTED'};
  out.sameTargetCompared=true;
 }catch{out.status='WINDOW_ENUM_UNAVAILABLE';}
 return out;
}
function stop(){if(daemon?.pid)spawnSync('taskkill',['/PID',String(daemon.pid),'/T','/F'],{stdio:'ignore',timeout:5000});fs.rmSync(work,{recursive:true,force:true});}
module.exports={start,observe,stop};
