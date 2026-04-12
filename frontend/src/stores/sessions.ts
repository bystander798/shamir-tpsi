import { defineStore } from "pinia";
import { reactive, toRef } from "vue";
import api, { fetchSessions, fetchSession, rerunSession, type SessionDetail, type SessionSummary } from "../api/client";
import { useToast } from "../services/toast";

interface SessionState {
  summaries: SessionSummary[];
  details: Record<string, SessionDetail>;
  loaded: boolean;
}

export const useSessionsStore = defineStore("sessions", () => {
  const state = reactive<SessionState>({ summaries: [], details: {}, loaded: false });
  const summaries = toRef(state, "summaries");
  const details = toRef(state, "details");
  const toast = useToast();

  const loadSummaries = async (force = false) => {
    if (!force && state.loaded) return;
    const { data } = await fetchSessions();
    summaries.value.splice(0, summaries.value.length, ...data);
    state.loaded = true;
  };

  const loadDetail = async (id: string) => {
    const { data } = await fetchSession(id);
    upsert(data);
  };

  const upsert = (detail: SessionDetail) => {
    details.value[detail.id] = detail;
    const summary: SessionSummary = {
      id: detail.id,
      name: detail.name,
      status: detail.status,
      size: detail.size,
      threshold: detail.threshold,
      modulus: detail.modulus,
      updatedAt: detail.updatedAt
    };
    const index = summaries.value.findIndex((item) => item.id === detail.id);
    if (index >= 0) {
      summaries.value[index] = summary;
    } else {
      summaries.value.unshift(summary);
    }
  };

  const rerun = async (id: string) => {
    try {
      const { data } = await rerunSession(id);
      upsert(data);
      toast.push("任务已重新执行", "success");
    } catch (err) {
      toast.push("重跑失败", "error");
    }
  };

  return {
    summaries,
    details,
    loadSummaries,
    loadDetail,
    upsert,
    rerun,
    reset: () => {
      summaries.value.splice(0, summaries.value.length);
      Object.keys(details.value).forEach((key) => delete details.value[key]);
      state.loaded = false;
    }
  };
});
