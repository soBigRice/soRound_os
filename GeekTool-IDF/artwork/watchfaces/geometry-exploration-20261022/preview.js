/* 只重画当前输入，没有后台计时器。 / Render only on input; no background timers. */
(() => {
  'use strict';
  const {designs,svg,time}=GeometryFaces;
  const board=document.querySelector('#board'),hour=document.querySelector('#hour'),minute=document.querySelector('#minute'),assist=document.querySelector('#assist');
  const groups=[['position','A / 位置','POSITION'],['area','B / 面积','AREA'],['rotation','C / 旋转','ROTATION']];
  board.innerHTML=groups.map(([key,name,en])=>`<section class="direction" data-group="${key}" aria-label="${name}"><div class="direction-head"><h2>${name}读时</h2><span>${en}</span></div><div class="faces">${designs.filter(d=>d.group===key).map(d=>`<figure data-design="${d.key}"><div class="screen"></div><figcaption><div class="caption-line"><span class="index">${d.number}</span><h3>${d.name}<small>${d.en}</small></h3></div><p class="rule">${d.rule}</p></figcaption></figure>`).join('')}</div></section>`).join('');
  function render(){
    const t=time(Number(hour.value),Number(minute.value));
    document.querySelector('#sample-time').value=t.stamp;document.querySelector('#hour-value').value=String(t.hour).padStart(2,'0');document.querySelector('#minute-value').value=String(t.minute).padStart(2,'0');
    for(const d of designs)document.querySelector(`[data-design="${d.key}"] .screen`).innerHTML=svg(d.key,t.hour,t.minute,assist.checked);
    document.querySelectorAll('[data-time]').forEach(button=>button.setAttribute('aria-pressed',String(button.dataset.time===t.stamp)));
  }
  for(const input of [hour,minute,assist])input.addEventListener('input',render);
  document.querySelectorAll('[data-time]').forEach(button=>button.addEventListener('click',()=>{[hour.value,minute.value]=button.dataset.time.split(':').map(Number);render();}));
  function focus(direction){
    board.dataset.focus=direction;
    board.querySelectorAll('[data-group]').forEach(group=>group.hidden=direction!=='all'&&group.dataset.group!==direction);
    document.querySelectorAll('button[data-direction]').forEach(button=>button.setAttribute('aria-pressed',String(button.dataset.direction===direction)));
  }
  document.querySelectorAll('button[data-direction]').forEach(button=>button.addEventListener('click',()=>focus(button.dataset.direction)));
  focus(document.body.dataset.direction);render();
})();
