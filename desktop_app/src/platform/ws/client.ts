export class WsClient {
  private socket: WebSocket | null = null;

  connect(url: string) {
    if (this.socket && this.socket.readyState <= WebSocket.OPEN) {
      return this.socket;
    }

    this.socket = new WebSocket(url);
    return this.socket;
  }

  disconnect() {
    this.socket?.close();
    this.socket = null;
  }
}

