import { defineStore } from "pinia";
import { reactive, toRef } from "vue";
import {
  fetchRequests,
  createRequest,
  confirmRequest,
  rejectRequest,
  overrideRequest,
  abortRequest,
  type RequestRecord,
  type SessionDetail
} from "../api/client";
import { useToast } from "../services/toast";
import { useSessionsStore } from "./sessions";

interface RequestState {
  items: RequestRecord[];
  loaded: boolean;
}

const upsert = (list: RequestRecord[], record: RequestRecord) => {
  const index = list.findIndex((item) => item.id === record.id);
  if (index >= 0) {
    list[index] = record;
  } else {
    list.unshift(record);
  }
};

export const useRequestsStore = defineStore("requests", () => {
  const state = reactive<RequestState>({ items: [], loaded: false });
  const items = toRef(state, "items");
  const toast = useToast();
  const sessions = useSessionsStore();

  const load = async () => {
    if (state.loaded) return;
    const { data } = await fetchRequests();
    state.items.splice(0, state.items.length, ...data);
    state.loaded = true;
  };

  const reload = async () => {
    const { data } = await fetchRequests();
    state.items.splice(0, state.items.length, ...data);
    state.loaded = true;
  };

  const create = async (payload: Parameters<typeof createRequest>[0]) => {
    const { data } = await createRequest(payload);
    upsert(items.value, data.request);
    toast.push("申请已提交，等待发送方确认", "success");
  };

  const confirm = async (id: string, senderDatasetId: string) => {
    const { data } = await confirmRequest(id, { senderDatasetId });
    upsert(items.value, data.request);
    if (data.session) {
      sessions.upsert(data.session as SessionDetail);
    }
    if (data.request.status === "completed") {
      toast.push("求交已完成", "success");
    } else if (data.request.status === "threshold-not-met") {
      toast.push("求交完成，阈值未达", "info");
    } else if (data.request.status === "failed") {
      toast.push(data.request.errorMessage || "求交失败", "error");
    } else {
      toast.push("协作已确认", "success");
    }
  };

  const reject = async (id: string) => {
    const { data } = await rejectRequest(id);
    upsert(items.value, data.request);
    toast.push("已拒绝该申请", "info");
  };

  const override = async (id: string, payload: Parameters<typeof overrideRequest>[1]) => {
    const { data } = await overrideRequest(id, payload);
    upsert(items.value, data.request);
    toast.push("参数已更新", "success");
  };

  const abort = async (id: string, reason?: string) => {
    const { data } = await abortRequest(id, reason ? { reason } : undefined);
    upsert(items.value, data.request);
    toast.push("请求已终止", "info");
  };

  const reset = () => {
    state.items.splice(0, state.items.length);
    state.loaded = false;
  };

  return {
    items,
    load,
    reload,
    create,
    confirm,
    reject,
    override,
    abort,
    reset
  };
});
