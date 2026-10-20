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
 const out={status:'NO_PID_MATCH',uia:'NOT_RUN'};
 try{
  const x=await rpc('window.listTop');out.idMatched=x.idMatched;
  if(!x.ok||!Array.isArray(x.result))throw Error('unavailable');
  for(const row of x.result.slice(0,20)){
   if(typeof row.hwnd!=='string'||!/^0x[\da-f]+$/i.test(row.hwnd))continue;
   const w=await rpc('window.getInfo',{hwnd:row.hwnd});
   if(w.ok&&Number(w.result?.pid)===pid){
    out.status='WINDOW_MATCH';
    try{const u=await rpc('ui.inspect',{hwnd:row.hwnd});out.uia=u.ok&&Array.isArray(u.result)&&u.result.length>0?'UIA_NONEMPTY':(u.ok?'UIA_EMPTY':'UIA_UNAVAILABLE');
      out.uiaCount=u.ok&&Array.isArray(u.result)?u.result.length:0;
      out.uiaIdMatched=u.idMatched;}
    catch{out.uia='UIA_TIMEOUT';}
    // Compare against the identical HWND/PID while Playwright holds this Edge open.
    const exact=spawnSync('pwsh',['-NoProfile','-NonInteractive','-File',
      path.join(__dirname,'public-browser-uia-same-hwnd.ps1'),
      '-Hwnd',row.hwnd,'-ExpectedPid',String(pid)],
      {encoding:'utf8',windowsHide:true,timeout:10000,maxBuffer:8192,stdio:['ignore','pipe','pipe']});
    if(exact.status===0){
      try{
        const direct=JSON.parse(exact.stdout.trim());
        out.independent={status:direct.status,childCount:direct.childCount||0,
          rootFound:direct.rootFound===true,pidMatched:direct.pidMatched===true,
          errorClass:direct.errorClass||null};
      }catch{out.independent={status:'MALFORMED_INDEPENDENT_RECEIPT'};}
    }else out.independent={status:'INDEPENDENT_PROBE_FAILED'};
    out.sameTargetCompared=true;
    break;
   }
  }
 }catch{out.status='WINDOW_ENUM_UNAVAILABLE';}
 return out;
}
function stop(){if(daemon?.pid)spawnSync('taskkill',['/PID',String(daemon.pid),'/T','/F'],{stdio:'ignore',timeout:5000});fs.rmSync(work,{recursive:true,force:true});}
module.exports={start,observe,stop};
