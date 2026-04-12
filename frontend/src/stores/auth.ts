import { defineStore } from "pinia";
import { computed, ref } from "vue";
import {
  changePassword as apiChangePassword,
  disableTwoFactor,
  enableTwoFactor,
  fetchMe,
  login as apiLogin,
  register as apiRegister,
  updateMe,
  updateNotifications as apiUpdateNotifications,
  uploadAvatar as apiUploadAvatar,
  type UserProfile as ApiUserProfile,
  type UserNotifications
} from "../api/client";
import { useToast } from "../services/toast";
import { pinia } from ".";
import { useDatasetsStore } from "./datasets";
import { useRequestsStore } from "./requests";
import { useSessionsStore } from "./sessions";

export type UserRole = "receiver" | "sender" | "admin";

export interface UserProfile extends ApiUserProfile {
  role: UserRole;
  notifications: UserNotifications;
  twoFactorEnabled?: boolean;
}

interface Credentials {
  username: string;
  password: string;
  role: UserRole;
}

const STORAGE_KEY = "tpsi_auth_state";

interface StoredState {
  token: string;
  user: UserProfile | null;
}

const persistState = (state: StoredState) => {
  try {
    window.localStorage.setItem(STORAGE_KEY, JSON.stringify(state));
  } catch (err) {
    console.warn("persist auth state failed", err);
  }
};

const readState = (): StoredState => {
  try {
    const raw = window.localStorage.getItem(STORAGE_KEY);
    if (!raw) {
      return { token: "", user: null };
    }
    return JSON.parse(raw) as StoredState;
  } catch (err) {
    console.warn("read auth state failed", err);
    return { token: "", user: null };
  }
};

export const useAuthStore = defineStore("auth", () => {
  const token = ref("");
  const user = ref<UserProfile | null>(null);
  const hydrated = ref(false);
  const toast = useToast();

  const isAuthenticated = computed(() => !!token.value && !!user.value);

  const resetDomainStores = () => {
    const datasets = useDatasetsStore(pinia);
    datasets.reset();
    const requests = useRequestsStore(pinia);
    requests.reset();
    const sessions = useSessionsStore(pinia);
    sessions.reset();
  };

  const normalizeRole = (role: string): UserRole => {
    if (role === "sender" || role === "admin") return role;
    return "receiver";
  };

  const normalizeNotifications = (notifications?: UserNotifications): UserNotifications => {
    return {
      approvals: notifications?.approvals ?? true,
      jobs: notifications?.jobs ?? true,
      alerts: notifications?.alerts ?? true
    };
  };

  const normalizeUser = (raw: ApiUserProfile | UserProfile): UserProfile => {
    return {
      ...raw,
      role: normalizeRole(raw.role),
      notifications: normalizeNotifications(raw.notifications),
      twoFactorEnabled: raw.twoFactorEnabled ?? false
    };
  };

  const restore = () => {
    if (hydrated.value) return;
    const snapshot = readState();
    token.value = snapshot.token;
    user.value = snapshot.user ? normalizeUser(snapshot.user) : null;
    hydrated.value = true;
    if (token.value && !user.value) {
      refreshMe().catch(() => {
        clearState(true);
      });
    }
  };

  const clearState = (silent = false) => {
    token.value = "";
    user.value = null;
    resetDomainStores();
    persistState({ token: "", user: null });
    if (!silent) {
      toast.push("已退出登录", "info");
    }
  };

  const login = async (payload: Credentials) => {
    if (!payload.username || !payload.password) {
      throw new Error("请输入账号与密码");
    }
    const { data } = await apiLogin({
      username: payload.username,
      password: payload.password,
      role: payload.role
    });
    resetDomainStores();
    token.value = data.token;
    user.value = normalizeUser(data.user);
    persistState({ token: token.value, user: user.value });
    toast.push("登录成功", "success");
  };

  const logout = (silent = false) => {
    clearState(silent);
  };

  const register = async (payload: { username: string; password: string; organization?: string; role: UserRole }) => {
    const { data } = await apiRegister({
      username: payload.username,
      password: payload.password,
      role: payload.role,
      organization: payload.organization
    });
    resetDomainStores();
    token.value = data.token;
    user.value = normalizeUser(data.user);
    persistState({ token: token.value, user: user.value });
    toast.push("注册成功，已自动登录", "success");
  };

  const refreshMe = async () => {
    if (!token.value) return;
    try {
      const { data } = await fetchMe();
      user.value = normalizeUser(data.user);
      persistState({ token: token.value, user: user.value });
    } catch (err) {
      logout(true);
      throw err;
    }
  };

  const updateProfile = async (profile: Partial<UserProfile>) => {
    if (!user.value) return;
    const { data } = await updateMe({
      name: profile.name,
      organization: profile.organization,
      email: profile.email,
      phone: profile.phone
    });
    user.value = normalizeUser(data.user);
    persistState({ token: token.value, user: user.value });
    toast.push("个人信息已更新", "success");
  };

  const updateNotifications = async (prefs: UserProfile["notifications"]) => {
    if (!user.value) return;
    const { data } = await apiUpdateNotifications(prefs);
    user.value = normalizeUser(data.user);
    persistState({ token: token.value, user: user.value });
    toast.push("通知偏好已保存", "success");
  };

  const changePassword = async (payload: { oldPassword: string; newPassword: string }) => {
    if (!payload.oldPassword || !payload.newPassword) {
      throw new Error("请输入完整的密码信息");
    }
    await apiChangePassword({ old_password: payload.oldPassword, new_password: payload.newPassword });
    toast.push("密码已更新", "success");
  };

  const toggleTwoFactor = async (enable: boolean) => {
    if (!user.value) return null;
    if (enable) {
      const { data } = await enableTwoFactor();
      user.value = normalizeUser(data.user);
      persistState({ token: token.value, user: user.value });
      toast.push("已开启二次验证", "success");
      return data.secret;
    }
    const { data } = await disableTwoFactor();
    user.value = normalizeUser(data.user);
    persistState({ token: token.value, user: user.value });
    toast.push("已关闭二次验证", "info");
    return null;
  };

  const uploadAvatar = async (file: File) => {
    const formData = new FormData();
    formData.append("file", file);
    const { data } = await apiUploadAvatar(formData);
    user.value = normalizeUser(data.user);
    persistState({ token: token.value, user: user.value });
    toast.push("头像已更新", "success");
    return data.avatarUrl;
  };

  const defaultRouteForRole = (role: UserRole) => {
    switch (role) {
      case "receiver":
        return "/receiver";
      case "sender":
        return "/sender";
      case "admin":
      default:
        return "/";
    }
  };

  return {
    token,
    user,
    hydrated,
    isAuthenticated,
    restore,
    refreshMe,
    login,
    logout,
    register,
    updateProfile,
    updateNotifications,
    changePassword,
    toggleTwoFactor,
    uploadAvatar,
    defaultRouteForRole
  };
});
