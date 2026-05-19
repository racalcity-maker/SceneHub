// GM panel source part. Edit this file, then rebuild gm_panel.js.
function renderProfilesAdminView(){
setPage('Game Modes','Start presets for room scenarios');
const rooms=(gmState&&Array.isArray(gmState.rooms))?gmState.rooms:[];
if(!rooms.length)return `<div class='card empty'>No rooms available</div>`;
if(!profileEditor.room_id||!rooms.some(r=>r.room_id===profileEditor.room_id)){
profileEditor.room_id=rooms[0].room_id;
}
const roomId=profileEditor.room_id;
const profiles=roomProfiles(roomId);
const scenarios=scenarioSummariesByRoom(roomId);
const editing=profiles.find(p=>p.id===profileEditor.profile_id)||null;
const prefill=(!editing&&profileEditor.prefill&&profileEditor.prefill.room_id===roomId)?profileEditor.prefill:null;
const editorOpen=!!(profileEditor.open||editing||profileEditor.dirty);
const modeName=(editing&&editing.name)||(prefill&&prefill.name)||'';
const modeId=(editing&&editing.id)||(prefill&&prefill.id)||'';
const firstValidScenario=scenarios.find(s=>s.valid!==false)||scenarios[0]||null;
const scenarioValue=(editing&&editing.scenario_id)||(prefill&&prefill.scenario_id)||(firstValidScenario&&firstValidScenario.id)||'';
const scenarioMissing=!!(scenarioValue&&!scenarios.some(s=>s.id===scenarioValue));
const scenarioInvalid=!!(scenarios.find(s=>s.id===scenarioValue&&s.valid===false));
const scenarioOptions=scenarios.length?scenarios.map(s=>`<option value='${esc(s.id)}' ${s.id===scenarioValue?'selected':''} ${s.valid===false?'disabled':''}>${esc(s.name||s.id)}${s.valid===false?' (invalid)':''}</option>`).join('')+(scenarioMissing?`<option value='${esc(scenarioValue)}' selected>${esc(scenarioValue)} (missing)</option>`:''):`<option value=''>No scenarios</option>`;
const scenarioHelp=!scenarios.length?`<div class='empty'>Create a room scenario before saving a game mode.</div><div class='actions'>${uiButton({label:'Create scenario',action:'admin.open',dataset:{view:'scenarios','room-id':roomId}})}</div>`:(scenarioMissing?`<div class='row-meta bad-text'>Selected scenario is missing. Choose another scenario before saving.</div>`:(scenarioInvalid?`<div class='row-meta bad-text'>Selected scenario has validation errors.</div>`:''));
const minutes=Math.max(1,Math.round(((editing&&editing.duration_ms)||(prefill&&prefill.duration_ms)||3600000)/60000));
const hintPack=(editing&&editing.hint_pack_id)||(prefill&&prefill.hint_pack_id)||'';
const audioPack=(editing&&editing.audio_pack_id)||(prefill&&prefill.audio_pack_id)||'';
const enabled=!editing||editing.enabled!==false;
const selectedProfileId=roomSelectedProfileId(roomId);
const selectedProfile=profiles.find(p=>p.id===selectedProfileId)||null;
const profileRows=profiles.length?profiles.map(p=>{
const selected=p.id===selectedProfileId;
const invalid=p.valid===false;
const disabled=p.enabled===false;
return `<div class='row-card profile-row ${selected?'selected-row':''}'><div class='row-main'><div class='row-title'>${esc(p.name||p.id)} ${selected?`<span class='badge selected-badge'>selected</span>`:''} ${disabled?`<span class='badge'>disabled</span>`:''} ${invalid?`<span class='badge scenario-issue-badge error'>invalid</span>`:''}</div><div class='profile-mode-summary'><span>${esc(scenarioName(roomId,p.scenario_id))}</span><span>${esc(fmtClock(p.duration_ms))}</span></div></div>${uiActions([
uiButton({label:'Edit',action:'profile.edit',dataset:{'profile-id':p.id}}),
uiButton({label:'Select',action:'profile.select',dataset:{'profile-id':p.id},disabled:selected||invalid||disabled}),
uiButton({label:'Delete',kind:'danger',action:'profile.delete',dataset:{'profile-id':p.id},confirm:`Delete game mode ${p.id}?`}),
])}</div>`;
}).join(''):`<div class='card empty'>No game modes for this room</div>`;
const saveDisabled=!scenarios.length||scenarioMissing||scenarioInvalid;
const editorHtml=editorOpen?`<div class='card'><div class='card-head'><div><h2 class='section-title'>${editing?'Edit game mode':'New game mode'}${profileEditor.dirty?' *':''}</h2><div class='card-sub'>A game mode selects one scenario and game duration for operators.</div></div><label class='row-meta'><input id='profile_enabled' type='checkbox' ${enabled?'checked':''} style='min-width:auto'> Enabled</label></div><div class='field-grid'><label class='field-stack'><span>Mode name</span><input id='profile_name' placeholder='Garri Potter' value='${esc(modeName)}'></label><label class='field-stack'><span>Duration, min</span><input id='profile_duration' type='number' min='1' step='1' placeholder='60' value='${minutes}'></label></div><div class='form-section'><label class='field-stack'><span>Scenario</span><select id='profile_scenario' class='scenario-select'>${scenarioOptions}</select></label><div class='profile-selected-summary'><div><span>Scenario</span><strong>${esc(scenarioName(roomId,scenarioValue))}</strong></div><div><span>Duration</span><strong>${esc(fmtClock(minutes*60000))}</strong></div></div></div><details class='scenario-advanced'><summary>Advanced</summary><div class='row'><input id='profile_id' placeholder='Mode ID' value='${esc(modeId)}'></div><div class='row'><input id='profile_hint_pack' placeholder='Hint pack ID' value='${esc(hintPack)}'><input id='profile_audio_pack' placeholder='Audio pack ID' value='${esc(audioPack)}'></div></details>${scenarioHelp}<div style='height:12px'></div>${uiActions([
uiButton({label:'Save game mode',action:'profile.save',disabled:saveDisabled}),
editing?uiButton({label:'Select for room',action:'profile.select',dataset:{'profile-id':editing.id},disabled:editing.id===selectedProfileId||saveDisabled||enabled===false}):'',
])}<div id='profile_editor_status' class='row-meta'></div></div>`:`<div class='card empty'><h2 class='section-title'>Game mode editor</h2><div class='row-meta'>Select a game mode or create a new one.</div></div>`;
return `<div class='scenario-room-bar'><div><h2 class='section-title'>Room</h2><select class='scenario-select' data-profile-room-select>${rooms.map(r=>`<option value='${esc(r.room_id)}' ${r.room_id===roomId?'selected':''}>${esc(r.title||r.room_id)}</option>`).join('')}</select></div><div class='row-meta'>Selected: <strong>${esc((selectedProfile&&(selectedProfile.name||selectedProfile.id))||'none')}</strong></div></div><div class='profile-admin-layout'><section><div class='card-head'><h2 class='section-title'>Game modes</h2>${uiActions([uiButton({label:'Add game mode',action:'profile.new'})])}</div><div class='list'>${profileRows}</div></section><section>${editorHtml}</section></div>`;
}
