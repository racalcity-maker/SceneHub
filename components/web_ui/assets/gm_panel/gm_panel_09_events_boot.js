// GM panel source part. Edit this file, then rebuild gm_panel.js.
const GM_WS_RECONNECT_MS=3000;
const GM_WS_INVALIDATION_FLUSH_MS=450;
const GM_RUNTIME_HTTP_FALLBACK_MS=8000;
const GM_STATE_SNAPSHOT_POLL_MS=20000;
const GM_WS_POLL_SUPPRESS_MS=30000;
let gmWsSocket=null;
let gmWsReconnectTimer=0;
let gmWsFlushTimer=0;
let gmWsVersionsIgnoreUntilMs=0;
let gmWsLastMessageAt=0;
const gmWsPendingSlices=new Map();

function gmWsUrl(){
const proto=window.location.protocol==='https:'?'wss:':'ws:';
return `${proto}//${window.location.host}/api/ws`;
}

function gmWsScheduleReconnect(){
if(gmWsReconnectTimer)return;
gmWsReconnectTimer=window.setTimeout(()=>{
gmWsReconnectTimer=0;
gmInitWebSocket();
}
,GM_WS_RECONNECT_MS);
}

function gmWsHealthy(){
return !!(gmWsSocket&&
gmWsSocket.readyState===WebSocket.OPEN&&
gmWsLastMessageAt>0&&
(Date.now()-gmWsLastMessageAt)<GM_WS_POLL_SUPPRESS_MS);
}

function gmWsQueueInvalidation(slice){
if(!slice||typeof slice!=='object')return;
const key=`${slice.slice||''}:${slice.target_id||''}`;
if(!key||key===':')return;
gmWsPendingSlices.set(key,{slice:slice.slice||'',target_id:slice.target_id||'',scope:slice.scope||'',generation:Number(slice.generation)||0,reason:slice.reason||''});
if(gmWsFlushTimer)return;
gmWsFlushTimer=window.setTimeout(async()=>{
const slices=Array.from(gmWsPendingSlices.values());
gmWsPendingSlices.clear();
gmWsFlushTimer=0;
gmStatInc('ws.invalidate_flush');
gmStatInc('ws.invalidate_slice',slices.length);
try{
await refreshGMByInvalidationSlices(slices);
}
catch(err){
console.error('GM WS invalidation refresh failed',slices,err);
setGMStatus(`WS refresh failed: ${compactText(err&&err.message||'unknown error',52)}`,'gm-bad');
}
}
,GM_WS_INVALIDATION_FLUSH_MS);
}

async function gmWsHandleVersionsChanged(payload){
if(!payload||Date.now()<gmWsVersionsIgnoreUntilMs)return;
await refreshGMByVersions({
rooms:Number(payload.rooms)||0,
devices:Number(payload.devices)||0,
scenarios:Number(payload.scenarios)||0,
profiles:Number(payload.profiles)||0,
ingest:Number(payload.ingest)||0,
session:Number(payload.session)||0,
static_generation:Number(payload.static)||0,
runtime_generation:Number(payload.runtime)||0
});
}

function gmWsHandleEnvelope(message){
if(!message||typeof message!=='object')return;
gmWsLastMessageAt=Date.now();
const type=String(message.type||'');
const payload=message.payload&&typeof message.payload==='object'?message.payload:null;
if(type==='gm.invalidate'&&payload){
gmWsVersionsIgnoreUntilMs=Date.now()+500;
gmWsQueueInvalidation({
slice:String(payload.slice||''),
target_id:String(payload.target_id||''),
scope:String(payload.scope||''),
generation:Number(payload.generation)||0,
reason:String(payload.reason||'')
});
return;
}
if(type==='gm.resync.required'&&payload){
gmWsVersionsIgnoreUntilMs=Date.now()+500;
refreshGMByInvalidationSlices([{slice:'full.snapshot',target_id:String(payload.target_id||''),scope:'recovery',generation:Number(payload.generation)||0,reason:String(payload.reason||'resync_required')}]).catch(()=>{
console.error('GM WS resync failed',payload);
setGMStatus('WS resync failed','gm-bad');
});
return;
}
if(type==='gm.versions.changed'&&payload){
gmWsHandleVersionsChanged(payload).catch(()=>{});
}
}

function gmInitWebSocket(){
if(gmWsSocket&&(
gmWsSocket.readyState===WebSocket.OPEN||
gmWsSocket.readyState===WebSocket.CONNECTING
))return;
if(typeof WebSocket!=='function')return;
try{
const socket=new WebSocket(gmWsUrl());
gmWsSocket=socket;
socket.onopen=()=>{
gmWsLastMessageAt=Date.now();
socket.send(JSON.stringify({type:'subscribe'}));
};
socket.onmessage=event=>{
let message=null;
try{
message=JSON.parse(event&&event.data||'');
}
catch(err){
return;
}
gmWsHandleEnvelope(message);
};
socket.onclose=()=>{
if(gmWsSocket===socket)gmWsSocket=null;
gmWsLastMessageAt=0;
gmWsScheduleReconnect();
};
socket.onerror=()=>{
try{socket.close();}catch(err){}
};
}
catch(err){
gmWsScheduleReconnect();
}
}

