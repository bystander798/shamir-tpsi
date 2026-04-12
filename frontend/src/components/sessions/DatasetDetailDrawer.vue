<template>
  <div class="overlay" @click.self="$emit('close')">
    <aside class="drawer" v-if="detail">
      <header>
        <h3>{{ detail.name }}</h3>
        <button class="close" @click="$emit('close')">×</button>
      </header>
      <section class="grid">
        <div>
          <span class="label">规模</span>
          <span class="value">{{ detail.size.toLocaleString("en-US") }}</span>
        </div>
        <div>
          <span class="label">上传方</span>
          <span class="value">{{ detail.owner || "未知" }}</span>
        </div>
        <div>
          <span class="label">模数</span>
          <span class="value">{{ detail.modulus.toLocaleString("en-US") }}</span>
        </div>
        <div>
          <span class="label">版本</span>
          <span class="value">{{ detail.version }}</span>
        </div>
        <div>
          <span class="label">更新时间</span>
          <span class="value">{{ detail.updatedAt }}</span>
        </div>
      </section>
      <section v-if="detail.stats" class="stats">
        <h4>统计信息</h4>
        <ul>
          <li>最小值：{{ detail.stats.min }}</li>
          <li>最大值：{{ detail.stats.max }}</li>
          <li>重复元素：{{ detail.stats.duplicates }}</li>
          <li v-if="detail.stats.hashed">集合哈希：{{ detail.stats.hashed }}</li>
        </ul>
      </section>
      <section v-if="detail.sample && detail.sample.length" class="sample">
        <h4>样本预览</h4>
        <p>{{ detail.sample.join(", ") }}</p>
      </section>
    </aside>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted } from "vue";
import { useDatasetsStore } from "../../stores/datasets";

const props = defineProps<{ datasetId: string }>();
const store = useDatasetsStore();

onMounted(() => {
  store.loadDetail(props.datasetId);
});

const detail = computed(() => store.details[props.datasetId]);
</script>

<style scoped>
.overlay {
  position: fixed;
  inset: 0;
  background: rgba(16, 28, 60, 0.45);
  display: flex;
  justify-content: flex-end;
  z-index: 1500;
}

.drawer {
  width: min(420px, 90vw);
  background: #fff;
  box-shadow: -20px 0 48px rgba(16, 28, 60, 0.3);
  padding: 24px;
  display: flex;
  flex-direction: column;
  gap: 16px;
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

.stats,
.sample {
  background: #f5f8ff;
  border-radius: 12px;
  padding: 16px;
}

.stats ul {
  margin: 0;
  padding-left: 18px;
}
</style>
