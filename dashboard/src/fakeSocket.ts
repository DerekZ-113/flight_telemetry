// A WebSocket stand-in for the tests (TC-022 steps 8 to 14): the test opens
// it, delivers messages and closes it by hand, so no bridge, no network and
// no real time are involved. The feed hook sees only SocketLike.
import type { SocketLike } from "./feed";

export class FakeSocket implements SocketLike {
  static instances: FakeSocket[] = [];

  onopen: WebSocket["onopen"] = null;
  onmessage: WebSocket["onmessage"] = null;
  onclose: WebSocket["onclose"] = null;
  closed = false;

  constructor(public readonly url: string) {
    FakeSocket.instances.push(this);
  }

  static reset(): void {
    FakeSocket.instances = [];
  }

  static latest(): FakeSocket {
    const socket = FakeSocket.instances[FakeSocket.instances.length - 1];
    if (socket === undefined) {
      throw new Error("no socket has been opened");
    }
    return socket;
  }

  // The handlers take `this: WebSocket`; the fake stands in for one.
  private asWebSocket(): WebSocket {
    return this as unknown as WebSocket;
  }

  open(): void {
    this.onopen?.call(this.asWebSocket(), new Event("open"));
  }

  deliver(message: unknown): void {
    const data = typeof message === "string" ? message : JSON.stringify(message);
    this.onmessage?.call(this.asWebSocket(), new MessageEvent("message", { data }));
  }

  closeFromServer(): void {
    this.onclose?.call(this.asWebSocket(), new CloseEvent("close"));
  }

  close(): void {
    this.closed = true;
  }
}

export const fakeSocketFactory = (url: string): SocketLike => new FakeSocket(url);
