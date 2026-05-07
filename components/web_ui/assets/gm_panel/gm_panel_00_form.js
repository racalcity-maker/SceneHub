// GM panel source part. Edit this file, then rebuild gm_panel.js.
function formFieldId(scope,name){
return `${scope||'form'}_${name||'field'}`.replace(/[^a-zA-Z0-9_:-]/g,'_');
}

function renderFormField(field,model,scope){
field=field||{};
const name=field.name||field.field||'';
const type=field.type||'text';
const value=model&&Object.prototype.hasOwnProperty.call(model,name)?model[name]:field.default;
const dataset={field:name,scope:scope||''};
let content='';
if(type==='checkbox'){
content=uiCheckbox({label:field.checkbox_label||'',checked:!!value,disabled:!!field.disabled,dataset});
return field.label?`<div class='field-stack'><span>${esc(field.label)}</span>${content}</div>`:content;
}
if(type==='select'){
content=uiSelect({value:value!==undefined?value:'',options:field.options||[],disabled:!!field.disabled,dataset});
}
else if(type==='textarea'){
content=`<textarea ${uiDataset(dataset)}${field.disabled?' disabled':''} placeholder='${esc(field.placeholder||'')}'>${esc(value!==undefined?value:'')}</textarea>`;
}
else if(type==='json'){
const text=value===undefined||value===null?'':(typeof value==='string'?value:JSON.stringify(value));
content=`<textarea ${uiDataset(dataset)}${field.disabled?' disabled':''} placeholder='${esc(field.placeholder||'{}')}'>${esc(text)}</textarea>`;
}
else{
content=uiInput({
type:type==='number'||type==='duration_ms'?'number':'text',
value:value!==undefined?value:'',
placeholder:field.placeholder||'',
min:field.min,
max:field.max,
step:field.step||(type==='number'||type==='duration_ms'?1:undefined),
disabled:!!field.disabled,
dataset,
});
}
return uiField({label:field.label||name,content});
}

function renderFormFields(schema,model,scope){
return (Array.isArray(schema)?schema:[]).map(field=>renderFormField(field,model||{},scope||'')).join('');
}

function collectFormFields(root,schema,scope){
const out={};
(Array.isArray(schema)?schema:[]).forEach(field=>{
field=field||{};
const name=field.name||field.field||'';
if(!name)return;
const candidates=root&&root.querySelectorAll?Array.from(root.querySelectorAll('[data-field]')):[];
const el=candidates.find(item=>(item.dataset.field||'')===name&&(!scope||(item.dataset.scope||'')===scope));
if(!el)return;
if(field.type==='checkbox'){
out[name]=!!el.checked;
}
else if(field.type==='number'||field.type==='duration_ms'){
const num=Number(el.value);
out[name]=Number.isFinite(num)?num:0;
}
else if(field.type==='json'){
const raw=(el.value||'').trim();
if(!raw){
out[name]=field.default!==undefined?field.default:null;
}
else{
try{
out[name]=JSON.parse(raw);
}
catch(err){
out[name]=raw;
}
}
}
else{
out[name]=el.value;
}
});
return out;
}

function validateFormFields(model,schema){
const errors=[];
(Array.isArray(schema)?schema:[]).forEach(field=>{
field=field||{};
const name=field.name||field.field||'';
if(!name)return;
const value=model?model[name]:undefined;
if(field.required&&(value===undefined||value===null||value==='')){
errors.push(`${field.label||name} is required`);
}
if((field.type==='number'||field.type==='duration_ms')&&value!==undefined){
if(field.min!==undefined&&value<field.min)errors.push(`${field.label||name} is below minimum`);
if(field.max!==undefined&&value>field.max)errors.push(`${field.label||name} is above maximum`);
}
if(field.type==='json'&&typeof value==='string'&&value.trim()){
try{
JSON.parse(value);
}
catch(err){
errors.push(`${field.label||name} must be valid JSON`);
}
}
});
return errors;
}
