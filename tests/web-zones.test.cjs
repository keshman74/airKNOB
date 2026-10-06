const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const source = fs.readFileSync('src/main.cpp', 'utf8');
const script = source.split('<script>')[1].split('</script>')[0];
const nodes = new Map();
function node() { return {value:'',disabled:false,textContent:'',options:[],replaceChildren(){this.options=[];this.value='';},append(o){this.options.push(o);if(!this.value)this.value=o.value;}}; }
const document = {getElementById(id){if(!nodes.has(id))nodes.set(id,node());return nodes.get(id);},createElement(){return node();}};
let scanning=false, selectedRequest, reloaded=false;
const zones = ['A31','A97','A98','A33'].map((chip,i)=>({name:'Зона '+chip,chip,ip:'192.168.0.'+(i+2)}));
const context={document,URLSearchParams,clearTimeout(){},setTimeout(){return 1;},location:{reload(){reloaded=true;}},fetch:async(url,options)=>{
 if(url==='/zones/select'){selectedRequest=options.body.get('ip');return {ok:true};}
 if(url==='/zones/scan')return {ok:true};
 return {ok:true,json:async()=>({current:'192.168.0.4',zones,scanning,progress:25})};
}};
vm.runInNewContext(script,context);
(async()=>{
 await context.loadZones();
 assert.equal(nodes.get('zone-list').options.length,4);
 assert.equal(nodes.get('zone-list').value,'192.168.0.4');
 assert.match(nodes.get('zone-list').options[3].textContent,/Зона A33.*A33/);
 assert.equal(nodes.get('zone-select').disabled,false);
 scanning=true;await context.loadZones();assert.equal(nodes.get('zone-select').disabled,true);assert.equal(nodes.get('zone-search').disabled,true);
 scanning=false;await context.loadZones();nodes.get('zone-list').value='192.168.0.5';await nodes.get('zone-select').onclick();
 assert.equal(selectedRequest,'192.168.0.5');assert.equal(reloaded,true);
 console.log('Web zones: all four chips, current selection, scanning lock and selected IP passed');
})().catch(e=>{console.error(e);process.exitCode=1;});
