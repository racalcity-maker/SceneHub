mod controller_http;
mod discovery;
mod http_proto;

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![
            controller_http::controller_http_get,
            controller_http::controller_http_post,
            discovery::discover_controllers
        ])
        .run(tauri::generate_context!())
        .expect("error while running SceneHub Desktop");
}
