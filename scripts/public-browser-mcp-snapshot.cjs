// MCP stdio read-only comparator. One allowlisted tool, synthetic target only.
// Parse protocol metadata and element counts; never store names, text, paths or screenshots.
'use strict';
const {spawn}=require('node:child_process');
const readline=require('node:readline');
async function inspect(hwnd){
 const out={status:'NOT_ADMITTED',stdio:'MCP_JSONRPC',allowlist:'ui_snapshot'};
 if(!process.env.MCP_RDTE_EXE||process.env.MCP_RDTE_EGRESS_DENIED!=='1'
   ||process.env.WINDOWS_MCP_TOOLS!=='ui_snapshot')return out;
 let child,rl,timer,done=false;
 const pending=new Map();
 let bufferTotal=0;
 const closePending=(error)=>{
  for(const [id,wait] of pending){pending.delete(id);wait.reject(error);}
 };
 try{
  const start=Date.now();
  child=spawn(process.env.MCP_RDTE_EXE,['--tools','ui_snapshot'],{
   windowsHide:true,env:{...process.env,WINDOWS_MCP_TOOLS:'ui_snapshot'},
   stdio:['pipe','pipe','ignore']});
  rl=readline.createInterface({input:child.stdout,crlfDelay:Infinity});
  rl.on('line',line=>{
   bufferTotal+=Buffer.byteLength(line);
   if(bufferTotal>524288){closePending(Error('RESPONSE_TOO_LARGE'));child.kill();return;}
   try{const obj=JSON.parse(line);if(typeof obj.id!=='number')return;
    const p=pending.get(obj.id);if(!p)return;
    pending.delete(obj.id);p.resolve(obj);
   }catch{/* ignore non-JSON vendor stdout: never log */}
  });
  child.on('error',err=>closePending(err));
  child.on('exit',()=>closePending(Error('PROCESS_EXITED')));
  const call=(id,method,params)=>new Promise((resolve,reject)=>{
   if(!child||!child.stdin.writable)return reject(Error('TRANSPORT_CLOSED'));
   pending.set(id,{resolve,reject});
   child.stdin.write(JSON.stringify({jsonrpc:'2.0',id,method,params})+'\n');
  });
  const guard=new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error('TIMEOUT')),20000)});
  const task=(async()=>{
   const init=await call(1,'initialize',{protocolVersion:'2025-03-26',capabilities:{},
    clientInfo:{name:'wininspect-public-rdte',version:'0.1'}});
   if(init.error)throw Error('INIT_REJECTED');
   if(!init.result?.serverInfo?.name)throw Error('INIT_NOT_MCP');
   child.stdin.write(JSON.stringify({jsonrpc:'2.0',method:'notifications/initialized'})+'\n');
   const listed=await call(2,'tools/list',{});
   if(listed.error||!Array.isArray(listed.result?.tools))throw Error('TOOLS_LIST_FAILED');
   const names=listed.result.tools.map(t=>t.name);
   if(names.length!==1||names[0]!=='ui_snapshot'){
    out.exposedToolCount=names.length;throw Error('UNSAFE_TOOL_SURFACE');
   }
   out.exposedToolCount=1;out.serverName=init.result.serverInfo.name;
   const snapshot=await call(3,'tools/call',{name:'ui_snapshot',arguments:{
    windowHandle:BigInt(hwnd).toString(10),maxDepth:1,mode:'full',includeDiagnostics:false}});
   if(snapshot.error)throw Error('MCP_RPC_FAILURE');
   if(snapshot.result?.isError===true)throw Error('SNAPSHOT_RETURNED_ERROR');
   const content=snapshot.result?.content;
   if(!Array.isArray(content))throw Error('SNAPSHOT_CONTENT_MISSING');
   const text=content.filter(c=>c.type==='text'&&typeof c.text==='string').map(x=>x.text).join('\n');
   if(text.length>262144)throw Error('SNAPSHOT_TOO_LARGE');
   let parsed;
   try{parsed=JSON.parse(text);}catch{throw Error('SNAPSHOT_NOT_JSON');}
   const data=parsed.data??parsed;
   const tree=data.tree??data.elements??data;
   function nodes(n,depth=0){
    if(depth>12||!n||typeof n!=='object')return 0;
    if(Array.isArray(n))return n.reduce((s,x)=>s+nodes(x,depth+1),0);
    if(Array.isArray(n.children))return 1+nodes(n.children,depth+1);
    return Array.isArray(n.elements)?nodes(n.elements,depth+1):(n.controlType?1:0);
   }
   out.rootOrElementCount=nodes(tree);
   out.hasStructuredTree=typeof tree==='object'&&tree!==null;
   out.resultKeys=Object.keys(data).filter(x=>/^(action|success|snapshotIncomplete|kind|status|tree)$/.test(x)).sort();
   out.status=out.hasStructuredTree?'STRUCTURED_SNAPSHOT':'NO_TREE';
   out.durationMs=Date.now()-start;
  })();
  await Promise.race([task,guard]);
 }catch(e){
  out.status='FAILED';out.failureClass=String(e?.message||'UNKNOWN').slice(0,48);
 }finally{
  if(timer)clearTimeout(timer);
  closePending(Error('CLOSED'));
  if(rl)rl.close();
  if(child){
   try{child.stdin.end();child.kill();}catch{}
   await new Promise(resolve=>{if(child.exitCode!==null)return resolve();
    const t=setTimeout(resolve,1500);child.once('exit',()=>{clearTimeout(t);resolve();});
   });
  }
 }
 return out;
}
module.exports={inspect};
