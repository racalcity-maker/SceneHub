use crate::http_proto::{decode_chunked_body, http_parts, is_chunked, read_http_response};
use serde::Serialize;
use serde_json::Value;
use std::collections::HashSet;
use std::io::Write;
use std::net::{Ipv4Addr, SocketAddr, SocketAddrV4, TcpStream};
use std::process::Command;
use std::sync::{
    atomic::{AtomicUsize, Ordering},
    mpsc, Arc,
};
use std::time::Duration;

const SCENEHUB_PRODUCT_ID: &str = "scenehub-controller";
const DEFAULT_PORT: u16 = 80;
const CONNECT_TIMEOUT_MS: u64 = 250;
const READ_TIMEOUT_MS: u64 = 900;
const MAX_WORKERS: usize = 32;

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DiscoveredController {
    pub base_url: String,
    pub device_id: String,
    pub device_name: String,
    pub hostname: Option<String>,
    pub firmware_version: String,
    pub api_version: i64,
    pub source: String,
}

#[derive(Debug, Clone)]
struct LocalInterface {
    ipv4: Ipv4Addr,
}

#[derive(Debug)]
struct MetaProbe {
    device_id: String,
    device_name: String,
    hostname: Option<String>,
    firmware_version: String,
    api_version: i64,
}

#[tauri::command]
pub async fn discover_controllers() -> Result<Vec<DiscoveredController>, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let interfaces = load_local_interfaces()?;
        let candidates = build_candidates(&interfaces);
        let results = scan_candidates(candidates);
        Ok(results)
    })
    .await
    .map_err(|error| format!("controller discovery task failed: {error}"))?
}

fn load_local_interfaces() -> Result<Vec<LocalInterface>, String> {
    #[cfg(target_os = "windows")]
    {
        load_local_interfaces_windows()
    }
    #[cfg(not(target_os = "windows"))]
    {
        Err("controller discovery is not implemented for this OS yet".to_string())
    }
}

#[cfg(target_os = "windows")]
fn load_local_interfaces_windows() -> Result<Vec<LocalInterface>, String> {
    let output = Command::new("powershell")
        .args(["-NoProfile", "-Command", "ipconfig"])
        .output()
        .map_err(|error| format!("failed to query network interfaces: {error}"))?;

    if !output.status.success() {
        return Err(format!(
            "failed to query network interfaces: {}",
            String::from_utf8_lossy(&output.stderr).trim()
        ));
    }

    let stdout = String::from_utf8_lossy(&output.stdout).to_string();
    if stdout.trim().is_empty() {
        return Ok(Vec::new());
    }

    let mut interfaces = Vec::new();
    for token in stdout.split_whitespace() {
        let candidate = token
            .trim_matches(|char: char| !(char.is_ascii_digit() || char == '.'));

        if let Ok(ipv4) = candidate.parse::<Ipv4Addr>() {
            if is_private_lan(ipv4) {
                interfaces.push(LocalInterface { ipv4 });
            }
        }
    }

    interfaces.sort_by_key(|interface| ipv4_to_u32(interface.ipv4));
    interfaces.dedup_by_key(|interface| interface.ipv4);

    Ok(interfaces)
}

fn is_private_lan(ip: Ipv4Addr) -> bool {
    let [a, b, ..] = ip.octets();
    matches!(
        (a, b),
        (10, _)
            | (172, 16..=31)
            | (192, 168)
    )
}

fn build_candidates(interfaces: &[LocalInterface]) -> Vec<Ipv4Addr> {
    let mut seen = HashSet::new();
    let mut candidates = Vec::new();

    for interface in interfaces {
        let own_ip = interface.ipv4;
        let (network, host_count) = subnet_range(interface.ipv4, 24);

        for host_offset in 1..host_count.saturating_sub(1) {
            let candidate = u32_to_ipv4(network + host_offset);
            if candidate == own_ip {
                continue;
            }
            if seen.insert(candidate) {
                candidates.push(candidate);
            }
        }
    }

    candidates
}

