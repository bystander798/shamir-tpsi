<template>
  <section class="card">
    <header class="head">
      <div>
        <h3>概览</h3>
        <p>接收方最新的数据集、待处理请求与任务进度，一站式掌握。</p>
      </div>
      <RouterLink to="/receiver/datasets" class="link">立即上传数据集</RouterLink>
    </header>
    <ul class="metrics">
      <li>
        <span class="label">我的数据集</span>
        <strong>{{ datasetCount }}</strong>
      </li>
      <li>
        <span class="label">待发送方确认</span>
        <strong>{{ pendingRequestCount }}</strong>
      </li>
      <li>
        <span class="label">已完成任务</span>
        <strong>{{ completedSessions }}</strong>
      </li>
    </ul>

    <div class="grid">
      <article class="panel">
        <header>
          <h4>最新数据集</h4>
          <RouterLink to="/receiver/datasets">管理</RouterLink>
        </header>
        <template v-if="latestDataset">
          <p class="title">{{ latestDataset.name }}</p>
          <p>规模：{{ latestDataset.size.toLocaleString("en-US") }}</p>
          <p>模数：{{ latestDataset.modulus.toLocaleString("en-US") }}</p>
          <p>更新时间：{{ latestDataset.updatedAt }}</p>
        </template>
        <p v-else class="empty">尚未上传数据集</p>
      </article>

      <article class="panel">
        <header>
          <h4>待确认请求</h4>
          <RouterLink to="/receiver/requests">查看</RouterLink>
        </header>
        <template v-if="nextPending">
          <p class="title">发送方：{{ nextPending.counterparty }}</p>
          <p>阈值：{{ nextPending.threshold || "未指定" }}</p>
          <p>状态：待发送方确认</p>
          <p>创建时间：{{ nextPending.createdAt }}</p>
        </template>
        <p v-else class="empty">暂无待确认的请求</p>
      </article>

      <article class="panel">
        <header>
          <h4>最新求交任务</h4>
          <RouterLink to="/receiver/results">结果记录</RouterLink>
        </header>
        <template v-if="latestSession">
          <p class="title">{{ latestSession.name }}</p>
          <p>阈值：{{ latestSession.threshold.toLocaleString("en-US") }}</p>
          <p>状态：{{ statusText(latestSession.status) }}</p>
          <p>更新时间：{{ latestSession.updatedAt }}</p>
        </template>
        <p v-else class="empty">暂无已执行的任务</p>
      </article>
    </div>
  </section>
</template>

<script setup lang="ts">
import { computed, onMounted } from "vue";
import { useDatasetsStore } from "../../stores/datasets";
import { useRequestsStore } from "../../stores/requests";
import { useSessionsStore } from "../../stores/sessions";
import { useAuthStore } from "../../stores/auth";

const datasetsStore = useDatasetsStore();
const requestsStore = useRequestsStore();
const sessionsStore = useSessionsStore();
const auth = useAuthStore();
auth.restore();

onMounted(async () => {
  await Promise.all([
    datasetsStore.loadSummaries(true),
    requestsStore.reload(),
    sessionsStore.loadSummaries(true)
  ]);
});

const username = computed(() => auth.user?.username ?? "");

const myDatasets = computed(() =>
  datasetsStore.summaries.filter((item) => item.owner === username.value)
);
const datasetCount = computed(() => myDatasets.value.length);
const latestDataset = computed(() => {
  if (!myDatasets.value.length) return null;
  return [...myDatasets.value].sort((a, b) => b.updatedAt.localeCompare(a.updatedAt))[0];
});

const myRequests = computed(() =>
  requestsStore.items.filter((item) => item.applicant === username.value)
);
const pendingRequestCount = computed(() =>
  myRequests.value.filter((item) => item.status === "pending-sender").length
);
const nextPending = computed(() => {
  const pending = myRequests.value
    .filter((item) => item.status === "pending-sender")
    .sort((a, b) => b.createdAt.localeCompare(a.createdAt));
  return pending[0] ?? null;
});

const sessionsSorted = computed(() =>
  [...sessionsStore.summaries].sort((a, b) => b.updatedAt.localeCompare(a.updatedAt))
);
const latestSession = computed(() => sessionsSorted.value[0] ?? null);
const completedSessions = computed(() =>
  sessionsStore.summaries.filter((item) => item.status === "completed").length
);

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
    default:
      return status;
  }
};
</script>

<style scoped>
.card {
  background: #fff;
  border-radius: 16px;
  padding: 24px;
  box-shadow: 0 12px 32px rgba(24, 43, 90, 0.1);
  display: flex;
  flex-direction: column;
  gap: 24px;
}

.head {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.link {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  padding: 8px 12px;
  border-radius: 10px;
  background: linear-gradient(135deg, #5a78f0, #4cb4ff);
  color: #fff;
  font-weight: 600;
}

.metrics {
  list-style: none;
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
  gap: 16px;
  padding: 0;
  margin: 0;
}

.metrics li {
  background: #f5f8ff;
  border-radius: 12px;
  padding: 16px;
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.metrics .label {
  font-size: 12px;
  color: #6b7aa6;
}

.metrics strong {
  font-size: 20px;
  color: #22315d;
}

.grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
  gap: 16px;
}

.panel {
  background: #f8faff;
  border-radius: 12px;
  padding: 18px;
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.panel header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  font-size: 13px;
  color: #51628f;
}

.panel header a {
  font-size: 12px;
  color: #2f6bff;
}

.panel .title {
  font-weight: 600;
  color: #1c2f6d;
}

.panel .empty {
  color: #8a97b8;
  font-size: 13px;
}
</style>
