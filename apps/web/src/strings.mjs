import {KeyContacts} from './keyboard.mjs';

// Touch lanes are chord voicings inside the existing C3–C5 bitmap range.
// They do not represent guitar tuning or hardware voice allocation.
const CHORDS = [
  ['C',[0,4,7,12,16,19]],['Am',[4,9,12,16,21,24]],
  ['Dm',[2,5,9,14,17,21]],['F',[0,5,9,12,17,21]],
  ['G',[2,7,11,14,19,23]],['Em',[4,7,11,16,19,23]]
];
const NAMES = ['C','C♯','D','D♯','E','F','F♯','G','G♯','A','A♯','B'];
const noteName = key => NAMES[key%12]+(3+Math.floor(key/12));

// Return the first point where a motion segment enters a target rectangle.
// Sampling crossed targets immediately avoids delayed/queued client gestures.
function entryTime(from,to,box) {
  let first=0,last=1;
  for(const [start,end,low,high] of [[from.x,to.x,box.left,box.right],[from.y,to.y,box.top,box.bottom]]) {
    const distance=end-start;
    if(distance===0){if(start<low||start>=high)return null;continue;}
    const a=(low-start)/distance,b=(high-start)/distance;
    first=Math.max(first,Math.min(a,b));last=Math.min(last,Math.max(a,b));
    // A corner-only touch does not pass through this cell. The final pointer
    // position is hit-tested separately using half-open cell boundaries.
    if(first>=last)return null;
  }
  return first;
}

