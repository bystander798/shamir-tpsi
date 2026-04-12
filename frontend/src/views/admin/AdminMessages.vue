<template>
  <section class="card">
    <header>
      <h3>求交申请审批</h3>
      <p>查看求交申请进度，管理员可调整参数或终止异常流程。</p>
    </header>
    <template v-if="requests.length">
      <table>
        <thead>
          <tr>
            <th>创建时间</th>
            <th>接收方</th>
            <th>发送方</th>
            <th>阈值</th>
            <th>模数</th>
            <th>状态</th>
            <th>错误信息</th>
            <th>数据</th>
            <th>操作</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="req in requests" :key="req.id">
            <td>{{ req.createdAt }}</td>
            <td>{{ req.applicant }} / {{ datasetName(req.receiverDatasetId) }}</td>
            <td>{{ req.counterparty }} / {{ datasetName(req.senderDatasetId) }}</td>
            <td>{{ req.threshold || '未指定' }}</td>
            <td>{{ req.modulus.toLocaleString("en-US") }}</td>
            <td><span :class="['status', req.status]">{{ statusText(req.status) }}</span></td>
            <td><span class="error" v-if="req.errorMessage">{{ req.errorMessage }}</span></td>
            <td>
              <button class="link-btn" type="button" @click="togglePreview(req)">
                {{ previewRequest && previewRequest.id === req.id ? "收起摘要" : "查看摘要" }}
              </button>
              <button class="link-btn" type="button" @click="openDataset(req.receiverDatasetId)">接收方</button>
              <button
                class="link-btn"
                type="button"
                :disabled="!req.senderDatasetId"
                @click="openDataset(req.senderDatasetId)"
              >
                发送方
              </button>
            </td>
            <td class="ops">
              <button
                :disabled="!canOverride(req)"
                @click="setPreview(req)"
              >
                调整参数
              </button>
              <button
                class="danger"
                :disabled="!canAbort(req)"
                @click="abort(req.id)">
                终止
              </button>
              <RouterLink v-if="req.sessionId" :to="{ name: 'sessions', query: { focus: req.sessionId } }">查看任务</RouterLink>
            </td>
          </tr>
        </tbody>
      </table>
      <section v-if="previewRequest" class="preview-panel">
        <h4>数据集摘要</h4>
        <div class="preview-grid">
          <article class="preview-card" v-if="receiverDetail">
            <header>{{ previewRequest.applicant }} / {{ receiverDetail.name }}</header>
            <p>规模：{{ receiverDetail.size.toLocaleString("en-US") }}</p>
            <p>模数：{{ receiverDetail.modulus.toLocaleString("en-US") }}</p>
            <p v-if="receiverDetail.stats">重复元素：{{ receiverDetail.stats.duplicates }}</p>
            <p v-if="receiverDetail.sample?.length">
              样本：{{ receiverDetail.sample.slice(0, 10).join(", ") }}
            </p>
          </article>
          <article class="preview-card" v-if="senderDetail">
            <header>{{ previewRequest.counterparty }} / {{ senderDetail.name }}</header>
            <p>规模：{{ senderDetail.size.toLocaleString("en-US") }}</p>
            <p>模数：{{ senderDetail.modulus.toLocaleString("en-US") }}</p>
            <p v-if="senderDetail.stats">重复元素：{{ senderDetail.stats.duplicates }}</p>
            <p v-if="senderDetail.sample?.length">
              样本：{{ senderDetail.sample.slice(0, 10).join(", ") }}
            </p>
          </article>
        </div>
        <form class="override-form" @submit.prevent="applyOverride">
          <label>
            模数
            <input v-model.number="form.modulus" type="number" min="2" />
          </label>
          <label>
            阈值
            <input v-model.number="form.threshold" type="number" min="0" />
          </label>
          <label>
            备注
            <input v-model.trim="form.notes" type="text" placeholder="可选说明" />
          </label>
          <div class="buttons">
            <button type="submit" :disabled="!formDirty">保存参数</button>
            <button type="button" class="ghost" @click="clearForm">重置</button>
          </div>
        </form>
      </section>
    </template>
    <p v-else>暂无求交申请。</p>
    <DatasetDetailDrawer v-if="detailId" :dataset-id="detailId" @close="detailId = ''" />
  </section>
