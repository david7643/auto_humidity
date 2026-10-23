const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const html = fs.readFileSync('web/index.html','utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
const elements = new Map();
function element(id) {
  if(!elements.has(id)) elements.set(id,{value:'',checked:false,disabled:false,textContent:'',children:[],
    replaceChildren(){this.children=[]},append(x){this.children.push(x)}});
  return elements.get(id);
}
const state = {rev:1,t:2500,h:4500,avg:4450,trend:0,valid:1,ready:1,age:10,at:60,
 target:45,p:2,l:1,m:2,wanted:1,src:1,sync:0,fault:0,high:0,errors:0,
 counts:[0,0,0],saved:[1,1,0],learn:0,slot:0,proto:7,bits:32,raw:0,tested:0,remembered:2,resume:0,sleep:1};
const calls=[];
let errorCode=0;
const context=vm.createContext({document:{getElementById:element,createElement:()=>element('button'+Math.random())},
 confirm:()=>true,URLSearchParams,AbortSignal,
 setTimeout(){},console,
 fetch:async(path,options)=>{
   calls.push({path,options});
   if(options?.method==='POST')return{ok:!errorCode,status:errorCode||200};
   return{ok:true,json:async()=>path==='/api/events'?[[1,0,0]]:{...state}};
 }});
vm.runInContext(script,context);
const settle=()=>new Promise(resolve=>setImmediate(resolve));
(async()=>{
 await settle();await settle();
 assert.match(element('reading').textContent,/25.0.*45.0/);
 assert.equal(element('remote').children[2].disabled,true);
 assert.equal(html.includes('id="key"'),false);
 element('power').value='1';element('level').value='2';element('sleep').value='1';
 await vm.runInContext('syncState()',context);
 let post=calls.find(x=>x.options?.method==='POST');
 assert.equal(Object.hasOwn(post.options.headers,'X-Control-Key'),false);
 assert.equal(post.options.body.toString(),'r=1&a=1&b=2&c=1&d=0');
 assert.match(element('state').textContent,/기억 2/);
 element('power').value='2';element('level').value='1';element('sleep').value='2';element('resume').value='2';
 await vm.runInContext('syncState()',context);
 assert.equal(calls.filter(x=>x.options?.method==='POST').at(-1).options.body.toString(),'r=1&a=2&b=1&c=2&d=2');
 errorCode=409;let before=calls.filter(x=>x.options?.method==='POST').length;
 await vm.runInContext("act('/api/ir/send',{a:0})",context);
 assert.equal(calls.filter(x=>x.options?.method==='POST').length,before+1);
 assert.match(element('msg').textContent,/자동 재전송하지 않습니다/);
 state.learn=2;state.tested=1;await vm.runInContext('refresh()',context);
 assert.equal(element('save').disabled,false);
 state.valid=0;await vm.runInContext('refresh()',context);
 assert.match(element('reading').textContent,/센서/);
 console.log('All web tests passed');
})().catch(e=>{console.error(e);process.exitCode=1});
