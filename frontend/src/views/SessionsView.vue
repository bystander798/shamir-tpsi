<template>
  <div class="card">
    <header class="head">
      <div>
        <h3>求交任务</h3>
        <p>管理离线/在线/汇总阶段，查看真实执行耗时与阈值情况。</p>
      </div>
      <RouterLink class="primary" :to="{ name: 'receiver-requests' }">新建求交</RouterLink>
    </header>
    <table>
      <thead>
        <tr>
          <th>名称</th>
          <th>集合规模</th>
          <th>阈值</th>
          <th>模数</th>
          <th>状态</th>
          <th>最新耗时</th>
          <th>操作</th>
        </tr>
      </thead>
      <tbody>
        <tr v-for="session in rows" :key="session.id">
          <td>
            <strong>{{ session.name }}</strong>
            <div class="meta">更新：{{ session.updatedAt }}</div>
          </td>
          <td>{{ session.size.toLocaleString("en-US") }}</td>
          <td>
            {{ session.threshold.toLocaleString("en-US") }}
            <span v-if="session.thresholdReached === true" class="badge badge-ok">达标</span>
            <span v-else-if="session.thresholdReached === false" class="badge badge-warn">未达</span>
          </td>
          <td>{{ session.modulus.toLocaleString("en-US") }}</td>
          <td><span :class="['status', session.status]">{{ statusLabel(session.status) }}</span></td>
          <td>{{ session.latestDuration }}</td>
          <td class="ops">
            <button @click="viewDetails(session.id)">详情</button>
            <button @click="rerun(session.id)">重跑</button>
          </td>
        </tr>
      </tbody>
    </table>
  </div>
  <SessionDetailDrawer v-if="detailId" :session-id="detailId" @close="detailId = ''" />
</template>

<script setup lang="ts">
import { computed, onMounted, ref } from "vue";
import { useSessionsStore } from "../stores/sessions";
import SessionDetailDrawer from "../components/sessions/SessionDetailDrawer.vue";

const sessionsStore = useSessionsStore();
const detailId = ref("");

onMounted(() => {
  sessionsStore.loadSummaries();
});

const rows = computed(() =>
  sessionsStore.summaries.map((summary) => {
    const detail = sessionsStore.details[summary.id];
    const totalDuration = detail
      ? detail.metrics.offlineMs + detail.metrics.onlineMs + detail.metrics.finalizeMs
      : null;
    return {
      ...summary,
      latestDuration: totalDuration !== null ? formatDuration(totalDuration) : "待查看",
      thresholdReached: detail?.thresholdReached ?? null
    };
  })
);

const formatDuration = (ms: number) => {
  if (ms >= 1000) {
    return `${(ms / 1000).toFixed(2)} s`;
  }
  return `${ms.toFixed(1)} ms`;
};

const statusLabel = (status: string) => {
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

const viewDetails = (id: string) => {
  detailId.value = id;
};

const rerun = (id: string) => {
  sessionsStore.rerun(id);
};
</script>

<style scoped>
.card {
  background: #fff;
  border-radius: 16px;
  padding: 24px;
  box-shadow: 0 12px 32px rgba(24, 43, 90, 0.1);
}

.head {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 16px;
}

.head p {
  margin: 6px 0 0;
  color: #5f6f92;
  font-size: 13px;
}

.primary {
  border: none;
  padding: 10px 16px;
  border-radius: 10px;
  background: linear-gradient(135deg, #5a78f0, #4cb4ff);
  color: #fff;
  font-weight: 600;
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

.meta {
  color: #8a97b8;
  font-size: 12px;
}

.status {
  padding: 6px 12px;
  border-radius: 999px;
  font-size: 12px;
  font-weight: 600;
}

.status.running {
  background: rgba(47, 107, 255, 0.15);
  color: #2f6bff;
}

.status.completed {
  background: rgba(59, 165, 92, 0.15);
  color: #3ba55c;
}

.status.failed {
  background: rgba(255, 94, 104, 0.15);
  color: #ff5e68;
}

.status.threshold-not-met {
  background: rgba(255, 176, 32, 0.16);
  color: #c77b00;
}

.badge {
  margin-left: 8px;
  padding: 2px 6px;
  border-radius: 999px;
  font-size: 11px;
  font-weight: 600;
}

.badge-ok {
  background: rgba(59, 165, 92, 0.16);
  color: #2e8b48;
}

.badge-warn {
  background: rgba(255, 94, 104, 0.16);
  color: #c74650;
}

.ops button {
  margin-right: 8px;
  border: none;
  padding: 6px 10px;
  border-radius: 8px;
  background: #eef3ff;
  cursor: pointer;
}
</style>