</template>

<script setup lang="ts">
import { computed, onMounted, reactive, ref } from "vue";
import { useRequestsStore } from "../../stores/requests";
import { useDatasetsStore } from "../../stores/datasets";
import DatasetDetailDrawer from "../../components/sessions/DatasetDetailDrawer.vue";
import type { DatasetSummary, RequestRecord } from "../../api/client";
import { useToast } from "../../services/toast";

const requestsStore = useRequestsStore();
const datasetsStore = useDatasetsStore();
const toast = useToast();

onMounted(() => {
  datasetsStore.loadSummaries();
  requestsStore.reload();
});

const datasetMap = computed(() => {
  const map = new Map<string, DatasetSummary>();
  datasetsStore.summaries.forEach((item) => map.set(item.id, item));
  return map;
});

const requests = computed(() => requestsStore.items);

const datasetName = (id: string) => datasetMap.value.get(id)?.name || id;

const detailId = ref("");
const previewRequestId = ref("");
const previewRequest = computed(() => requests.value.find((item) => item.id === previewRequestId.value) ?? null);
const receiverDetail = computed(() =>
  previewRequest.value ? datasetsStore.details[previewRequest.value.receiverDatasetId] : null
);
const senderDetail = computed(() =>
  previewRequest.value ? datasetsStore.details[previewRequest.value.senderDatasetId] : null
);

const form = reactive({
  modulus: 0,
  threshold: 0,
  notes: ""
});
const initialForm = reactive({ modulus: 0, threshold: 0, notes: "" });
const formDirty = computed(
  () =>
    form.modulus !== initialForm.modulus ||
    form.threshold !== initialForm.threshold ||
    form.notes.trim() !== initialForm.notes.trim()
);

const statusText = (status: string) => {
  switch (status) {
    case "pending-sender":
      return "待发送方确认";
    case "running":
      return "执行中";
    case "completed":
      return "已完成";
    case "threshold-not-met":
      return "阈值未达";
    case "failed":
      return "失败";
    case "sender-rejected":
      return "发送方拒绝";
    case "cancelled":
      return "已终止";
    default:
      return status;
  }
};

const canOverride = (req: RequestRecord) => ["pending-sender", "threshold-not-met", "failed"].includes(req.status);
const canAbort = (req: RequestRecord) => req.status === "pending-sender";

const syncForm = (req: RequestRecord) => {
  form.modulus = req.modulus;
  initialForm.modulus = req.modulus;
  form.threshold = req.threshold || 0;
  initialForm.threshold = req.threshold || 0;
  form.notes = req.notes || "";
  initialForm.notes = req.notes || "";
};

const togglePreview = async (req: RequestRecord) => {
  if (previewRequestId.value === req.id) {
    previewRequestId.value = "";
    return;
  }
  previewRequestId.value = req.id;
  const tasks: Promise<unknown>[] = [];
  if (req.receiverDatasetId) tasks.push(datasetsStore.loadDetail(req.receiverDatasetId));
  if (req.senderDatasetId) tasks.push(datasetsStore.loadDetail(req.senderDatasetId));
  await Promise.all(tasks);
  syncForm(req);
};

const openDataset = async (id: string) => {
  if (!id) return;
  await datasetsStore.loadDetail(id);
  detailId.value = id;
};

const setPreview = async (req: RequestRecord) => {
  if (previewRequestId.value !== req.id) {
    await togglePreview(req);
  } else {
    syncForm(req);
  }
};

const clearForm = () => {
  form.modulus = initialForm.modulus;
  form.threshold = initialForm.threshold;
  form.notes = initialForm.notes;
};