function setGMStatus(text,cls){
setStatus(text,cls==='gm-bad'?'state-fault':(cls==='gm-ok'?'state-ok':'state-unknown'));
}

document.addEventListener('pointerdown',e=>{
const target=e.target;
if(!target||!target.closest)return;
if(target.closest('#gm_content')||target.closest('#gm_right_sidebar')){
gmBeginInteraction();
}
}
,true);

document.addEventListener('pointerup',()=>{
const wasInteracting=gmInteractionActive;
gmEndInteraction();
if(wasInteracting&&gmAutoRenderDeferred&&!shouldDeferAutoRender()){
gmFlushDeferredRender();
}
}
,true);

document.addEventListener('pointercancel',()=>{
const wasInteracting=gmInteractionActive;
gmEndInteraction();
if(wasInteracting&&gmAutoRenderDeferred&&!shouldDeferAutoRender()){
gmFlushDeferredRender();
}
}
,true);

window.addEventListener('blur',()=>{
gmEndInteraction();
});

document.getElementById('gm_nav').onclick=async e=>{
const btn=e.target.closest('.nav-btn');
if(!btn)return;
const view=btn.dataset.view||'rooms';
if(!canOpenView(view))return;
if(view!==currentView&&!confirmDiscardEditorChanges())return;
currentView=gmRouteToSingleRoom(view)?'room':view;
try{
await loadGMViewData(false);
}
catch(err){
setGMStatus('View data refresh failed','gm-bad');
}
render();
}
;

document.getElementById('gm_content').onclick=async e=>{
await gmHandleActionClick(e);
}
;

const gmPageSub=document.getElementById('page_sub');
if(gmPageSub){
gmPageSub.onclick=async e=>{
await gmHandleActionClick(e);
}
;
}

const gmPageTitle=document.getElementById('page_title');
if(gmPageTitle){
gmPageTitle.onclick=async e=>{
await gmHandleActionClick(e);
}
;
}

const gmRightSidebar=document.getElementById('gm_right_sidebar');
if(gmRightSidebar){
gmRightSidebar.onclick=async e=>{
if(await gmHandleActionClick(e))return;
}
;
}

window.__gmRefreshManualSidebar=async()=>{
renderRightSidebar(true);
};

initGMEditorEventHandlers();

document.getElementById('gm_refresh').onclick=()=>{
if(!confirmDiscardEditorChanges())return;
clearProfileDirty();
clearScenarioDirty();
clearQuestDeviceDirty();
clearTransientFieldDirty();
loadGMFullSnapshot();
}
;

document.getElementById('gm_logout').onclick=()=>{
if(!confirmDiscardEditorChanges())return;
clearProfileDirty();
clearScenarioDirty();
clearQuestDeviceDirty();
clearTransientFieldDirty();
fetch('/api/auth/logout',{
method:'POST'}
).finally(()=>window.location='/login');
}
;

const gmAdminHome=document.getElementById('gm_admin_home');
if(gmAdminHome){
gmAdminHome.onclick=e=>{
if(!confirmDiscardEditorChanges()){
e.preventDefault();
return;
}
clearProfileDirty();
clearScenarioDirty();
clearQuestDeviceDirty();
clearTransientFieldDirty();
window.location='/';
}
;
}

window.addEventListener('beforeunload',e=>{
if(gmWsReconnectTimer){
clearTimeout(gmWsReconnectTimer);
gmWsReconnectTimer=0;
}
if(gmWsFlushTimer){
clearTimeout(gmWsFlushTimer);
gmWsFlushTimer=0;
}
if(!hasUnsavedEditorChanges())return;e.preventDefault();e.returnValue='';}
);

window.__sessionRolePromise=loadGMSession();

window.__sessionRolePromise.then(async()=>{
try{
await loadGMFullSnapshot();
}
finally{
gmInitWebSocket();
}
});

function gmPollActiveRoomRuntimeVisible(){
if(document.hidden)return;
if(gmWsHealthy()){
gmStatInc('poll.runtime.skip_ws');
return;
}
gmStatInc('poll.runtime.run');
pollActiveRoomRuntime();
}

function gmPollStateSnapshotVisible(){
if(document.hidden)return;
if(gmWsHealthy()){
gmStatInc('poll.snapshot.skip_ws');
return;
}
gmStatInc('poll.snapshot.run');
pollGMStateSnapshot();
}

function gmUpdateVisibleRoomClocksVisible(){
if(document.hidden)return;
gmStatInc('poll.clock.run');
updateVisibleRoomClocks();
}

document.addEventListener('visibilitychange',()=>{
if(document.hidden)return;
updateVisibleRoomClocks();
gmPollActiveRoomRuntimeVisible();
gmPollStateSnapshotVisible();
});

setInterval(gmPollActiveRoomRuntimeVisible,GM_RUNTIME_HTTP_FALLBACK_MS);
setInterval(gmPollStateSnapshotVisible,GM_STATE_SNAPSHOT_POLL_MS);
setInterval(gmUpdateVisibleRoomClocksVisible,250);
