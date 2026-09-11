// Exercise the actual embedded viewer's bounded JPEG reassembly and controls.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const html = fs.readFileSync('examples/Doorbell/Viewer.h', 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
const elements = new Map();
const drawn = [];
const context = vm.createContext({
  document: {getElementById(id) {
    if (!elements.has(id)) elements.set(id, {disabled:false, value:'test', play:async()=>{},
      getContext:()=>({drawImage(image){drawn.push(image);}})});
    return elements.get(id);
  }},
  Uint8Array, ArrayBuffer, DataView, Blob, console, setTimeout, clearTimeout,
  createImageBitmap:async blob=>({width:320,height:240,close(){},size:blob.size}),
});
vm.runInContext(script, context);
function packet(id, size, offset, payload) {
  const buffer = new ArrayBuffer(16 + payload.length);
  const view = new DataView(buffer);
  [0x47504a53, id, size, offset].forEach((n,i)=>view.setUint32(i*4,n,true));
  new Uint8Array(buffer,16).set(payload);
  return buffer;
}
async function receive(data){context.receive({data});await new Promise(setImmediate);}
(async()=>{
  const candidate='a=candidate:1 1 udp 123 host-id.local 9999 typ host\r\n';
  assert.equal(context.lanOffer(candidate,'192.168.1.20'),'a=candidate:1 1 udp 123 192.168.1.20 9999 typ host\r\n');
  const numeric='a=candidate:2 1 udp 123 10.0.0.1 9000 typ host\r\n';
  assert.equal(context.lanOffer(numeric,'192.168.1.20'),numeric);
  assert.throws(()=>context.lanOffer(candidate,'999.0.0.1'));
  await receive(packet(1,4,0,[0xff,0xd8])); assert.equal(drawn.length,0);
  await receive(packet(1,4,2,[0xff,0xd9])); assert.equal(drawn.length,1);
  await receive(packet(2,4,0,[1,2]));
  await receive(packet(2,4,3,[3])); // missing fragment; discard
  assert.equal(drawn.length,1);
  await receive(packet(3,131073,0,[1])); // bounded allocation
  assert.equal(drawn.length,1);
  await receive(packet(4,4,0,[1,2]));
  await receive(packet(5,4,2,[3,4])); // stale/wrong frame
  assert.equal(drawn.length,1);
  await receive(packet(6,4,0,[1,2]));
  await receive(packet(6,4,2,[3,4])); assert.equal(drawn.length,2);
  await receive(new ArrayBuffer(2));
  await receive('RING'); assert.match(elements.get('status').textContent,/ringing/);
  console.log('PASS viewer: complete frames, gaps, size bounds, stale frames, short packets, ring');
})().catch(e=>{console.error(e);process.exitCode=1;});
