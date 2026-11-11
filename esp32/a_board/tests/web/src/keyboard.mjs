export class KeyContacts {
  constructor() { this.contacts = new Map(); }
  press(id, key) {
    if (Number.isInteger(key) && key >= 0 && key < 25) this.contacts.set(id,key);
    return this.mask;
  }
  release(id) { this.contacts.delete(id); return this.mask; }
  clear() { this.contacts.clear(); }
  get mask() { let mask = 0; for (const key of this.contacts.values()) mask |= 1 << key; return mask; }
}
export function mountKeyboard(container, controller) {
  const contacts = new KeyContacts(); const keys = [];
  const names = ['C','C♯','D','D♯','E','F','F♯','G','G♯','A','A♯','B'];
  let white = 0;
  function redraw() { for (const key of keys) key.classList.toggle('pressed',!!(contacts.mask & (1 << Number(key.dataset.key)))); }
  function release(id) {
    if (!contacts.contacts.has(id)) return;
    contacts.release(id); redraw(); controller.keys(contacts.mask);
  }
  for (let index=0;index<25;index++) {
    const black = [1,3,6,8,10].includes(index%12);
    const key = document.createElement('button');
    key.type = 'button'; key.dataset.key = index; key.className = 'piano-key '+(black?'black':'white');
    key.textContent = names[index%12]+(3+Math.floor(index/12)); key.setAttribute('aria-label',key.textContent+'，按住演奏');
    key.setAttribute('aria-pressed','false'); key.disabled = true;
    if (black) key.style.left = `calc(${white}/15 * 100% - 2.1%)`;
    else { key.style.left = `calc(${white}/15 * 100%)`; white++; }
    key.addEventListener('pointerdown',event=>{
      if (!controller.playable || event.button > 0) return;
      event.preventDefault();
      try { key.setPointerCapture(event.pointerId); } catch { /* synthetic test events have no capture */ }
      contacts.press('p'+event.pointerId,index); redraw(); controller.keys(contacts.mask);
    });
    for (const name of ['pointerup','pointercancel','lostpointercapture']) key.addEventListener(name,event=>release('p'+event.pointerId));
    // Focused buttons also support accessible Space / Enter key holds.
    key.addEventListener('keydown',event=>{
      if ((event.code==='Space'||event.code==='Enter') && controller.playable) { event.preventDefault(); if (!event.repeat) { contacts.press('k'+index,index); redraw(); controller.keys(contacts.mask); } }
    });
    key.addEventListener('keyup',event=>{ if(event.code==='Space'||event.code==='Enter') {event.preventDefault();release('k'+index);} });
    key.addEventListener('blur',()=>release('k'+index));
    keys.push(key); container.append(key);
  }
  return {
    clear() { contacts.clear(); redraw(); },
    update() { for (const key of keys) { key.disabled = !controller.playable; key.setAttribute('aria-pressed',String(!!(contacts.mask & (1 << Number(key.dataset.key))))); } },
    contacts
  };
}
