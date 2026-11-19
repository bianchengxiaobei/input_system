export type DiagnosticSeverity = "error" | "warning" | "info";

export interface Diagnostic {
  severity: DiagnosticSeverity;
  code: string;
  message: string;
  jsonPointer?: string;
  line?: number;
  column?: number;
}

export interface HelloResult {
  serverVersion: string;
  protocolVersion: string;
  tokenValid: boolean;
  capabilities: string[];
}

export interface OpenResult {
  path: string;
  revision: string;
  json: string;
  format: "json" | "insysb";
  readOnly: boolean;
  schemaVersion?: number;
}

export interface SaveResult {
  path: string;
  revision: string;
  changed: boolean;
}

export interface BakeResult {
  path: string;
  revision: string;
  bytes: number;
  changed: boolean;
}

export class RpcError extends Error {
  readonly code: number;
  readonly data: unknown;

  constructor(code: number, message: string, data?: unknown) {
    super(message);
    this.name = "RpcError";
    this.code = code;
    this.data = data;
  }
}

interface Pending {
  resolve: (value: unknown) => void;
  reject: (reason: unknown) => void;
}

interface RpcResponse {
  jsonrpc: "2.0";
  id?: number | string | null;
  result?: unknown;
  error?: {
    code: number;
    message: string;
    data?: unknown;
  };
}

export class EditorClient {
  onDisconnect?: () => void;

  private socket: WebSocket | null = null;
  private nextId = 1;
  private pending = new Map<number, Pending>();

  constructor(private readonly token: string) {}

  async connect(): Promise<HelloResult> {
    if (!this.token) {
      throw new Error("missing editor token; append ?token=... to the URL");
    }

    const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
    const url = `${protocol}//${window.location.host}/ws?token=${encodeURIComponent(this.token)}`;
    const socket = new WebSocket(url);
    this.socket = socket;

    await new Promise<void>((resolve, reject) => {
      let settled = false;
      const fail = (error: Error) => {
        if (settled) {
          return;
        }
        settled = true;
        reject(error);
      };

      socket.onopen = () => {
        settled = true;
        resolve();
      };
      socket.onerror = () => {
        fail(new Error("failed to connect to the editor server"));
      };
      socket.onclose = () => {
        fail(new Error("editor server closed the connection"));
        this.rejectPending(new Error("editor server disconnected"));
        this.onDisconnect?.();
      };
      socket.onmessage = (event) => this.handleMessage(event.data);
    });

    const hello = await this.request<HelloResult>("session.hello", {
      protocolVersion: "1",
      client: "web",
    });
    if (!hello.tokenValid) {
      throw new Error("editor server rejected the token");
    }
    return hello;
  }

  request<T>(method: string, params?: Record<string, unknown>): Promise<T> {
    const socket = this.socket;
    if (!socket || socket.readyState !== WebSocket.OPEN) {
      return Promise.reject(new Error("editor server is not connected"));
    }

    const id = this.nextId++;
    const message = JSON.stringify({
      jsonrpc: "2.0",
      id,
      method,
      params: params ?? {},
    });

    return new Promise<T>((resolve, reject) => {
      this.pending.set(id, {
        resolve: resolve as (value: unknown) => void,
        reject,
      });
      socket.send(message);
    });
  }

  close(): void {
    const socket = this.socket;
    this.socket = null;
    if (socket) {
      socket.close();
    }
    this.rejectPending(new Error("editor client closed"));
  }

  private handleMessage(data: unknown): void {
    if (typeof data !== "string") {
      return;
    }

    let response: RpcResponse;
    try {
      response = JSON.parse(data) as RpcResponse;
    } catch {
      return;
    }

    if (typeof response.id !== "number") {
      return;
    }
    const pending = this.pending.get(response.id);
    if (!pending) {
      return;
    }
    this.pending.delete(response.id);

    if (response.error) {
      pending.reject(new RpcError(response.error.code, response.error.message, response.error.data));
      return;
    }
    pending.resolve(response.result);
  }

  private rejectPending(error: Error): void {
    for (const pending of this.pending.values()) {
      pending.reject(error);
    }
    this.pending.clear();
  }
}
