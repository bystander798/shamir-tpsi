import { defineStore } from "pinia";
import { reactive, toRef } from "vue";
import api, { createDataset, fetchDatasets, fetchDataset, type DatasetDetail, type DatasetSummary } from "../api/client";
import { useToast } from "../services/toast";

interface DatasetState {
  summaries: DatasetSummary[];
  details: Record<string, DatasetDetail>;
}

export const useDatasetsStore = defineStore("datasets", () => {
  const state = reactive<DatasetState>({ summaries: [], details: {} });
  const summaries = toRef(state, "summaries");
  const details = toRef(state, "details");
  const toast = useToast();

  const sortSummaries = () => {
    state.summaries.sort((a, b) => b.updatedAt.localeCompare(a.updatedAt));
  };

  const loadSummaries = async (force = false) => {
    if (!force && state.summaries.length) return;
    const { data } = await fetchDatasets();
    state.summaries.splice(0, state.summaries.length, ...data);
    sortSummaries();
  };

  const loadDetail = async (id: string) => {
    if (!id) return;
    if (state.details[id]) return;
    const { data } = await fetchDataset(id);
    state.details[id] = data;
    const summary: DatasetSummary = {
      id: data.id,
      name: data.name,
      size: data.size,
      modulus: data.modulus,
      version: data.version,
      updatedAt: data.updatedAt,
      owner: data.owner
    };
    const index = state.summaries.findIndex((item) => item.id === data.id);
    if (index >= 0) {
      state.summaries.splice(index, 1, summary);
    } else {
      state.summaries.push(summary);
    }
    sortSummaries();
  };

  const create = async (payload: { name: string; owner: string; modulus: number; elements: number[]; file?: File | null }) => {
    const requestPayload =
      payload.file instanceof File
        ? (() => {
            const form = new FormData();
            form.append("name", payload.name);
            form.append("modulus", String(payload.modulus));
            form.append("version", "上传");
            form.append("owner", payload.owner);
            form.append("elements", JSON.stringify(payload.elements));
            form.append("file", payload.file);
            return form;
          })()
        : { ...payload, version: "上传" };

    const { data } = await createDataset(requestPayload);
    const summary: DatasetSummary = {
      id: data.id,
      name: data.name,
      size: data.size,
      modulus: data.modulus,
      version: data.version,
      updatedAt: data.updatedAt,
      owner: data.owner
    };
    const existing = state.summaries.findIndex((item) => item.id === data.id);
    if (existing >= 0) {
      state.summaries.splice(existing, 1, summary);
    } else {
      state.summaries.unshift(summary);
    }
    sortSummaries();
    state.details[data.id] = data;
    return data;
  };

  const exportSample = async (id: string) => {
    try {
      const response = await api.get(`/datasets/${id}/sample`, { responseType: "blob" });
      const url = URL.createObjectURL(response.data);
      const a = document.createElement("a");
      a.href = url;
      a.download = `${id}-sample.csv`;
      a.click();
      URL.revokeObjectURL(url);
    } catch (err) {
      toast.push("导出失败", "error");
    }
  };

  const reset = () => {
    state.summaries.splice(0, state.summaries.length);
    Object.keys(state.details).forEach((key) => delete state.details[key]);
  };

  return {
    summaries,
    details,
    loadSummaries,
    loadDetail,
    create,
    exportSample,
    reset
  };
});
