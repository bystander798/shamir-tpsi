import axios from "axios";
import { pinia } from "../stores";
import { useAuthStore } from "../stores/auth";
import { useToast } from "../services/toast";

const api = axios.create({
  baseURL: "/api",
  timeout: 15000
});

api.interceptors.request.use((config) => {
  const stored = window.localStorage.getItem("tpsi_auth_state");
  if (stored) {
    try {
      const { token } = JSON.parse(stored);
      if (token) {
        config.headers = config.headers || {};
        config.headers.Authorization = `Bearer ${token}`;
      }
    } catch (err) {
      // ignore
    }
  }
  return config;
});

// 统一提取后端错误信息，便于前端 toast 展示，并处理鉴权失效
api.interceptors.response.use(
  (res) => res,
  (err) => {
    const status = err?.response?.status;
    const message = err?.response?.data?.error || err?.message || "请求失败";
    if (status === 401) {
      const auth = useAuthStore(pinia);
      if (auth.isAuthenticated) {
        const toast = useToast();
        auth.logout(true);
        toast.push("登录已过期，请重新登录", "error");
        if (!window.location.pathname.startsWith("/auth")) {
          const redirect = encodeURIComponent(window.location.href);
          window.location.href = `/auth/login?redirect=${redirect}`;
        }
      }
    }
    return Promise.reject(new Error(message));
  }
);

export interface UserNotifications {
  approvals: boolean;
  jobs: boolean;
  alerts: boolean;
}

export interface UserProfile {
  id: string;
  username: string;
  name: string;
  role: string;
  organization?: string;
  phone?: string;
  email?: string;
  avatarUrl?: string;
  twoFactorEnabled?: boolean;
  notifications: UserNotifications;
  createdAt?: string;
  updatedAt?: string;
}

export interface DatasetSummary {
  id: string;
  name: string;
  size: number;
  modulus: number;
  version: string;
  updatedAt: string;
  owner: string;
}

export interface DatasetDetail extends DatasetSummary {
  stats?: {
    min: number;
    max: number;
    duplicates: number;
    hashed?: string;
  };
  sample?: number[];
}

export interface SessionSummary {
  id: string;
  name: string;
  status: string;
  size: number;
  threshold: number;
  modulus: number;
  updatedAt: string;
}

export interface SessionDetail extends SessionSummary {
  thresholdRatio: number;
  peerOrg?: string;
  notes?: string;
  thresholdReached: boolean;
  intersectionSize: number;
  receiverAccount?: string;
  senderAccount?: string;
  requestId?: string;
  metrics: {
    offlineMs: number;
    receiverOfflineMs: number;
    senderOfflineMs: number;
    onlineMs: number;
    receiverOnlineMs: number;
    senderOnlineMs: number;
    onlineCommMs: number;
    finalizeMs: number;
    coinFlipMs: number;
    totalProtocolMs: number;
    communicationBytes: number;
    onlineCommBytes: number;
    senderTimeMs: number;
    receiverTimeMs: number;
    senderBytes: number;
    receiverBytes: number;
    coinFlipBytes: number;
    pcgSeedBytes: number;
  };
  logs: Array<{ time: string; level: string; message: string }>;
  intersection?: number[];
}

export interface RequestRecord {
  id: string;
  applicant: string;
  receiverDatasetId: string;
  counterparty: string;
  senderDatasetId?: string;
  status:
    | "pending-sender"
    | "running"
    | "completed"
    | "failed"
    | "threshold-not-met"
    | "sender-rejected"
    | "cancelled";
  threshold: number;
  thresholdRatio: number;
  modulus: number;
  notes?: string;
  createdAt: string;
  updatedAt: string;
  sessionId?: string;
  errorMessage?: string;
}

export const fetchDatasets = () => api.get<DatasetSummary[]>("/datasets");
export const fetchDataset = (id: string) => api.get<DatasetDetail>(`/datasets/${id}`);
export interface CreateDatasetPayload {
  name: string;
  owner: string;
  modulus: number;
  version?: string;
  elements: number[];
}

export const createDataset = (payload: CreateDatasetPayload | FormData) => {
  if (payload instanceof FormData) {
    // 让浏览器自动设置带 boundary 的 Content-Type
    return api.post<DatasetDetail>("/datasets", payload);
  }
  return api.post<DatasetDetail>("/datasets", payload);
};

export const fetchSessions = () => api.get<SessionSummary[]>("/sessions");
export const fetchSession = (id: string) => api.get<SessionDetail>(`/sessions/${id}`);
export const rerunSession = (id: string) => api.post<SessionDetail>(`/sessions/${id}/rerun`);
export const downloadSessionIntersection = (id: string) =>
  api.get<Blob>(`/sessions/${id}/intersection.csv`, { responseType: "blob" });

export const fetchRequests = () => api.get<RequestRecord[]>("/requests");
export const createRequest = (payload: {
  counterparty: string;
  receiverDatasetId: string;
  threshold: number;
  notes?: string;
}) => api.post<{ request: RequestRecord }>("/requests", payload);
export const confirmRequest = (id: string, payload: { senderDatasetId: string }) =>
  api.post<{ request: RequestRecord; session?: SessionDetail }>(`/requests/${id}/confirm`, payload);
export const rejectRequest = (id: string) => api.post<{ request: RequestRecord }>(`/requests/${id}/reject`);
export const overrideRequest = (id: string, payload: { modulus?: number; threshold?: number; notes?: string }) =>
  api.post<{ request: RequestRecord }>(`/requests/${id}/override`, payload);
export const abortRequest = (id: string, payload?: { reason?: string }) =>
  api.post<{ request: RequestRecord }>(`/requests/${id}/abort`, payload);

export interface AuthResponse {
  token: string;
  user: UserProfile;
}

export const login = (payload: { username: string; password: string; role?: string }) => api.post<AuthResponse>("/auth/login", payload);
export const register = (payload: { username: string; password: string; role: string; organization?: string; name?: string }) =>
  api.post<AuthResponse>("/auth/register", payload);
export const fetchMe = () => api.get<{ user: UserProfile }>("/auth/me");
export const updateMe = (payload: Partial<Pick<UserProfile, "name" | "organization" | "phone" | "email">>) =>
  api.patch<{ user: UserProfile }>("/users/me", payload);
export const uploadAvatar = (formData: FormData) =>
  api.post<{ avatarUrl: string; user: UserProfile }>("/users/me/avatar", formData, {
    headers: { "Content-Type": "multipart/form-data" }
  });
export const updateNotifications = (payload: UserNotifications) => api.put<{ user: UserProfile }>("/users/me/notifications", payload);
export const changePassword = (payload: { old_password: string; new_password: string }) =>
  api.post("/auth/password", payload);
export const enableTwoFactor = () => api.post<{ user: UserProfile; secret: string }>("/auth/2fa/enable");
export const disableTwoFactor = () => api.post<{ user: UserProfile }>("/auth/2fa/disable");

export default api;
