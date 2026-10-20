// Demand/coverage report from primitive catalog. This is *not* a causal impact ranking.
// Only paired randomized or tightly matched ablations can estimate marginal utility.
'use strict';
const fs=require('node:fs'),path=require('node:path');
const c=JSON.parse(fs.readFileSync(path.resolve(__dirname,'../qualification/primitive-impact-catalog.json'),'utf8'));
const result=[];
const tiers=['ADVERTISED','SOURCE_CONFIRMED','EXECUTED_ON_EXACT_TARGET','CONSUMER_QUALIFIED'];
for(const p of c.primitive_definitions){
 const uses=c.workflows.filter(w=>w.required.some(set=>set.includes(p.id)));
 const required=uses.filter(w=>w.status.startsWith('QUALIFIED')).map(w=>w.id);
 const pending=uses.filter(w=>!w.status.startsWith('QUALIFIED')).map(w=>w.id);
 const claims=c.provider_claims.filter(cl=>cl.primitive===p.id);
 const passing=claims.filter(cl=>cl.outcome==='PASS' && tiers.indexOf(cl.tier)>=2);
 const failed=claims.filter(cl=>cl.outcome==='FAIL' && tiers.indexOf(cl.tier)>=2);
 const onlySource=claims.filter(cl=>cl.tier==='SOURCE_CONFIRMED' && cl.outcome==='NOT_EXECUTED');
 let state;
 if(failed.length)state='KNOWN_NEGATIVE_NEEDS_DIAGNOSIS';
 else if(pending.length&&!passing.length)state='DEMANDED_NOT_YET_QUALIFIED';
 else if(pending.length)state='REUSE_VALIDATION_NEEDED';
 else if(passing.length)state='EVIDENCE_FOR_EXISTING_MECHANISM';
 else state='NO_EXECUTED_EVIDENCE';
 result.push({primitive:p.id,demandedBy:uses.length,qualifiedSyntheticWorkflowCount:required.length,
  unqualifiedWorkflowCount:pending.length,passingProviderCount:passing.length,
  failingProviderCount:failed.length,sourceOnlyProviderCount:onlySource.length,
  triage:state,qualifiedWorkflows:required,unqualifiedWorkflows:pending,
  marginalTaskSuccessDelta:'UNMEASURED_WITH_PAIRED_ABLATION'});
}
const priorities=result.filter(r=>r.unqualifiedWorkflowCount||r.failingProviderCount)
 .sort((a,b)=>b.unqualifiedWorkflowCount-a.unqualifiedWorkflowCount ||
  b.failingProviderCount-a.failingProviderCount || a.primitive.localeCompare(b.primitive));
const report={schema:'wininspect-primitive-demand-report/v1',warning:
 'Demand counts are seeded workflow occurrences, not real-world usage weights or causal value; observations are synthetic.',
 fromCatalog:c.schema,counts:{providers:c.provider_claims.length,workflows:c.workflows.length,
  primitives:result.length,sourceMethods:c.source_inventory.methods.length},
 priorities,all:result};
const output=process.env.PRIMITIVE_IMPACT_REPORT;
if(output)fs.writeFileSync(output,JSON.stringify(report,null,2));
console.log('PRIMITIVE_DEMAND_REPORT contracts='+result.length+' pending_exposures='+priorities.length);
for(const p of priorities.slice(0,10))console.log(
 'PRIMITIVE='+p.primitive+' pending_workflows='+p.unqualifiedWorkflowCount+
 ' confirmed_providers='+p.passingProviderCount+' state='+p.triage);
