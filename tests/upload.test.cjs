const fs=require('fs'),vm=require('vm'),assert=require('assert');
const source=fs.readFileSync('src/main.cpp','utf8');
const a=source.indexOf('let otaManifest=null'),b=source.indexOf('</script></section>)HTML',a);
const script=source.slice(a,b);
const elements=new Map();function el(id){if(!elements.has(id))elements.set(id,{disabled:false,hidden:true,value:0,textContent:''});return elements.get(id);}
let xhr;class FakeXHR{constructor(){this.upload={};xhr=this;}open(method,url){assert.equal(method,'POST');assert.equal(url,'/update');}setRequestHeader(k,v){this.header=[k,v];}send(form){this.form=form;}}
const ctx={document:{getElementById:el,querySelector:()=>({content:'0.8.2'})},XMLHttpRequest:FakeXHR,URL,AbortController,FormData,Blob,Uint8Array,Error,Number,Date,Math,Promise,setTimeout:()=>0,clearTimeout(){},location:{replace(){}},fetch:()=>{throw Error('unexpected fetch')}};
vm.createContext(ctx);vm.runInContext(script,ctx);
(async()=>{
const promise=vm.runInContext('uploadFirmware({},12345)',ctx);
assert.deepEqual(xhr.header,['X-Firmware-Size','12345']);assert.equal(el('ota-progress').hidden,false);
xhr.upload.onprogress({lengthComputable:true,loaded:500,total:1000});assert.equal(el('ota-progress').value,50);
xhr.upload.onload();assert.match(el('ota-status').textContent,/Проверка/);
xhr.status=200;xhr.responseText='OK';xhr.onload();assert.equal(await promise,'OK');assert.equal(el('ota-progress').value,100);
const failed=vm.runInContext('uploadFirmware({},12345)',ctx);xhr.status=500;xhr.responseText='verification failed';xhr.onload();await assert.rejects(failed,/verification failed/);assert.notEqual(el('ota-progress').value,100);
assert(source.includes("history.replaceState(null,'','/')"));assert(source.includes('String(kFirmwareVersion)'));
console.log('Actual upload JS: progress, verification wait, success and error passed');
})().catch(e=>{console.error(e);process.exit(1)});
