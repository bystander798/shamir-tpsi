<template>
  <div class="overlay" @click.self="$emit('close')">
    <aside class="drawer" v-if="detail">
      <header>
        <h3>{{ detail.name }}</h3>
        <button class="close" @click="$emit('close')">×</button>
      </header>
      <section class="grid">
        <div>
          <span class="label">集合规模</span>
          <span class="value">{{ detail.size.toLocaleString("en-US") }}</span>
        </div>
        <div>
          <span class="label">阈值</span>
          <span class="value">{{ detail.threshold.toLocaleString("en-US") }}</span>
        </div>
        <div>
          <span class="label">阈值占比</span>
          <span class="value">{{ detail.thresholdRatio }}</span>
        </div>
        <div>
          <span class="label">模数</span>
          <span class="value">{{ detail.modulus.toLocaleString("en-US") }}</span>
        </div>
      </section>
      <section class="metrics">
        <h4>阶段耗时</h4>
        <ul>
          <li>离线阶段：{{ formatMs(detail.metrics.offlineMs) }}</li>
          <li>在线阶段：{{ formatMs(detail.metrics.onlineMs) }}</li>
          <li>汇总阶段：{{ formatMs(detail.metrics.finalizeMs) }}</li>
          <li>通信总量：{{ detail.metrics.communicationBytes.toLocaleString("en-US") }} bytes</li>
        </ul>
      </section>
      <section class="intersection">
        <h4>交集摘要</h4>
        <p>阈值达成：{{ detail.thresholdReached ? '是' : '否' }}</p>
        <p>交集大小：{{ detail.intersectionSize.toLocaleString("en-US") }}</p>
        <p v-if="previewIntersection.length">样本：{{ previewIntersection.join(", ") }}</p>
        <button v-if="canDownload" class="download" @click="downloadIntersection">下载交集明细</button>
        <p v-else class="hint">发送方无权下载交集明细</p>
      </section>
      <section class="logs">
        <h4>执行日志</h4>
        <ul>
          <li v-for="log in detail.logs" :key="log.time">
            <span class="time">{{ log.time }}</span>
            <span class="level" :class="log.level">{{ log.level.toUpperCase() }}</span>
            <span>{{ log.message }}</span>
          </li>
        </ul>
      </section>
    </aside>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted } from "vue";
import { useSessionsStore } from "../../stores/sessions";
import { useAuthStore } from "../../stores/auth";
import { downloadSessionIntersection } from "../../api/client";
import { useToast } from "../../services/toast";

const props = defineProps<{ sessionId: string }>();
const store = useSessionsStore();
const auth = useAuthStore();
const toast = useToast();

onMounted(() => {
  store.loadDetail(props.sessionId).catch((err) => {
    toast.push((err as Error).message || "加载任务详情失败", "error");
  });
});

auth.restore();

const detail = computed(() => store.details[props.sessionId]);
const canDownload = computed(() => {
  const role = auth.user?.role;
  return role === "receiver" || role === "admin";
});
const previewIntersection = computed(() => detail.value?.intersection?.slice(0, 10) ?? []);

const formatMs = (value: number) => `${value.toFixed(1)} ms`;

const downloadIntersection = async () => {
  if (!canDownload.value) {
    toast.push("无权限下载交集", "error");
    return;
  }
  const numbers = detail.value?.intersection;
  if (numbers && numbers.length) {
    const lines = ["element", ...numbers.map((value) => String(value))];
    const blob = new Blob([`${lines.join("\n")}\n`], { type: "text/csv;charset=utf-8" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = `${props.sessionId}-intersection.csv`;
    a.click();
    URL.revokeObjectURL(url);
    return;
  }
  try {
    const { data } = await downloadSessionIntersection(props.sessionId);
    const url = URL.createObjectURL(data);
    const a = document.createElement("a");
    a.href = url;
    a.download = `${props.sessionId}-intersection.csv`;
    a.click();
    URL.revokeObjectURL(url);
  } catch (err) {
    toast.push((err as Error).message || "下载失败", "error");
  }
};
</script>

<style scoped>
.overlay {
  position: fixed;
  inset: 0;
  background: rgba(16, 28, 60, 0.45);
  z-index: 1600;
  display: flex;
  justify-content: flex-end;
}

.drawer {
  width: min(460px, 90vw);
  background: #fff;
  padding: 24px;
  display: flex;
  flex-direction: column;
  gap: 16px;
  box-shadow: -16px 0 40px rgba(16, 28, 60, 0.3);
}

header {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.close {
  border: none;
  background: transparent;
  font-size: 24px;
  cursor: pointer;
}

.grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 12px;
}

.label {
  display: block;
  font-size: 12px;
  color: #5f6f92;
}

.value {
  font-weight: 600;
  font-size: 16px;
}

.metrics,
.intersection,
.logs {
  background: #f5f8ff;
  border-radius: 12px;
  padding: 16px;
}

.hint {
  margin: 8px 0 0;
  font-size: 12px;
  color: #7a88ad;
}

.metrics ul,
.logs ul {
  margin: 0;
  padding-left: 18px;
}

.download {
  border: none;
  padding: 8px 12px;
  border-radius: 8px;
  background: linear-gradient(135deg, #5a78f0, #4cb4ff);
  color: #fff;
  cursor: pointer;
}

.logs li {
  display: flex;
  gap: 12px;
  font-size: 13px;
}

.time {
  color: #7a88ad;
}

.level.info {
  color: #2f6bff;
}

.level.warn {
  color: #ff9f40;
}

.level.error {
  color: #ff5e68;
}
</style>