const applyOverride = async () => {
  const current = previewRequest.value;
  if (!current) {
    toast.push("请先选择请求", "error");
    return;
  }
  const payload: Parameters<typeof requestsStore.override>[1] = {};
  if (form.modulus > 0 && form.modulus !== initialForm.modulus) {
    payload.modulus = form.modulus;
  }
  if (form.threshold >= 0 && form.threshold !== initialForm.threshold) {
    payload.threshold = form.threshold;
  }
  if (form.notes.trim() !== initialForm.notes.trim()) {
    payload.notes = form.notes.trim();
  }
  if (!Object.keys(payload).length) {
    toast.push("没有需要保存的更改", "info");
    return;
  }
  try {
    await requestsStore.override(current.id, payload);
  } catch (err) {
    toast.push((err as Error).message, "error");
    return;
  }
  const updated = requests.value.find((item) => item.id === current.id) ?? current;
  syncForm(updated);
};

const abort = async (id: string) => {
  try {
    await requestsStore.abort(id);
  } catch (err) {
    toast.push((err as Error).message, "error");
    return;
  }
  if (previewRequestId.value === id) {
    previewRequestId.value = "";
  }
};
</script>

<style scoped>
.card {
  background: #fff;
  border-radius: 16px;
  padding: 24px;
  box-shadow: 0 12px 32px rgba(24, 43, 90, 0.1);
}

header {
  display: flex;
  flex-direction: column;
  gap: 8px;
  margin-bottom: 16px;
}

table {
  width: 100%;
  border-collapse: collapse;
}

th,
td {
  padding: 12px 16px;
  text-align: left;
  font-size: 14px;
  border-bottom: 1px solid #e5ecff;
}

.ops {
  display: flex;
  gap: 8px;
  align-items: center;
}

.ops button {
  border: none;
  padding: 6px 10px;
  border-radius: 8px;
  background: #eef3ff;
  cursor: pointer;
}

.ops button[disabled] {
  opacity: 0.4;
  cursor: not-allowed;
}

.link-btn {
  border: none;
  background: none;
  padding: 0;
  margin-right: 8px;
  color: #2f6bff;
  cursor: pointer;
  font-size: 12px;
}

.ops .danger {
  background: rgba(255, 94, 104, 0.16);
  color: #ff5e68;
}

.status.pending-sender {
  color: #ff9f40;
}

.status.running {
  color: #2f6bff;
}

.status.completed {
  color: #3ba55c;
}

.status.threshold-not-met {
  color: #ffb020;
}

.status.failed {
  color: #ff5e68;
}

.status.sender-rejected,
.status.cancelled {
  color: #7a88ad;
}

.error {
  color: #ff5e68;
  font-size: 12px;
}

.preview-panel {
  margin-top: 16px;
  background: #f5f8ff;
  border-radius: 12px;
  padding: 16px;
  display: grid;
  gap: 12px;
}

.preview-panel h4 {
  margin: 0;
}

.preview-grid {
  display: flex;
  gap: 16px;
  flex-wrap: wrap;
}

.preview-card {
  flex: 1 1 220px;
  background: #fff;
  border-radius: 12px;
  padding: 12px 16px;
  box-shadow: 0 6px 18px rgba(16, 28, 60, 0.08);
  color: #475a85;
  font-size: 13px;
}

.override-form {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
  gap: 12px 16px;
  margin-top: 16px;
  align-items: end;
}

.override-form label {
  display: flex;
  flex-direction: column;
  gap: 6px;
  font-size: 13px;
}

.override-form input {
  padding: 8px 10px;
  border-radius: 8px;
  border: 1px solid #d6ddf2;
}

.override-form .buttons {
  display: flex;
  gap: 12px;
}

.override-form button {
  border: none;
  padding: 8px 12px;
  border-radius: 8px;
  background: linear-gradient(135deg, #5a78f0, #4cb4ff);
  color: #fff;
  cursor: pointer;
}

.override-form button[disabled] {
  opacity: 0.4;
  cursor: not-allowed;
}

.override-form .ghost {
  background: #eef3ff;
  color: #2f3a6a;
}

.link-btn:disabled {
  opacity: 0.4;
  cursor: not-allowed;
}

.preview-card header {
  font-weight: 600;
  color: #1c2f6d;
  margin-bottom: 4px;
}
</style>
