<template>
  <div class="card">
    <header class="head">
      <div>
        <h3>求交结果</h3>
        <p>查看历史 TPSI 任务，下载交集明细，快速跳转到任务详情。</p>
      </div>
      <RouterLink class="primary" to="/sessions">前往任务详情</RouterLink>
    </header>
    <table v-if="rows.length">
      <thead>
        <tr>
          <th>名称</th>
          <th>规模</th>
          <th>阈值</th>
          <th>状态</th>
          <th>更新时间</th>
          <th>操作</th>
        </tr>
      </thead>
      <tbody>
        <tr v-for="session in rows" :key="session.id">
          <td>{{ session.name }}</td>
          <td>{{ session.size.toLocaleString("en-US") }}</td>
          <td>{{ session.threshold.toLocaleString("en-US") }}</td>
          <td><span :class="['status', session.status]">{{ statusText(session.status) }}</span></td>
          <td>{{ session.updatedAt }}</td>
          <td class="ops">
            <button @click="openDetail(session.id)">详情</button>
          </td>
        </tr>
      </tbody>
    </table>
    <p v-else class="empty">暂未有求交任务，请先完成一次协同求交。</p>
  </div>
  <SessionDetailDrawer v-if="detailId" :session-id="detailId" @close="detailId = ''" />
</template>

<script setup lang="ts">
import { computed, onMounted, ref } from "vue";
import { useSessionsStore } from "../../stores/sessions";
import SessionDetailDrawer from "../../components/sessions/SessionDetailDrawer.vue";

const sessionsStore = useSessionsStore();
const detailId = ref("");

onMounted(async () => {
  await sessionsStore.loadSummaries(true);
});

const rows = computed(() =>
  [...sessionsStore.summaries].sort((a, b) => b.updatedAt.localeCompare(a.updatedAt))
);

const openDetail = (id: string) => {
  detailId.value = id;
};

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
  gap: 16px;
}

.head {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.primary {
  border: none;
  padding: 8px 12px;
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

.ops button {
  border: none;
  padding: 6px 10px;
  border-radius: 8px;
  background: #eef3ff;
  cursor: pointer;
}

.status {
  padding: 4px 10px;
  border-radius: 999px;
  font-size: 12px;
  font-weight: 600;
}

.status.running {
  background: rgba(47, 107, 255, 0.16);
  color: #2f6bff;
}

.status.completed {
  background: rgba(59, 165, 92, 0.16);
  color: #2e8b48;
}

.status.failed {
  background: rgba(255, 94, 104, 0.16);
  color: #c74650;
}

.status.threshold-not-met {
  background: rgba(255, 176, 32, 0.16);
  color: #c77b00;
}

.empty {
  color: #8a97b8;
}
</style>
