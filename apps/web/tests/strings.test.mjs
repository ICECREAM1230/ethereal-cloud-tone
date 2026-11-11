import test from 'node:test';
import assert from 'node:assert/strict';
import {mountStrings} from '../src/strings.mjs';

class Element {
  constructor(tag='div') {
    this.tagName=tag;this.children=[];this.listeners=new Map();this.dataset={};this.attributes={};
    const classes=new Set();this.classList={add:value=>classes.add(value),toggle:(value,on)=>on?classes.add(value):classes.delete(value),contains:value=>classes.has(value)};
    this.style={setProperty:(key,value)=>this.style[key]=value};this.disabled=false;
  }
  append(...children){for(const child of children){child.parentElement=this;this.children.push(child);}}
  replaceChildren(...children){this.children=[];this.append(...children);}
  setAttribute(key,value){this.attributes[key]=String(value);}
  addEventListener(type,fn){if(!this.listeners.has(type))this.listeners.set(type,[]);this.listeners.get(type).push(fn);}
  dispatch(type,values={}) {
    const event={type,target:this,button:0,pointerId:1,preventDefault(){this.defaultPrevented=true;},...values};
    for(let node=this;node;node=node.parentElement)for(const fn of node.listeners.get(type)||[])fn(event);
    return event;
  }
  getBoundingClientRect(){const column=Number(this.dataset.stringColumn),row=Number(this.dataset.stringRow);return {left:column*100,right:(column+1)*100,top:30+row*60,bottom:90+row*60,width:100,height:60};}
  focus(){this.ownerDocument.focused=this;}
  setPointerCapture(){}
  releasePointerCapture(){}
}
function fixture(){
  const document={createElement:tag=>Object.assign(new Element(tag),{ownerDocument:document})};
  const container=document.createElement('div'),masks=[];
  const controller={playable:true,keys(mask){masks.push(mask);return true;},releaseOwn(){masks.push(0);}};
  const view=mountStrings(container,controller);view.update();
  const cells=()=>container.children.filter(child=>child.tagName==='button');
  const cell=(row,column)=>cells().find(child=>Number(child.dataset.stringRow)===row&&Number(child.dataset.stringColumn)===column);
  const point=button=>{const box=button.getBoundingClientRect();return {clientX:box.left+50,clientY:box.top+30};};
  const down=(row,column,id=1)=>{const button=cell(row,column);button.dispatch('pointerdown',{pointerId:id,...point(button)});return button;};
  const move=(row,column,id=1)=>container.dispatch('pointermove',{pointerId:id,...point(cell(row,column))});
  const up=(id=1,type='pointerup')=>container.dispatch(type,{pointerId:id});
  return {container,controller,view,cells,cell,point,down,move,up,masks,document};
}