fn subnet_range(ip: Ipv4Addr, prefix_length: u8) -> (u32, u32) {
    let ip_u32 = ipv4_to_u32(ip);
    let mask = if prefix_length == 0 {
        0
    } else {
        u32::MAX << (32 - prefix_length)
    };
    let network = ip_u32 & mask;
    let host_count = if prefix_length >= 31 {
        2
    } else {
        1u32 << (32 - prefix_length)
    };
    (network, host_count)
}

fn ipv4_to_u32(ip: Ipv4Addr) -> u32 {
    u32::from_be_bytes(ip.octets())
}

fn u32_to_ipv4(value: u32) -> Ipv4Addr {
    Ipv4Addr::from(value.to_be_bytes())
}

fn scan_candidates(candidates: Vec<Ipv4Addr>) -> Vec<DiscoveredController> {
    if candidates.is_empty() {
        return Vec::new();
    }

    let worker_count = candidates.len().min(MAX_WORKERS).max(1);
    let candidates = Arc::new(candidates);
    let cursor = Arc::new(AtomicUsize::new(0));
    let (tx, rx) = mpsc::channel();

    let mut workers = Vec::new();
    for _ in 0..worker_count {
        let tx = tx.clone();
        let candidates = Arc::clone(&candidates);
        let cursor = Arc::clone(&cursor);

        workers.push(std::thread::spawn(move || loop {
            let index = cursor.fetch_add(1, Ordering::Relaxed);
            if index >= candidates.len() {
                break;
            }

            if let Some(controller) = probe_controller(candidates[index]) {
                let _ = tx.send(controller);
            }
        }));
    }

    drop(tx);

    for worker in workers {
        let _ = worker.join();
    }

    let mut discovered = Vec::new();
    for controller in rx.try_iter() {
        discovered.push(controller);
    }

    discovered.sort_by(|left, right| left.base_url.cmp(&right.base_url));
    discovered
}

fn probe_controller(ip: Ipv4Addr) -> Option<DiscoveredController> {
    let meta = fetch_meta(ip, DEFAULT_PORT)?;
    Some(DiscoveredController {
        base_url: format!("http://{}", ip),
        device_id: meta.device_id,
        device_name: meta.device_name,
        hostname: meta.hostname,
        firmware_version: meta.firmware_version,
        api_version: meta.api_version,
        source: "subnet_scan".to_string(),
    })
}

fn fetch_meta(ip: Ipv4Addr, port: u16) -> Option<MetaProbe> {
    let address = SocketAddr::V4(SocketAddrV4::new(ip, port));
    let mut stream = TcpStream::connect_timeout(
        &address,
        Duration::from_millis(CONNECT_TIMEOUT_MS),
    )
    .ok()?;
    stream
        .set_read_timeout(Some(Duration::from_millis(READ_TIMEOUT_MS)))
        .ok()?;
    stream
        .set_write_timeout(Some(Duration::from_millis(READ_TIMEOUT_MS)))
        .ok()?;

    let request = format!(
        "GET /api/meta HTTP/1.1\r\nHost: {}\r\nConnection: close\r\n\r\n",
        ip
    );
    stream.write_all(request.as_bytes()).ok()?;
    stream.flush().ok()?;

    let response = read_http_response(&mut stream).ok()?;
    let (headers, body) = http_parts(&response)?;
    let json: Value = if is_chunked(headers) {
        serde_json::from_slice(&decode_chunked_body(body).ok()?).ok()?
    } else {
        serde_json::from_slice(body).ok()?
    };

    if json.get("product_id").and_then(Value::as_str) != Some(SCENEHUB_PRODUCT_ID) {
        return None;
    }

    Some(MetaProbe {
        device_id: json.get("device_id")?.as_str()?.to_string(),
        device_name: json.get("device_name")?.as_str()?.to_string(),
        hostname: json
            .get("hostname")
            .and_then(Value::as_str)
            .map(|value| value.to_string()),
        firmware_version: json.get("firmware_version")?.as_str()?.to_string(),
        api_version: json.get("api_version")?.as_i64()?,
    })
}
