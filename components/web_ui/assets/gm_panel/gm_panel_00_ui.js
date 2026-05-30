function uiButton(opts){
opts=opts||{};
const classes=['ui-btn',opts.kind||'',opts.size?`ui-btn-${opts.size}`:'',opts.className||''].filter(Boolean).join(' ');
const cls=` class='${esc(classes)}'`;
const action=opts.action?` data-action='${esc(opts.action)}'`:'';
const dataset=uiDataset(opts.dataset);
const confirm=opts.confirm?` data-confirm='${esc(opts.confirm)}'`:'';
const title=opts.title?` title='${esc(opts.title)}'`:'';
const disabled=opts.disabled?' disabled':'';
const attrs=[action,dataset,confirm,title,disabled].filter(Boolean).join(' ');
return `<button${cls}${attrs?` ${attrs}`:''}>${esc(opts.label||'')}</button>`;
}

function uiIconButton(opts){
opts=opts||{};
return uiButton({...opts,kind:`icon-btn${opts.kind?` ${opts.kind}`:''}`.trim()});
}

function uiActions(buttons){
return `<div class='actions'>${(Array.isArray(buttons)?buttons:[]).join('')}</div>`;
}

function uiCard(opts){
opts=opts||{};
const cls=['card',opts.kind||'',opts.className||''].filter(Boolean).join(' ');
const dataset=opts.dataset?` ${uiDataset(opts.dataset)}`:'';
const actions=Array.isArray(opts.actions)?opts.actions.join(''):(opts.actions||'');
const header=(opts.title||opts.subtitle||opts.status||actions)?`<div class='card-head'><div>${opts.title?`<h2 class='section-title'>${esc(opts.title)}</h2>`:''}${opts.subtitle?`<div class='card-sub'>${esc(opts.subtitle)}</div>`:''}</div>${opts.status||actions?`<div class='actions'>${opts.status||''}${actions||''}</div>`:''}</div>`:'';
const footer=opts.footer?`<div class='card-footer'>${opts.footer}</div>`:'';
return `<section class='${esc(cls)}'${dataset}>${header}${opts.content||''}${footer}</section>`;
}

function uiSection(opts){
opts=opts||{};
return `<section${opts.kind?` class='${esc(opts.kind)}'`:''}>${opts.title?`<h2 class='section-title'>${esc(opts.title)}</h2>`:''}${opts.content||''}</section>`;
}

function uiField(opts){
opts=opts||{};
return `<label class='field-stack'>${opts.label?`<span>${esc(opts.label)}</span>`:''}${opts.content||''}</label>`;
}

function uiInput(opts){
opts=opts||{};
const attrs=uiAttrs({
type:opts.type||'text',
value:opts.value!==undefined?opts.value:'',
placeholder:opts.placeholder||undefined,
min:opts.min,
max:opts.max,
step:opts.step,
disabled:!!opts.disabled,
});
return `<input ${attrs}${opts.dataset?` ${uiDataset(opts.dataset)}`:''}>`;
}

function uiSelect(opts){
opts=opts||{};
const selected=String(opts.value!==undefined?opts.value:'');
const options=(Array.isArray(opts.options)?opts.options:[]).map(option=>{
const value=String(option&&typeof option==='object'?option.value:option);
const label=option&&typeof option==='object'?(option.label!==undefined?option.label:value):value;
return `<option value='${esc(value)}' ${value===selected?'selected':''}>${esc(label)}</option>`;
}).join('');
return `<select${opts.dataset?` ${uiDataset(opts.dataset)}`:''}${opts.disabled?' disabled':''}>${options}</select>`;
}

function uiCheckbox(opts){
opts=opts||{};
return `<label class='row-meta'><input type='checkbox' ${opts.checked?'checked':''}${opts.disabled?' disabled':''}${opts.dataset?` ${uiDataset(opts.dataset)}`:''} style='min-width:auto'> ${esc(opts.label||'')}</label>`;
}

function uiBadge(text,kind){
return `<span class='badge${kind?` ${esc(kind)}`:''}'>${esc(text)}</span>`;
}

function uiEmpty(text){
return `<div class='manual-empty'>${esc(text||'')}</div>`;
}

function uiDetails(opts){
opts=opts||{};
return `<details class='scenario-advanced' ${opts.open?'open':''}><summary>${esc(opts.summary||'Details')}</summary>${opts.content||''}</details>`;
}

function uiOverlayCard(opts){
opts=opts||{};
const closeAction=opts.closeAction||'';
const modalClass=['overlay-card',opts.className||''].filter(Boolean).join(' ');
const backdropAction=closeAction?` data-action='${esc(closeAction)}'`:'';
const closeButton=closeAction?uiButton({label:opts.closeLabel||'Close',action:closeAction}):'';
return `<div class='overlay-shell'${opts.dataset?` ${uiDataset(opts.dataset)}`:''}><div class='overlay-backdrop'${backdropAction}></div><section class='${esc(modalClass)}'>${opts.header!==false?`<div class='card-head'><div>${opts.title?`<h2 class='section-title'>${esc(opts.title)}</h2>`:''}${opts.subtitle?`<div class='card-sub'>${esc(opts.subtitle)}</div>`:''}</div>${closeButton?`<div class='actions'>${closeButton}</div>`:''}</div>`:''}${opts.content||''}</section></div>`;
}