test('chord intersections preserve App voicings, high strings above low strings',()=>{
  const {container,cells,cell,masks}=fixture();
  assert.equal(cells().length,36);assert.equal(container.dataset.layout,'chords');
  assert.deepEqual(container.children.filter(child=>child.tagName==='span').map(child=>child.textContent),['C','Am','Dm','F','G','Em']);
  assert.deepEqual(Array.from({length:6},(_,row)=>Number(cell(row,0).dataset.stringKey)),[19,16,12,7,4,0]);
  assert.equal(cell(0,1).dataset.stringKey,'24');assert.equal(masks.length,0,'mount must not send');
});
test('a single pluck sends On then Off and complete release permits a fresh same-note pluck',()=>{
  const {down,up,masks,view}=fixture();down(5,0);up();down(5,0);up();
  assert.deepEqual(masks,[1,0,1,0]);assert.equal(view.contacts.mask,0);
});
test('same-note multi-finger contacts keep a bit until the final finger releases',()=>{
  const {down,up,masks,view}=fixture();down(5,0,1);down(5,3,2);up(1);
  assert.equal(view.contacts.mask,1);assert.equal(masks.at(-1),1);up(2);assert.equal(masks.at(-1),0);
});
test('a fast vertical drag interpolates crossed strings and retains another finger',()=>{
  const {down,move,up,masks,view}=fixture();down(0,1,2);down(0,0,1);const before=masks.length;move(5,0,1);
  assert.deepEqual(masks.slice(before),[16,12,7,4,0].map(key=>(1<<24)|(1<<key)));
  up(1);assert.equal(view.contacts.mask,1<<24);up(2);assert.equal(view.contacts.mask,0);
});
test('diagonal motion does not pluck cells touched only at their shared corner',()=>{
  const {down,move,up,masks}=fixture();down(0,0);move(1,1);move(0,0);up();
  assert.deepEqual(masks,[1<<19,1<<21,1<<19,0]);
});
test('moving outside releases the note; returning during that gesture can pluck again',()=>{
  const {container,down,move,up,masks}=fixture();down(5,0);
  container.dispatch('pointermove',{pointerId:1,clientX:-50,clientY:360});assert.equal(masks.at(-1),0);
  move(5,0);assert.equal(masks.at(-1),1);up();assert.equal(masks.at(-1),0);
});
for(const type of ['pointercancel','lostpointercapture'])test(type+' releases only its own finger and late events do nothing',()=>{
  const {down,up,masks,view}=fixture();down(5,0,1);down(0,1,2);up(1,type);up(1,type);
  assert.equal(view.contacts.mask,1<<24);assert.equal(masks.at(-1),1<<24);up(2);assert.equal(masks.at(-1),0);
});
test('clear cancels every gesture and keyboard hold without replaying late moves or releases',()=>{
  const {down,move,up,cell,view,masks}=fixture();down(5,0);cell(0,1).dispatch('keydown',{code:'Space'});
  view.clear();const count=masks.length;move(0,0);up();cell(0,1).dispatch('keyup',{code:'Space'});
  assert.equal(view.contacts.mask,0);assert.equal(masks.length,count);down(5,0);assert.equal(masks.at(-1),1);
});
test('inactive and unplayable surfaces disable all controls and reject new input',()=>{
  const {view,cells,down,controller,masks}=fixture();view.update(false);down(5,0);
  assert(cells().every(cell=>cell.disabled));assert.deepEqual(masks,[]);
  view.update();controller.playable=false;view.update();down(5,0);assert.deepEqual(masks,[]);
});
test('safe pointer release still submits Off if playability changes before update',()=>{
  const {controller,down,up,masks}=fixture();down(5,0);controller.playable=false;up();assert.deepEqual(masks,[1,0]);
});
test('Space and Enter hold notes, ignore repeat and blur clears keyboard contacts',()=>{
  const {cell,masks,view}=fixture();const button=cell(5,0);
  button.dispatch('keydown',{code:'Space'});button.dispatch('keydown',{code:'Space',repeat:true});
  button.dispatch('keyup',{code:'Space'});button.dispatch('keydown',{code:'Enter'});button.dispatch('blur');
  assert.deepEqual(masks,[1,0,1,0]);assert.equal(view.contacts.mask,0);
});
test('arrow keys move focus through the string matrix without sending notes',()=>{
  const {cell,document,masks}=fixture();cell(1,1).dispatch('keydown',{code:'ArrowDown'});assert.equal(document.focused,cell(2,1));
  cell(0,0).dispatch('keydown',{code:'ArrowLeft'});assert.equal(document.focused,cell(0,0));assert.deepEqual(masks,[]);
});
test('chromatic layout covers all 25 pitches and layout changes release before replacing controls',()=>{
  const {view,cells,cell,down,move,masks}=fixture();down(5,0);view.setLayout('chromatic');
  assert.equal(masks.at(-1),0);assert.equal(view.contacts.mask,0);assert.equal(cells().length,25);
  assert.deepEqual(cells().map(button=>Number(button.dataset.stringKey)).sort((a,b)=>a-b),Array.from({length:25},(_,i)=>i));
  assert.equal(cell(0,0).dataset.stringKey,'4');assert.equal(cell(4,0).dataset.stringKey,'0');assert.equal(cell(0,4).dataset.stringKey,'24');
  const count=masks.length;move(0,0);assert.equal(masks.length,count,'old gesture must not survive layout change');
  down(0,4);assert.equal(masks.at(-1),1<<24);assert.throws(()=>view.setLayout('guitar'));
});
test('synchronous controller clear cancels remaining interpolated notes',()=>{
  const f=fixture();f.down(0,0);const original=f.controller.keys;let movements=0;
  f.controller.keys=mask=>{original.call(f.controller,mask);if(++movements===1)f.view.clear();return true;};
  f.move(5,0);assert.equal(movements,1);assert.equal(f.view.contacts.mask,0);f.move(0,0);assert.equal(movements,1);
});
test('a gate closing synchronously during interpolation stops new On and releases its contact',()=>{
  const f=fixture();f.down(0,0);const original=f.controller.keys;const before=f.masks.length;
  f.controller.keys=mask=>{original.call(f.controller,mask);f.controller.playable=false;return true;};
  f.move(5,0);assert.deepEqual(f.masks.slice(before),[1<<16,0]);assert.equal(f.view.contacts.mask,0);
});
test('a gate closing on the final crossed cell releases without needing another move',()=>{
  const f=fixture();f.down(0,0);const original=f.controller.keys;const before=f.masks.length;
  f.controller.keys=mask=>{original.call(f.controller,mask);f.controller.playable=false;return true;};
  f.move(1,0);assert.deepEqual(f.masks.slice(before),[1<<16,0]);assert.equal(f.view.contacts.mask,0);
});
