// Validates supplier-neutral primitive impact catalog as evidence, not a coverage claim.
'use strict';
const fs=require('node:fs'),path=require('node:path');
const file=path.resolve(__dirname,'../qualification/primitive-impact-catalog.json');
const c=JSON.parse(fs.readFileSync(file,'utf8'));
const errors=[],warn=[];
function assert(ok,message){if(!ok)errors.push(message);}
function unique(items){return new Set(items).size===items.length;}
const tiers=new Set(c.evidence_tiers),outcomes=new Set(c.outcomes);
const defs=new Set(c.primitive_definitions.map(x=>x.id));
assert(c.schema==='wininspect-primitive-impact-rdte/v1','schema mismatch');
assert(unique(c.primitive_definitions.map(x=>x.id)),'duplicate primitive IDs');
assert(unique(c.workflows.map(x=>x.id)),'duplicate workflow IDs');
assert(unique(c.source_inventory.methods),'duplicate WinInspect methods');
assert(c.source_inventory.methods.length===c.source_inventory.wininspect_dispatch_count,'source method count mismatch');
for(const def of c.primitive_definitions){
 for(const p of ['id','contract','oracle','preconditions'])assert(typeof def[p]==='string'&&def[p].length>0,'missing primitive '+p+': '+def.id);
}
for(const claim of c.provider_claims){
 assert(defs.has(claim.primitive),'unknown primitive '+claim.primitive);
 assert(tiers.has(claim.tier),'invalid evidence tier '+claim.provider);
 assert(outcomes.has(claim.outcome),'invalid outcome '+claim.provider);
 if(['PASS','FAIL'].includes(claim.outcome)){
  assert(['EXECUTED_ON_EXACT_TARGET','CONSUMER_QUALIFIED'].includes(claim.tier),'runtime outcome without runtime evidence '+claim.provider);
  assert(typeof claim.evidence==='string'&&claim.evidence.startsWith('https://'),'runtime outcome lacks evidence '+claim.provider);
 }
 if(claim.tier==='SOURCE_CONFIRMED'&&claim.outcome==='PASS')errors.push('source-only claim cannot pass '+claim.provider);
}
for(const w of c.workflows){
 assert(Array.isArray(w.required)&&w.required.length>0,'workflow no requirements: '+w.id);
 for(const set of w.required)for(const id of set)assert(defs.has(id),'unknown workflow primitive '+w.id+'/'+id);
 if(w.status.startsWith('QUALIFIED'))assert(Array.isArray(w.evidence)&&w.evidence.length>0,'qualified workflow without receipt '+w.id);
 if(w.status==='NOT_QUALIFIED')assert(w.outcome==='NOT_EXECUTED','unqualified work must remain not executed '+w.id);
 if(typeof w.wininspect_marginal_utility==='string'&&/IMPROVED|DOMINATED|WINNER/.test(w.wininspect_marginal_utility))warn.push('causal attribution requires paired ablation '+w.id);
}
const score={
 schema:'wininspect-primitive-impact-validation/v1',valid:errors.length===0,
 methods:c.source_inventory.methods.length,primitiveContracts:defs.size,
 providerClaims:c.provider_claims.length,workflows:c.workflows.length,
 sourceOnlyClaims:c.provider_claims.filter(x=>x.tier==='SOURCE_CONFIRMED').length,
 executedClaims:c.provider_claims.filter(x=>x.tier==='EXECUTED_ON_EXACT_TARGET').length,
 qualifiedTaskCount:c.workflows.filter(x=>x.status.startsWith('QUALIFIED')).length,
 unqualifiedTaskCount:c.workflows.filter(x=>x.status==='NOT_QUALIFIED').length,
 warnings:warn,errors
};
console.log(JSON.stringify(score,null,2));
if(errors.length)process.exitCode=1;
