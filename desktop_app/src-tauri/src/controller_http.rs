use crate::http_proto::{
    decode_chunked_body, header_value, http_parts, is_chunked, parse_status_code,
    read_http_response,
};
use serde::Serialize;
use std::collections::HashMap;
use std::io::Write;
use std::net::{TcpStream, ToSocketAddrs};
use std::sync::{Mutex, OnceLock};
use std::time::Duration;

const CONNECT_TIMEOUT_MS: u64 = 1200;
const IO_TIMEOUT_MS: u64 = 2000;

static COOKIE_JAR: OnceLock<Mutex<HashMap<String, String>>> = OnceLock::new();

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct HttpResponsePayload {
    pub status: u16,
    pub content_type: String,
    pub body: String,
}

#[derive(Debug, Clone)]
struct ParsedBaseUrl {
    origin: String,
    authority: String,
    host: String,
    port: u16,
}

#[derive(Debug)]
struct ParsedHttpResponse {
    status: u16,
    content_type: String,
    body: Vec<u8>,
    set_cookie_headers: Vec<String>,
}

#[tauri::command]
pub fn controller_http_get(base_url: String, path: String) -> Result<HttpResponsePayload, String> {
    let response = send_request("GET", &base_url, &path, None)?;
    Ok(to_payload(response))
}

#[tauri::command]
pub fn controller_http_post(
    base_url: String,
    path: String,
    body: Option<String>,
) -> Result<HttpResponsePayload, String> {
    let response = send_request("POST", &base_url, &path, body.as_deref())?;
    Ok(to_payload(response))
}

fn to_payload(response: ParsedHttpResponse) -> HttpResponsePayload {
    HttpResponsePayload {
        status: response.status,
        content_type: response.content_type,
        body: String::from_utf8_lossy(&response.body).into_owned(),
    }
}

fn send_request(
    method: &str,
    base_url: &str,
    path: &str,
    body: Option<&str>,
) -> Result<ParsedHttpResponse, String> {
    let parsed = parse_http_base_url(base_url)?;
    let request_path = normalize_request_path(path);
    let mut addresses = format!("{}:{}", parsed.host, parsed.port)
        .to_socket_addrs()
        .map_err(|error| format!("resolve failed: {error}"))?;
    let address = addresses
        .next()
        .ok_or_else(|| "controller address did not resolve".to_string())?;
    let mut stream = TcpStream::connect_timeout(
        &address,
        Duration::from_millis(CONNECT_TIMEOUT_MS),
    )
    .map_err(|error| format!("connect failed: {error}"))?;
    stream
        .set_read_timeout(Some(Duration::from_millis(IO_TIMEOUT_MS)))
        .map_err(|error| format!("read timeout setup failed: {error}"))?;
    stream
        .set_write_timeout(Some(Duration::from_millis(IO_TIMEOUT_MS)))
        .map_err(|error| format!("write timeout setup failed: {error}"))?;

    let mut request = format!(
        "{method} {request_path} HTTP/1.1\r\nHost: {authority}\r\nAccept: application/json\r\nOrigin: {origin}\r\nReferer: {origin}/\r\nConnection: close\r\n",
        authority = parsed.authority,
        origin = parsed.origin,
    );

    if let Some(cookie) = lookup_cookie(base_url) {
        request.push_str(&format!("Cookie: {cookie}\r\n"));
    }

    if let Some(body) = body {
        request.push_str("Content-Type: application/json\r\n");
        request.push_str(&format!("Content-Length: {}\r\n", body.len()));
        request.push_str("\r\n");
        request.push_str(body);
    } else {
        request.push_str("\r\n");
    }

    stream
        .write_all(request.as_bytes())
        .map_err(|error| format!("request write failed: {error}"))?;
    stream
        .flush()
        .map_err(|error| format!("request flush failed: {error}"))?;

    let response = read_http_response(&mut stream)?;
    let parsed_response = parse_http_response(&response)?;
    apply_set_cookie_headers(base_url, &parsed_response.set_cookie_headers)?;
    Ok(parsed_response)
}

fn parse_http_base_url(base_url: &str) -> Result<ParsedBaseUrl, String> {
    let trimmed = base_url.trim().trim_end_matches('/');
    let rest = trimmed
        .strip_prefix("http://")
        .ok_or_else(|| "only plain http:// controller URLs are supported".to_string())?;
    let authority = rest
        .split('/')
        .next()
        .filter(|value| !value.is_empty())
        .ok_or_else(|| "invalid controller URL".to_string())?;

    if authority.starts_with('[') {
        return Err("IPv6 controller URLs are not supported yet".to_string());
    }

    let (host, port) = match authority.split_once(':') {
        Some((host, raw_port)) => {
            let port = raw_port
                .parse::<u16>()
                .map_err(|_| "invalid controller URL port".to_string())?;
            (host.to_string(), port)
        }
        None => (authority.to_string(), 80),
    };

    if host.is_empty() {
        return Err("invalid controller URL host".to_string());
    }

    Ok(ParsedBaseUrl {
        origin: format!("http://{authority}"),
        authority: authority.to_string(),
        host,
        port,
    })
}

fn normalize_request_path(path: &str) -> String {
    if path.starts_with('/') {
        path.to_string()
    } else {
        format!("/{path}")
    }
}

fn parse_http_response(response: &[u8]) -> Result<ParsedHttpResponse, String> {
    let (headers, body) = http_parts(response).ok_or_else(|| "invalid HTTP response".to_string())?;
    let headers_text = String::from_utf8_lossy(headers);
    let mut lines = headers_text.lines();
    let status_line = lines
        .next()
        .ok_or_else(|| "missing HTTP status line".to_string())?;
    let status = parse_status_code(status_line)?;

    let content_type = header_value(headers, "content-type").unwrap_or_default();
    let mut set_cookie_headers = Vec::new();

    for line in lines {
        if let Some((name, value)) = line.split_once(':') {
            let header_name = name.trim().to_ascii_lowercase();
            let header_value = value.trim();
            match header_name.as_str() {
                "set-cookie" => set_cookie_headers.push(header_value.to_string()),
                _ => {}
            }
        }
    }

    let decoded_body = if is_chunked(headers) {
        decode_chunked_body(body)?
    } else {
        body.to_vec()
    };

    Ok(ParsedHttpResponse {
        status,
        content_type,
        body: decoded_body,
        set_cookie_headers,
    })
}

fn apply_set_cookie_headers(base_url: &str, headers: &[String]) -> Result<(), String> {
    if headers.is_empty() {
        return Ok(());
    }

    let mut cookie = lookup_cookie(base_url).unwrap_or_default();

    for header in headers {
        if let Some(value) = header.split(';').next().map(str::trim) {
            if value.is_empty() {
                continue;
            }

            if value.ends_with("=deleted") {
                cookie.clear();
            } else {
                cookie = value.to_string();
            }
        }
    }

    let jar = COOKIE_JAR.get_or_init(|| Mutex::new(HashMap::new()));
    let mut guard = jar.lock().map_err(|_| "cookie jar lock poisoned".to_string())?;
    if cookie.is_empty() {
        guard.remove(base_url);
    } else {
        guard.insert(base_url.to_string(), cookie);
    }
    Ok(())
}

fn lookup_cookie(base_url: &str) -> Option<String> {
    let jar = COOKIE_JAR.get_or_init(|| Mutex::new(HashMap::new()));
    let guard = jar.lock().ok()?;
    guard.get(base_url).cloned()
}
