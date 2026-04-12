<template>
  <div class="grid">
    <section class="card">
      <h3>运行概览</h3>
      <div class="metrics">
        <div class="metric">
          <span class="label">数据集数量</span>
          <span class="value">{{ metrics.datasets }}</span>
        </div>
        <div class="metric">
          <span class="label">执行中任务</span>
          <span class="value">{{ metrics.running }}</span>
        </div>
        <div class="metric">
          <span class="label">阈值达成率</span>
          <span class="value">{{ metrics.thresholdRate }}%</span>
        </div>
        <div class="metric">
          <span class="label">告警数量</span>
          <span class="value">{{ metrics.alerts }}</span>
        </div>
      </div>
    </section>
    <section class="card">
      <h3>最近求交任务</h3>
      <ul class="session-list">
        <li v-for="session in sessions" :key="session.id">
          <div>
            <strong>{{ session.name }}</strong>
            <p>规模 {{ session.size }} · 阈值 {{ session.threshold }} · 模数 {{ session.modulus }}</p>
          </div>
          <router-link :to="{ name: 'sessions', query: { focus: session.id } }" class="status" :class="session.status">
            {{ statusLabel(session.status) }}
          </router-link>
        </li>
      </ul>
    </section>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted } from "vue";
import { useSessionsStore } from "../stores/sessions";
import { useDatasetsStore } from "../stores/datasets";

const sessionsStore = useSessionsStore();
const datasetsStore = useDatasetsStore();

onMounted(() => {
  sessionsStore.loadSummaries();
  datasetsStore.loadSummaries();
});

const sessions = computed(() =>
  sessionsStore.summaries.slice(0, 5).map((item) => ({
    id: item.id,
    name: item.name,
    size: item.size.toLocaleString("en-US"),
    threshold: item.threshold.toLocaleString("en-US"),
    modulus: item.modulus.toLocaleString("en-US"),
    status: item.status
  }))
);

const metrics = computed(() => {
  const total = sessionsStore.summaries.length;
  const running = sessionsStore.summaries.filter((item) => item.status === "running").length;
  const completed = sessionsStore.summaries.filter((item) => item.status === "completed").length;
  const alerts = sessionsStore.summaries.filter((item) => item.status === "failed").length;
  return {
    datasets: datasetsStore.summaries.length,
    running,
    thresholdRate: total ? Math.round((completed / total) * 100) : 0,
    alerts
  };
});

const statusLabel = (status: string) => {
  switch (status) {
    case "running":
      return "执行中";
    case "completed":
      return "已完成";
    case "failed":
      return "失败";
    default:
      return status;
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

.metrics {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
  gap: 16px;
  margin-top: 16px;
}

.metric {
  border-radius: 12px;
  padding: 16px;
  background: #eef3ff;
}

.label {
  display: block;
  font-size: 13px;
  color: #5f6f92;
}

.value {
  font-size: 24px;
  font-weight: 600;
  margin-top: 8px;
}

.session-list {
  margin: 16px 0 0;
  padding: 0;
  list-style: none;
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.session-list li {
  display: flex;
  justify-content: space-between;
  align-items: center;
  border: 1px solid #e2e8f8;
  border-radius: 12px;
  padding: 16px;
}

.session-list p {
  margin: 8px 0 0;
  color: #65749c;
  font-size: 13px;
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
</style>
