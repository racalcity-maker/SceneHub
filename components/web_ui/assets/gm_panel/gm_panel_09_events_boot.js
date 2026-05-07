// GM panel source part. Edit this file, then rebuild gm_panel.js.
function setGMStatus(text,cls){
setStatus(text,cls==='gm-bad'?'state-fault':(cls==='gm-ok'?'state-ok':'state-unknown'));
}

document.getElementById('gm_nav').onclick=async e=>{
const btn=e.target.closest('.nav-btn');
if(!btn)return;
const view=btn.dataset.view||'dashboard';
if(!canOpenView(view))return;
if(view!==currentView&&!confirmDiscardEditorChanges())return;
currentView=view;
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

const gmRightSidebar=document.getElementById('gm_right_sidebar');
if(gmRightSidebar){
gmRightSidebar.onclick=async e=>{
if(await gmHandleActionClick(e))return;
}
;
}

initGMEditorEventHandlers();

document.getElementById('gm_refresh').onclick=()=>{
if(!confirmDiscardEditorChanges())return;
clearProfileDirty();
clearScenarioDirty();
clearQuestDeviceDirty();
clearTransientFieldDirty();
loadGM();
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
gmAdminHome.onclick=()=>{
clearProfileDirty();
clearScenarioDirty();
clearQuestDeviceDirty();
clearTransientFieldDirty();
}
;
}

window.addEventListener('beforeunload',e=>{
if(!hasUnsavedEditorChanges())return;e.preventDefault();e.returnValue='';}
);

window.__sessionRolePromise=loadGMSession();

window.__sessionRolePromise.then(()=>loadGM());

function gmPollActiveRoomRuntimeVisible(){
if(document.hidden)return;
pollActiveRoomRuntime();
}

function gmPollStateSnapshotVisible(){
if(document.hidden)return;
pollGMStateSnapshot();
}

function gmUpdateVisibleRoomClocksVisible(){
if(document.hidden)return;
updateVisibleRoomClocks();
}

document.addEventListener('visibilitychange',()=>{
if(document.hidden)return;
updateVisibleRoomClocks();
pollActiveRoomRuntime();
pollGMStateSnapshot();
});

setInterval(gmPollActiveRoomRuntimeVisible,1000);
setInterval(gmPollStateSnapshotVisible,10000);
setInterval(gmUpdateVisibleRoomClocksVisible,250);
