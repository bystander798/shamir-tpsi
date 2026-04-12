<template>
  <section class="card">
    <h3>协同请求</h3>
    <p>确认接收方发起的求交请求，并决定是否参与。</p>
    <table v-if="pendingRequests.length || processedRequests.length">
      <thead>
        <tr>
          <th>创建时间</th>
          <th>申请方</th>
          <th>接收方数据集</th>
          <th>我方数据集</th>
          <th>阈值</th>
          <th>状态</th>
          <th>操作</th>
        </tr>
      </thead>
      <tbody>
        <tr v-for="req in pendingRequests" :key="req.id">
          <td>{{ req.createdAt }}</td>
          <td>{{ req.applicant }}</td>
          <td>{{ datasetName(req.receiverDatasetId) }}</td>
          <td>
            <select v-model="selection[req.id]">
              <option value="">选择我的数据集</option>
              <option v-for="item in myDatasets" :key="item.id" :value="item.id">
                {{ item.name }} ({{ item.size }})
              </option>
            </select>
          </td>
          <td>{{ req.threshold || '未指定' }}</td>
          <td><span class="status pending-sender">待我确认</span></td>
          <td class="ops">
            <button @click="confirm(req.id)">确认</button>
            <button class="danger" @click="reject(req.id)">拒绝</button>
          </td>
        </tr>
        <tr v-for="req in processedRequests" :key="req.id">
          <td>{{ req.createdAt }}</td>
          <td>{{ req.applicant }}</td>
          <td>
            {{ datasetName(req.receiverDatasetId) }}
            <button class="link-btn" type="button" @click="openDataset(req.receiverDatasetId)">详情</button>
          </td>
          <td>
            <span v-if="req.senderDatasetId">
              {{ datasetName(req.senderDatasetId) }}
              <button
                v-if="canViewDataset(req.senderDatasetId)"
                class="link-btn"
                type="button"
                @click="openDataset(req.senderDatasetId)"
              >
                详情
              </button>
            </span>
            <span v-else>待确认</span>
          </td>
          <td>{{ req.threshold || '未指定' }}</td>
          <td>
            <span :class="['status', req.status]">{{ statusText(req.status) }}</span>
          </td>
          <td>
            <RouterLink v-if="req.sessionId" :to="{ name: 'sessions', query: { focus: req.sessionId } }">查看任务</RouterLink>
          </td>
        </tr>
      </tbody>
    </table>
    <p v-else>暂无协同请求。</p>
    <DatasetDetailDrawer v-if="detailId" :dataset-id="detailId" @close="detailId = ''" />
  </section>
</template>

<script setup lang="ts">
import { computed, onMounted, reactive, ref } from "vue";
import { useRequestsStore } from "../../stores/requests";
import { useDatasetsStore } from "../../stores/datasets";
import { useAuthStore } from "../../stores/auth";
import { useToast } from "../../services/toast";
import type { DatasetSummary } from "../../api/client";
import DatasetDetailDrawer from "../../components/sessions/DatasetDetailDrawer.vue";

const requestsStore = useRequestsStore();
const datasetsStore = useDatasetsStore();
const auth = useAuthStore();
const toast = useToast();

auth.restore();

onMounted(async () => {
  await datasetsStore.loadSummaries(true);
  await requestsStore.reload();
});

const datasetMap = computed(() => {
  const map = new Map<string, DatasetSummary>();
  datasetsStore.summaries.forEach((item) => map.set(item.id, item));
  return map;
});

const myName = computed(() => auth.user?.username || "sender");
const pendingRequests = computed(() =>
  requestsStore.items.filter((item) => item.counterparty === myName.value && item.status === "pending-sender")
);
const processedRequests = computed(() =>
  requestsStore.items.filter((item) => item.counterparty === myName.value && item.status !== "pending-sender")
);

const datasetName = (id: string) => datasetMap.value.get(id)?.name || id;
const detailId = ref("");
const selection = reactive<Record<string, string>>({});
const myDatasets = computed(() => datasetsStore.summaries.filter((item) => item.owner === myName.value));
const canViewDataset = (id: string) => datasetMap.value.get(id)?.owner === myName.value;

const statusText = (status: string) => {
  switch (status) {
    case "running":
      return "执行中";
    case "completed":
      return "已完成";
    case "threshold-not-met":
      return "阈值未达";
    case "failed":
      return "失败";
    case "sender-rejected":
      return "已拒绝";
    case "cancelled":
      return "已终止";
    default:
      return status;
  }
};

const confirm = async (id: string) => {
  const datasetId = selection[id];
  if (!datasetId) {
    toast.push("请选择本方数据集", "error");
    return;
  }
  try {
    await requestsStore.confirm(id, datasetId);
    selection[id] = "";
  } catch (err) {
    toast.push((err as Error).message, "error");
  }
};

const reject = async (id: string) => {
  try {
    await requestsStore.reject(id);
  } catch (err) {
    toast.push((err as Error).message, "error");
  }
};

const openDataset = async (id: string) => {
  if (!id) return;
  if (!canViewDataset(id)) {
    toast.push("无权限查看该数据集详情", "error");
    return;
  }
  await datasetsStore.loadDetail(id);
  detailId.value = id;
};
</script>

<style scoped>
.card {
  background: #fff;
  border-radius: 16px;
  padding: 24px;
  box-shadow: 0 12px 32px rgba(24, 43, 90, 0.1);
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

.ops button {
  margin-right: 8px;
  border: none;
  padding: 6px 10px;
  border-radius: 8px;
  background: #eef3ff;
  cursor: pointer;
}

.ops .danger {
  background: rgba(255, 94, 104, 0.16);
  color: #ff5e68;
}

td select {
  padding: 6px 8px;
  border-radius: 8px;
  border: 1px solid #d6ddf2;
}

.link-btn {
  margin-left: 6px;
  border: none;
  background: none;
  color: #2f6bff;
  cursor: pointer;
  font-size: 12px;
  padding: 0;
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
</style>
