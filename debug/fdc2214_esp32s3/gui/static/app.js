'use strict';
const colors=['#2d63c7','#188b8a','#8656bd','#bf7c26'];
const $=id=>document.getElementById(id);
const fmt=value=>value===null||value===undefined?'—':Number(value).toLocaleString('en-US');
const rate=value=>value===null||value===undefined?'—':Number(value).toFixed(1);
let current=null, frozen=false, busy=false, windowSeconds=30, serverError='', previousSession=null;
const grid=$('channels'), table=$('device-ranges');
for(let i=0;i<4;i++){
  const card=document.createElement('section');card.className='channel';card.style.setProperty('--channel',colors[i]);card.setAttribute('aria-label',`CH${i} 原始数据`);
  card.innerHTML=`<div class="channel-header"><span class="channel-name"><i></i>CH${i}</span><span class="badge" id="badge-${i}">等待读数</span></div><div class="reading-row"><strong class="reading" id="raw-${i}">—</strong><span class="raw-label">原始码</span></div><canvas class="plot" id="plot-${i}" aria-label="CH${i} 原始码变化曲线"></canvas><div class="observed"><div><span>观测最小</span><strong id="min-${i}">—</strong></div><div><span>观测最大</span><strong id="max-${i}">—</strong></div><div><span>观测跨度</span><strong id="span-${i}">—</strong></div></div>`;
  grid.appendChild(card);
  const row=document.createElement('tr');row.style.setProperty('--channel',colors[i]);row.innerHTML=`<td class="table-channel"><i></i>CH${i}</td><td id="valid-${i}">—</td><td id="errors-${i}">—</td><td id="device-min-${i}">—</td><td id="device-max-${i}">—</td><td id="device-span-${i}">—</td>`;table.appendChild(row);
}
async function api(path,body){
  const options=body===undefined?{}:{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)};
  const response=await fetch(path,options);const data=await response.json();
  if(!response.ok)throw new Error(data.error||`请求失败 (${response.status})`);return data;
}
async function ports(){
  try{
    const data=await api('/api/ports'),select=$('port'), selected=select.value;select.replaceChildren();
    for(const port of data.ports){const option=document.createElement('option');option.value=port.path;option.textContent=port.label||port.path;option.title=port.path;select.appendChild(option);}
    if(!data.ports.length){const empty=document.createElement('option');empty.value='';empty.textContent='未发现 USB 串口';select.appendChild(empty);}
    if([...select.options].some(o=>o.value===selected))select.value=selected;
    else if(current?.port&&[...select.options].some(o=>o.value===current.port))select.value=current.port;
    $('connect').disabled=busy||(!current?.connected&&!select.value);
  }catch(error){showError(error.message);}
}
function showError(message){$('error').hidden=!message;$('error').textContent=message||'';}
function drawPlot(index,history){
  const canvas=$(`plot-${index}`),box=canvas.getBoundingClientRect(),dpr=window.devicePixelRatio||1;
  if(!box.width)return;
  if(canvas.width!==Math.round(box.width*dpr)||canvas.height!==Math.round(box.height*dpr)){canvas.width=Math.round(box.width*dpr);canvas.height=Math.round(box.height*dpr);}
  const ctx=canvas.getContext('2d');ctx.setTransform(dpr,0,0,dpr,0,0);ctx.clearRect(0,0,box.width,box.height);
  const left=83,right=18,top=14,bottom=26,w=box.width-left-right,h=box.height-top-bottom;
  ctx.font='10px "DejaVu Sans Mono",monospace';ctx.lineWidth=1;
  const last=history.length?history[history.length-1].time_ms:0,start=last-windowSeconds*1000;
  const points=history.filter(p=>p.time_ms>=start&&Number.isFinite(p.raw[index]));
  if(!points.length){ctx.fillStyle='#8493a7';ctx.textAlign='center';ctx.fillText('等待原始数据',left+w/2,top+h/2);return;}
  let lo=Math.min(...points.map(p=>p.raw[index])),hi=Math.max(...points.map(p=>p.raw[index]));
  const margin=Math.max((hi-lo)*.16,8);lo-=margin;hi+=margin;
  for(let j=0;j<3;j++){
    const y=top+h*j/2;ctx.strokeStyle='#e7edf5';ctx.beginPath();ctx.moveTo(left,y+.5);ctx.lineTo(left+w,y+.5);ctx.stroke();
    ctx.fillStyle='#7b8ba1';ctx.textAlign='right';ctx.fillText(fmt(Math.round(hi-(hi-lo)*j/2)),left-9,y+3);
  }
  const x=p=>left+w*(p.time_ms-start)/(windowSeconds*1000),y=p=>top+h*(hi-p.raw[index])/(hi-lo);
  ctx.save();ctx.beginPath();ctx.rect(left,top,w,h);ctx.clip();ctx.beginPath();
  for(let j=0;j<points.length;j++){const p=points[j];if(j===0)ctx.moveTo(x(p),y(p));else ctx.lineTo(x(p),y(p));}
  ctx.strokeStyle=colors[index];ctx.lineWidth=1.6;ctx.stroke();
  for(const p of points){if(p.flags[index]||!(p.valid_mask&(1<<index))){ctx.beginPath();ctx.arc(x(p),y(p),1.8,0,Math.PI*2);ctx.fillStyle='#c48a36';ctx.fill();}}
  const final=points[points.length-1];ctx.beginPath();ctx.arc(x(final),y(final),3,0,Math.PI*2);ctx.fillStyle=colors[index];ctx.fill();ctx.restore();
  ctx.fillStyle='#8a99ab';ctx.textAlign='left';ctx.fillText(`−${windowSeconds} 秒`,left,box.height-7);ctx.textAlign='center';ctx.fillText(`−${windowSeconds/2} 秒`,left+w/2,box.height-7);ctx.textAlign='right';ctx.fillText('现在',left+w,box.height-7);
}
function render(data){
  if(previousSession!==null&&data.session!==previousSession){frozen=false;document.body.classList.remove('paused');$('freeze').textContent='暂停显示';}
  previousSession=data.session;
  const hasData=data.channels?.some(c=>c.raw!==null&&c.raw!==undefined),stale=hasData&&(data.sample_age_ms===null||data.sample_age_ms>2000);
  const connection=$('connection');connection.className='connection'+(data.connected?(stale?' stale':' online'):'');connection.querySelector('span').textContent=data.connected?'串口已连接':'串口未连接';
  const connect=$('connect');connect.textContent=busy?'处理中…':data.connected?'断开连接':'连接设备';connect.classList.toggle('connected',data.connected);connect.disabled=busy||(!data.connected&&!$('port').value);
  showError(serverError||data.error||'');
  const live=$('live');live.className='live'+(data.connected&&hasData?(stale?' stale':' online'):'');live.lastChild.textContent=frozen?' 显示已暂停':data.connected?(hasData?(stale?' 数据已停止更新':' 数据持续更新'):' 等待设备数据'):' 等待连接设备';
  $('visible-rate').textContent=rate(data.visible_rate_hz);$('device-rate').textContent=rate(data.device_rate_hz);$('sample-counter').textContent=`已接收 ${fmt(data.sample_count||0)} 帧`;
  $('freeze').disabled=!hasData;$('clear').disabled=!hasData;$('export').disabled=!(data.history?.length);
  $('updated').textContent=hasData?`最近数据：${data.sample_age_ms===null?'未知':(data.sample_age_ms/1000).toFixed(1)+' 秒前'}`:'尚未接收数据';
  const bad=(data.channels||[]).filter(c=>c.raw!==null&&(!c.valid||c.flags));
  const notice=$('notice');notice.className='notice'+(hasData&&bad.length?' warning':'');
  $('notice-text').textContent=!data.connected?'选择 USB 串口并连接，查看四路实时原始数据。':!hasData?'已连接，等待 ESP32 输出完整的四通道数据…':stale?'保留的是最后收到的数据，请检查串口与设备供电。':bad.length?`${bad.map(c=>'CH'+c.index).join('、')} 有告警或无效标记。原始值仍会显示；设备有效范围不包含这些样本。`:'四路最近样本均有效。可以移动手部或天线，观察原始码变化。';
  if(frozen)return;
  for(const c of data.channels||[]){
    const i=c.index;$(`raw-${i}`).textContent=fmt(c.raw);$(`min-${i}`).textContent=fmt(c.observed_min);$(`max-${i}`).textContent=fmt(c.observed_max);$(`span-${i}`).textContent=fmt(c.observed_span);
    const badge=$(`badge-${i}`);let label='等待读数',type='';
    if(c.raw!==null&&c.raw!==undefined){if(c.flags&2){label='看门狗告警';type='bad';}else if(c.flags&1){label='幅度告警';type='warning';}else if(!c.valid){label='无效样本';type='bad';}else{label='有效';type='good';}}
    badge.textContent=label;badge.className='badge '+type;badge.title=`DATA 标志：0x${Number(c.flags||0).toString(16).padStart(2,'0')}`;
    $(`valid-${i}`).textContent=fmt(c.valid_count);$(`errors-${i}`).textContent=fmt(c.error_count);$(`device-min-${i}`).textContent=fmt(c.device_min);$(`device-max-${i}`).textContent=fmt(c.device_max);$(`device-span-${i}`).textContent=fmt(c.device_span);
    drawPlot(i,data.history||[]);
  }
  $('events').textContent=(data.events||[]).join('\n')||'尚无设备日志';
}
async function poll(){
  try{current=await api('/api/state');serverError='';render(current);}catch(error){serverError='本机服务连接中断，请确认监测程序仍在运行。';showError(serverError);$('live').className='live stale';$('live').lastChild.textContent=' 服务未连接';}
  setTimeout(poll,250);
}
$('refresh').addEventListener('click',ports);
$('connect').addEventListener('click',async()=>{if(busy)return;busy=true;if(current)render(current);try{await api(current?.connected?'/api/disconnect':'/api/connect',current?.connected?{}:{port:$('port').value});current=await api('/api/state');showError('');}catch(error){showError(error.message);}finally{busy=false;if(current)render(current);}});
$('freeze').addEventListener('click',()=>{frozen=!frozen;document.body.classList.toggle('paused',frozen);$('freeze').textContent=frozen?'恢复显示':'暂停显示';if(current)render(current);});
$('clear').addEventListener('click',async()=>{try{await api('/api/clear',{});frozen=false;document.body.classList.remove('paused');$('freeze').textContent='暂停显示';current=await api('/api/state');render(current);}catch(error){showError(error.message);}});
$('window').addEventListener('change',()=>{windowSeconds=Number($('window').value);if(current&&!frozen)render(current);});
$('export').addEventListener('click',()=>{window.location.assign('/api/export.csv');});
window.addEventListener('resize',()=>{if(current&&!frozen)render(current);});
for(let i=0;i<4;i++)drawPlot(i,[]);
ports();poll();
