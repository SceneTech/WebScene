// Each producer/consumer is a distinct process: V8's in-memory compilation cache
// cannot accidentally make a persisted-cache test pass.
const assert = require('node:assert/strict');
const {spawnSync} = require('node:child_process');
const path = require('node:path');
const fs = require('node:fs');
const os = require('node:os');
const adapter = path.resolve(process.env.WEBSCENE_TEST_ADAPTER || path.join(__dirname,'adapter.node'));
const node = process.execPath;
function child(body, input) {
  const result = spawnSync(node, ['-e', `const a=require(${JSON.stringify(adapter)}); const x=JSON.parse(require('node:fs').readFileSync(0,'utf8')); ${body}`],
    {input:JSON.stringify(input), encoding:'utf8', timeout:15000});
  assert.equal(result.status,0,result.stderr||result.stdout);
  return JSON.parse(result.stdout);
}
function produce(source,module=false) {
  return child(`const p=a.produce(x.source,x.module); p.data=p.data.toString('base64'); console.log(JSON.stringify(p))`,{source,module});
}
function consume(source,p,module=false,mutation='') {
  return child(`x.p.data=Buffer.from(x.p.data,'base64'); ${mutation}
    const registration=a.install(x.source,x.module,x.p);
    try { const result=a.run(x.source,x.module); console.log(JSON.stringify({registration,result,stats:a.stats()})); }
    catch(e) { console.log(JSON.stringify({registration,error:String(e),stats:a.stats()})); }`,{source,p,module});
}
let passed=0;
function test(name,body) { body(); console.log('PASS '+name); ++passed; }
test('cold classic cache executes relocated script and closure',()=>{
  const s='function make(x){return y=>x+y;} make(20)(22);';const r=consume(s,produce(s));
  assert.equal(r.result,42);assert.equal(r.stats.hits,1);assert.equal(r.stats.rejections,0);
});
test('cold ES module cache executes after relocation',()=>{
  const s='export function f(){return 42;} export const answer=f();';const r=consume(s,produce(s,true),true);
  assert.equal(r.result,42);assert.equal(r.stats.hits,1);
});
test('producer never executes top-level application code',()=>{
  assert.ok(produce('throw new Error("DO NOT EXECUTE AT BUILD TIME");').data.length>0);
});
test('producer does not instantiate imports or resolve host resources',()=>{
  assert.ok(produce('import {remote} from "https://invalid.example/app.mjs"; export const answer=remote;',true).data.length>0);
});
test('syntax diagnostics include source name',()=>{
  const result=child(`try {a.produce(x.source,false);console.log('null');}catch(e){console.log(JSON.stringify(String(e)));}`,{source:'const = ;'});
  assert.match(result,/build:\/\/app.js/);assert.match(result,/line 1/);
});
test('Unicode and BOM source survive code cache round trip',()=>{
  const s='\ufeffconst café="Olá 🌍"; café.length;';const r=consume(s,produce(s));assert.equal(r.result,6);
});
test('cache survives exceptions and prototype/object operations',()=>{
  const s='class Base { value(){return 40;} } class Child extends Base { value(){try{throw 2;}catch(x){return super.value()+x;}} } new Child().value();';
  assert.equal(consume(s,produce(s)).result,42);
});
test('V8 version mismatch fails explicitly',()=>{
  const s='40+2; //version';const p=produce(s);p.version+='-wrong';const r=consume(s,p);
  assert.match(r.error,/does not match/);assert.equal(r.stats.hits,0);assert.equal(r.stats.rejections,1);
});
test('runtime flags tag mismatch fails explicitly',()=>{
  const s='40+2; //flags';const p=produce(s);p.tag=(p.tag+1)>>>0;assert.match(consume(s,p).error,/does not match/);
});
test('bootstrap snapshot mismatch fails explicitly',()=>{
  const s='40+2; //snapshot';const p=produce(s);p.snapshot='different';assert.match(consume(s,p).error,/does not match/);
});
test('bad payload hash rejected before V8 sees data',()=>{
  const s='42; //hash';const p=produce(s);
  assert.equal(child(`x.p.data=Buffer.from(x.p.data,'base64'); console.log(JSON.stringify(a.install(x.source,false,x.p,true)));`,{source:s,p}),-1);
});
test('V8 rejected cache never executes source fallback',()=>{
  const s='42; //payload';const p=produce(s);
  const r=consume(s,p,false,'x.p.data[0]^=0xff;');
  assert.match(r.error,/V8 rejected/);assert.equal(r.stats.hits,0);
});
test('same-length changed source cannot alias a registered entry',()=>{
  const s='20+22;';const p=produce(s);
  const result=child(`x.p.data=Buffer.from(x.p.data,'base64'); a.install(x.source,false,x.p);
    try{a.run('20+23;',false);console.log('null');}catch(e){console.log(JSON.stringify(String(e)));}`,{source:s,p});
  assert.match(result,/No registered cache/);
});
test('classic and module identities cannot collide',()=>{
  const s='42;';const p=produce(s);
  const result=child(`x.p.data=Buffer.from(x.p.data,'base64'); a.install(x.source,false,x.p);
    try{a.run(x.source,true);console.log('null');}catch(e){console.log(JSON.stringify(String(e)));}`,{source:s,p});assert.match(result,/No registered cache/);
});
test('registration copies borrowed data and deduplicates',()=>{
  const s='21*2;';const p=produce(s);
  const r=child(`x.p.data=Buffer.from(x.p.data,'base64'); const first=a.install(x.source,false,x.p);const second=a.install(x.source,false,x.p);
    x.p.data.fill(0);console.log(JSON.stringify({first,second,result:a.run(x.source,false),stats:a.stats()}));`,{source:s,p});
  assert.equal(r.first,0);assert.equal(r.second,0);assert.equal(r.result,42);assert.equal(r.stats.registered,1);
});
test('compiler CLI emits compilable C++ without executing JS',()=>{
  const temp=fs.mkdtempSync(path.join(os.tmpdir(),'webscene cache '));
  try {
    const source=path.join(temp,'app.js'),output=path.join(temp,'app.cpp');
    fs.writeFileSync(source,'throw new Error("not at build time");');
    const result=spawnSync(node,['-e',`const a=require(${JSON.stringify(adapter)}); process.exit(a.cli(process.argv.slice(1)));`,
      '--','--input',source,'--output',output,'--name','strange"\nname.js'],{encoding:'utf8',timeout:15000});
    assert.equal(result.status,0,result.stderr);assert.ok(fs.readFileSync(output,'utf8').includes('webscene_register_precompiled_javascript_v1'));
    const include=process.env.WEBSCENE_TEST_INCLUDE || path.resolve(__dirname,'../../native');
    if(include) {
      const cc=spawnSync(process.env.CXX||'c++',['-std=c++20','-c',output,'-I',path.resolve(__dirname,'../../native'),'-I',include,'-o',path.join(temp,'app.o')],{encoding:'utf8'});
      assert.equal(cc.status,0,cc.stderr);
    }
  } finally {fs.rmSync(temp,{recursive:true,force:true});}
});
console.log(JSON.stringify({passed,v8:process.versions.v8,node:process.version}));