export function mountStrings(container,controller) {
  const document=container.ownerDocument||globalThis.document;
  const contacts=new KeyContacts(),pointers=new Map(),keyboard=new Map();
  let cells=[],layout='chords',active=true,rows=6,columns=6;
  container.classList.add('strings-grid');

  function redraw() {
    const held=new Set([...pointers.values()].map(pointer=>pointer.cell).concat([...keyboard.values()]));
    for(const cell of cells){const pressed=held.has(cell);cell.classList.toggle('pressed',pressed);cell.setAttribute('aria-pressed',String(pressed));}
  }
  function change(id,cell) {
    const before=contacts.mask;
    if(cell)contacts.press(id,Number(cell.dataset.stringKey));else contacts.release(id);
    redraw();
    if(before===contacts.mask)return true;
    if(controller.keys(contacts.mask)!==false)return true;
    // A gate may close synchronously. Drop this local intent and offer only
    // the safe remaining subset; never retain an unsent On for later replay.
    contacts.release(id);redraw();controller.keys(contacts.mask);return false;
  }
  function clear() {
    const ids=[...pointers.keys()];pointers.clear();keyboard.clear();contacts.clear();redraw();
    for(const id of ids)try{container.releasePointerCapture(id);}catch{/* Capture may already be lost. */}
  }
  function releasePointer(id) {
    const pointer=pointers.get(id);if(!pointer)return;
    pointers.delete(id);change('p'+id,null);
    try{container.releasePointerCapture(id);}catch{/* Synthetic events and cancelled captures. */}
  }
  function moveTo(id,pointer,cell) {
    if(pointers.get(id)!==pointer||pointer.cell===cell)return;
    pointer.cell=cell;
    if(!change('p'+id,cell))releasePointer(id);
  }
  function releaseKeyboard(cell) {
    for(const [id,held] of keyboard)if(held===cell){keyboard.delete(id);change(id,null);}
  }
  function update(enabled=true) {
    active=!!enabled;
    if(!active)clear();
    for(const cell of cells)cell.disabled=!active||!controller.playable;
    redraw();
  }
  function render() {
    container.replaceChildren();cells=[];rows=layout==='chords'?6:5;columns=layout==='chords'?6:5;
    container.dataset.layout=layout;container.style.setProperty('--string-columns',String(columns));
    for(let column=0;column<columns;column++) {
      const label=document.createElement('span');label.className='string-name';
      label.textContent=layout==='chords'?CHORDS[column][0]:noteName(column*5)+'–'+noteName(column*5+4);
      container.append(label);
    }
    for(let row=0;row<rows;row++)for(let column=0;column<columns;column++) {
      const key=layout==='chords'?CHORDS[column][1][5-row]:column*5+4-row;
      const cell=document.createElement('button');cell.type='button';cell.className='string-cell';
      cell.dataset.stringKey=String(key);cell.dataset.stringRow=String(row);cell.dataset.stringColumn=String(column);
      cell.setAttribute('aria-label',(layout==='chords'?CHORDS[column][0]+' 和弦，第'+(row+1)+'弦，':'')+noteName(key)+'，按住拨弦');
      const wire=document.createElement('span');wire.className='string-wire';wire.setAttribute('aria-hidden','true');
      const note=document.createElement('span');note.className='string-note';note.textContent=noteName(key);cell.append(wire,note);
      cell.addEventListener('pointerdown',event=>{
        if(!active||!controller.playable||event.button>0)return;
        event.preventDefault();
        const pointer={cell,x:event.clientX,y:event.clientY};pointers.set(event.pointerId,pointer);
        try{container.setPointerCapture(event.pointerId);}catch{/* Synthetic test events have no capture. */}
        if(!change('p'+event.pointerId,cell))releasePointer(event.pointerId);
      });
      cell.addEventListener('keydown',event=>{
        const directions={ArrowLeft:[0,-1],ArrowRight:[0,1],ArrowUp:[-1,0],ArrowDown:[1,0]};
        const direction=directions[event.code];
        if(direction){event.preventDefault();const nextRow=Math.max(0,Math.min(rows-1,row+direction[0])),nextColumn=Math.max(0,Math.min(columns-1,column+direction[1]));cells[nextRow*columns+nextColumn].focus();return;}
        if(!['Space','Enter'].includes(event.code)||!active||!controller.playable)return;
        event.preventDefault();if(event.repeat)return;
        const id='k'+row+':'+column+':'+event.code;keyboard.set(id,cell);
        if(!change(id,cell)){keyboard.delete(id);redraw();}
      });
      cell.addEventListener('keyup',event=>{
        if(!['Space','Enter'].includes(event.code))return;
        event.preventDefault();const id='k'+row+':'+column+':'+event.code;
        if(keyboard.has(id)){keyboard.delete(id);change(id,null);}
      });
      cell.addEventListener('blur',()=>releaseKeyboard(cell));
      cells.push(cell);container.append(cell);
    }
    update(active);
  }
  container.addEventListener('pointermove',event=>{
    const id=event.pointerId,pointer=pointers.get(id);if(!pointer)return;
    event.preventDefault();
    if(!active||!controller.playable){releasePointer(id);return;}
    const to={x:event.clientX,y:event.clientY};
    if(!Number.isFinite(to.x)||!Number.isFinite(to.y))return;
    const targets=cells.map(cell=>({cell,box:cell.getBoundingClientRect()}));
    const hits=targets.map(target=>({...target,time:entryTime(pointer,to,target.box)})).filter(target=>target.time!==null).sort((a,b)=>a.time-b.time);
    for(const target of hits){
      if(pointers.get(id)!==pointer)break;
      if(!active||!controller.playable){releasePointer(id);break;}
      moveTo(id,pointer,target.cell);
    }
    const final=targets.find(({box})=>to.x>=box.left&&to.x<box.right&&to.y>=box.top&&to.y<box.bottom)?.cell||null;
    if(pointers.get(id)===pointer){
      if(!active||!controller.playable)releasePointer(id);
      else {moveTo(id,pointer,final);pointer.x=to.x;pointer.y=to.y;}
    }
  });
  for(const type of ['pointerup','pointercancel','lostpointercapture'])container.addEventListener(type,event=>releasePointer(event.pointerId));
  render();
  return {clear,update,contacts,setLayout(name){
    if(!['chords','chromatic'].includes(name))throw new RangeError('unknown string layout');
    if(name===layout)return;
    controller.releaseOwn?.();clear();layout=name;render();
  }};
}
