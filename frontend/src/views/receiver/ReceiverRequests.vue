<template>
  <div class="grid">
    <section class="card">
      <h3>发起求交申请</h3>
      <form class="form" @submit.prevent="submitRequest">
        <label>
          选择本地数据集
          <select v-model="receiverDatasetId" required>
            <option disabled value="">请选择数据集</option>
            <option v-for="item in myDatasets" :key="item.id" :value="item.id">
              {{ item.name }} ({{ item.size }})
            </option>
          </select>
        </label>
        <label>
          目标发送方账号
          <input v-model.trim="counterparty" list="sender-account-options" placeholder="请输入发送方账号" required />
        </label>
        <datalist id="sender-account-options">
          <option v-for="account in senderAccounts" :key="account" :value="account" />
        </datalist>
        <section v-if="receiverDetail" class="selected">
          <article class="preview-card">
            <header>本地数据集</header>
            <p>规模：{{ receiverDetail.size.toLocaleString("en-US") }}</p>
            <p>模数：{{ receiverDetail.modulus.toLocaleString("en-US") }}</p>
            <p v-if="receiverDetail.stats">重复元素：{{ receiverDetail.stats.duplicates }}</p>
            <p v-if="receiverDetail.sample?.length">
              样本：{{ receiverDetail.sample.slice(0, 10).join(", ") }}
            </p>
          </article>
        </section>
        <label>
          阈值
          <input v-model.number="threshold" type="number" min="1" required />
        </label>
        <label>
          备注
          <input v-model.trim="notes" placeholder="例如：季度对账" />
        </label>
        <button class="primary" type="submit">提交申请</button>
      </form>
    </section>
    <section class="card">
      <h3>我的申请</h3>
      <table v-if="myRequests.length">
        <thead>
          <tr>
            <th>创建时间</th>
            <th>发送方</th>
            <th>发送方数据集</th>
            <th>阈值</th>
            <th>状态</th>
            <th>关联任务</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="req in myRequests" :key="req.id">
            <td>{{ req.createdAt }}</td>
            <td>{{ req.counterparty }}</td>
            <td>{{ req.senderDatasetId ? datasetName(req.senderDatasetId) : "待发送方确认" }}</td>
            <td>{{ req.threshold || '未指定' }}</td>
            <td><span :class="['status', req.status]">{{ statusText(req.status) }}</span></td>
            <td>
              <RouterLink v-if="req.sessionId" :to="{ name: 'sessions', query: { focus: req.sessionId } }">查看任务</RouterLink>
            </td>
          </tr>
        </tbody>
      </table>
      <p v-else>暂无求交申请。</p>
    </section>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, ref, watch } from "vue";
import { useDatasetsStore } from "../../stores/datasets";
import { useRequestsStore } from "../../stores/requests";
import { useAuthStore } from "../../stores/auth";
import { useToast } from "../../services/toast";
import type { DatasetSummary } from "../../api/client";

const datasetsStore = useDatasetsStore();
const requestsStore = useRequestsStore();
const auth = useAuthStore();
const toast = useToast();

auth.restore();

const receiverDatasetId = ref("");
const counterparty = ref("");
const threshold = ref<number | null>(null);
const notes = ref("");

onMounted(async () => {
  await datasetsStore.loadSummaries(true);
  await requestsStore.reload();
});

watch(receiverDatasetId, (id) => {
  if (id) datasetsStore.loadDetail(id);
});

const datasetMap = computed(() => {
  const map = new Map<string, DatasetSummary>();
  datasetsStore.summaries.forEach((item) => map.set(item.id, item));
  return map;
});

const myDatasets = computed(() => datasetsStore.summaries.filter((item) => item.owner === auth.user?.username));
const senderAccounts = computed(() => {
  const owners = new Set<string>();
  datasetsStore.summaries
    .filter((item) => item.owner !== auth.user?.username)
    .forEach((item) => owners.add(item.owner));
  return Array.from(owners).sort();
});
const myRequests = computed(() => requestsStore.items.filter((item) => item.applicant === auth.user?.username));
const receiverDetail = computed(() => (receiverDatasetId.value ? datasetsStore.details[receiverDatasetId.value] : null));

const datasetName = (id: string) => datasetMap.value.get(id)?.name || id;

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

const submitRequest = async () => {
  if (!receiverDatasetId.value) {
    toast.push("请选择本地数据集", "error");
    return;
  }
  if (!counterparty.value) {
    toast.push("请填写目标发送方账号", "error");
    return;
  }
  if (!threshold.value || threshold.value <= 0) {
    toast.push("请填写有效阈值", "error");
    return;
  }
  const thresholdValue = threshold.value ?? 0;
  try {
    await requestsStore.create({
      counterparty: counterparty.value,
      receiverDatasetId: receiverDatasetId.value,
      threshold: thresholdValue,
      notes: notes.value
    });
    receiverDatasetId.value = "";
    counterparty.value = "";
    threshold.value = null;
    notes.value = "";
  } catch (err) {
    toast.push((err as Error).message, "error");
  }
};
</script>

<style scoped>
.grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(320px, 1fr));
  gap: 24px;
}

.card {
  background: #fff;
  border-radius: 16px;
  padding: 24px;
  box-shadow: 0 12px 32px rgba(24, 43, 90, 0.1);
}

.form {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

label {
  display: flex;
  flex-direction: column;
  gap: 8px;
  font-size: 14px;
}

select,
input {
  padding: 10px;
  border: 1px solid #d6ddf2;
  border-radius: 10px;
}

.selected {
  display: flex;
  gap: 16px;
  flex-wrap: wrap;
}

.preview-card {
  flex: 1 1 220px;
  background: #f5f8ff;
  border-radius: 12px;
  padding: 12px 16px;
  display: grid;
  gap: 6px;
  font-size: 13px;
  color: #475a85;
}

.preview-card header {
  font-weight: 600;
  color: #1c2f6d;
}

.primary {
  align-self: flex-start;
  border: none;
  padding: 10px 16px;
  border-radius: 10px;
  background: linear-gradient(135deg, #5a78f0, #4cb4ff);
  color: #fff;
  font-weight: 600;
  cursor: pointer;
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
</style>
