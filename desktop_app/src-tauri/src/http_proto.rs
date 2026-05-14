use std::io::Read;
use std::net::TcpStream;

pub fn read_http_response(stream: &mut TcpStream) -> Result<Vec<u8>, String> {
    let mut response = Vec::new();
    let mut buffer = [0u8; 2048];

    loop {
        let read = stream
            .read(&mut buffer)
            .map_err(|error| format!("response read failed: {error}"))?;

        if read == 0 {
            break;
        }

        response.extend_from_slice(&buffer[..read]);

        if is_complete_http_response(&response)? {
            break;
        }
    }

    if response.is_empty() {
        return Err("empty HTTP response".to_string());
    }

    Ok(response)
}

pub fn http_parts(response: &[u8]) -> Option<(&[u8], &[u8])> {
    let separator = b"\r\n\r\n";
    response
        .windows(separator.len())
        .position(|window| window == separator)
        .map(|index| (&response[..index], &response[index + separator.len()..]))
}

pub fn header_value(headers: &[u8], header_name: &str) -> Option<String> {
    let wanted = header_name.to_ascii_lowercase();
    let header_text = String::from_utf8_lossy(headers);

    for line in header_text.lines().skip(1) {
        let (name, value) = line.split_once(':')?;
        if name.trim().eq_ignore_ascii_case(&wanted) {
            return Some(value.trim().to_string());
        }
    }

    None
}

pub fn is_chunked(headers: &[u8]) -> bool {
    header_value(headers, "transfer-encoding")
        .map(|value| value.eq_ignore_ascii_case("chunked"))
        .unwrap_or(false)
}

pub fn decode_chunked_body(body: &[u8]) -> Result<Vec<u8>, String> {
    let mut cursor = 0usize;
    let mut decoded = Vec::new();

    while cursor < body.len() {
        let size_line_end =
            find_crlf(body, cursor).ok_or_else(|| "invalid chunk header".to_string())?;
        let size_hex = String::from_utf8_lossy(&body[cursor..size_line_end]);
        let size = usize::from_str_radix(size_hex.trim(), 16)
            .map_err(|_| "invalid chunk size".to_string())?;
        cursor = size_line_end + 2;

        if size == 0 {
            break;
        }

        let chunk_end = cursor + size;
        if chunk_end > body.len() {
            return Err("truncated chunked body".to_string());
        }

        decoded.extend_from_slice(&body[cursor..chunk_end]);
        cursor = chunk_end;

        if body.get(cursor..cursor + 2) != Some(b"\r\n") {
            return Err("invalid chunk terminator".to_string());
        }
        cursor += 2;
    }

    Ok(decoded)
}

pub fn parse_status_code(status_line: &str) -> Result<u16, String> {
    let raw_status = status_line
        .split_whitespace()
        .nth(1)
        .ok_or_else(|| "missing HTTP status code".to_string())?;
    raw_status
        .parse::<u16>()
        .map_err(|_| "invalid HTTP status code".to_string())
}

fn is_complete_http_response(response: &[u8]) -> Result<bool, String> {
    let Some((headers, body)) = http_parts(response) else {
        return Ok(false);
    };

    if is_chunked(headers) {
        return Ok(has_complete_chunked_body(body));
    }

    if let Some(content_length) = header_value(headers, "content-length") {
        let expected = content_length
            .parse::<usize>()
            .map_err(|_| "invalid content-length header".to_string())?;
        return Ok(body.len() >= expected);
    }

    Ok(false)
}

fn has_complete_chunked_body(body: &[u8]) -> bool {
    let mut cursor = 0usize;

    while cursor < body.len() {
        let Some(size_line_end) = find_crlf(body, cursor) else {
            return false;
        };

        let size_hex = String::from_utf8_lossy(&body[cursor..size_line_end]);
        let Ok(size) = usize::from_str_radix(size_hex.trim(), 16) else {
            return false;
        };
        cursor = size_line_end + 2;

        if size == 0 {
            return body.get(cursor..cursor + 2) == Some(b"\r\n");
        }

        let chunk_end = cursor + size;
        if chunk_end + 2 > body.len() {
            return false;
        }

        if body.get(chunk_end..chunk_end + 2) != Some(b"\r\n") {
            return false;
        }

        cursor = chunk_end + 2;
    }

    false
}

fn find_crlf(body: &[u8], start: usize) -> Option<usize> {
    body.get(start..)?
        .windows(2)
        .position(|window| window == b"\r\n")
        .map(|offset| start + offset)
}
