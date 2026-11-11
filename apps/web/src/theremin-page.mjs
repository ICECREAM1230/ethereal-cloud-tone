export function mountThereminPage(doc=globalThis.document){
 const page=doc.getElementById('theremin'),regular=doc.getElementById('regularPages');
 if(!page||!regular||page.dataset.mounted)return;
 page.dataset.mounted='true';let previous=false;
 function render(){
  const active=location.hash==='#theremin';
  page.hidden=!active;regular.hidden=active;
  for(const link of doc.querySelectorAll('.tabs a')){
   const selected=active?link.hash==='#theremin':link.hash===(location.hash||'#perform');
   link.classList.toggle('active',selected);
   if(selected)link.setAttribute('aria-current','page');else link.removeAttribute('aria-current');
  }
  if(active&&!previous){
   // Release only this web client's held keys. FDC and other MIDI sources
   // keep their own state; selecting a page never arms a sensor or sends STOP.
   const release=doc.getElementById('release');if(release&&!release.disabled)release.click();
  }
  previous=active;
 }
 window.addEventListener('hashchange',render);render();
}
