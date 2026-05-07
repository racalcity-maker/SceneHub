// GM panel source part. Edit this file, then rebuild gm_panel.js.
function esc(v){
const value=(v===undefined||v===null)?'':v;
return String(value).replace(/[&<>"']/g,m=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[m]));
}

function enc(value){
return encodeURIComponent(value);
}

function kebab(value){
return String(value||'').replace(/([a-z0-9])([A-Z])/g,'$1-$2').replace(/[_\s]+/g,'-').toLowerCase();
}

function boolAttr(name,value){
return value?` ${esc(name)}`:'';
}

function jsonAttr(value){
return esc(JSON.stringify(value===undefined?null:value));
}

function uiAttrs(attrs){
return Object.entries(attrs||{}).filter(([,value])=>value!==false&&value!==undefined&&value!==null).map(([key,value])=>value===true?esc(key):`${esc(key)}='${esc(value)}'`).join(' ');
}

function uiDataset(dataset){
const attrs={};
Object.entries(dataset||{}).forEach(([key,value])=>{
if(value===undefined||value===null)return;
attrs[`data-${kebab(key)}`]=value;
});
return uiAttrs(attrs);
}
