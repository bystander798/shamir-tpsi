<template>
  <div class="layout">
    <section class="card upload-card">
      <header class="head">
        <div>
          <h3>数据集管理</h3>
          <p>上传真实集合文件，解析统计信息后用于求交流程。</p>
        </div>
        <button class="ghost" type="button" @click="refresh" :disabled="refreshing">
          {{ refreshing ? "刷新中..." : "刷新列表" }}
        </button>
      </header>
      <form class="upload-form" @submit.prevent="submitUpload">
        <label>
          数据集名称
          <input v-model.trim="uploadName" placeholder="例如：接收方用户集合" required />
        </label>
        <label>
          模数
          <input v-model.number="uploadModulus" type="number" min="2" required />
        </label>
        <div class="file-row">
          <label class="upload">
            选择文件
            <input type="file" accept=".csv,.json,.txt,text/plain" @change="handleFile" hidden />
          </label>
          <span class="file-info" v-if="fileInfo">{{ fileInfo }}</span>
          <span class="file-info" v-if="elementsCount">
            元素：{{ elementsCount.toLocaleString("en-US") }}
          </span>
          <span class="file-info warn" v-if="isParsing">正在解析...</span>
        </div>
        <button class="primary" type="submit" :disabled="!canSubmit">
          {{ submitting ? "上传中..." : "提交上传" }}
        </button>
      </form>
      <section v-if="elementsCount" class="analysis">
        <h4>本地解析结果</h4>
        <ul class="stats" v-if="stats">
          <li>最小值：{{ stats.min }}</li>
          <li>最大值：{{ stats.max }}</li>
          <li>重复元素：{{ stats.duplicates }}</li>
        </ul>
        <p class="preview" v-if="preview.length">
          预览（前 {{ preview.length }} 项）：
          <span>{{ preview.join(", ") }}</span>
        </p>
      </section>
    </section>

    <section class="card table-card">
      <header class="head">
        <div>
          <h3>已上传数据集</h3>
          <p>包含上传方、规模、模数等信息，可在审批及求交中引用。</p>
        </div>
      </header>
      <table v-if="datasets.length">
        <thead>
          <tr>
            <th>名称</th>
            <th>上传方</th>
            <th>规模</th>
            <th>模数</th>
            <th>版本</th>
            <th>更新时间</th>
            <th>操作</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="ds in datasets" :key="ds.id">
            <td>{{ ds.name }}</td>
            <td>{{ ds.owner || "未知" }}</td>
            <td>{{ ds.size.toLocaleString("en-US") }}</td>
            <td>{{ ds.modulus.toLocaleString("en-US") }}</td>
            <td>{{ ds.version }}</td>
            <td>{{ ds.updatedAt }}</td>
            <td class="ops">
              <button type="button" @click="openDetail(ds.id)">详情</button>
              <button type="button" @click="exportSample(ds.id)">导出样本</button>
            </td>
          </tr>
        </tbody>
      </table>
      <p v-else>暂无数据集，请先完成一次上传。</p>
    </section>

    <DatasetDetailDrawer v-if="detailId" :dataset-id="detailId" @close="detailId = ''" />
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, ref } from "vue";
import DatasetDetailDrawer from "../components/sessions/DatasetDetailDrawer.vue";
import { useDatasetsStore } from "../stores/datasets";
import { useAuthStore } from "../stores/auth";
import { useToast } from "../services/toast";
import { useDatasetUpload, DEFAULT_MODULUS } from "../composables/useDatasetUpload";

const datasetsStore = useDatasetsStore();
const auth = useAuthStore();
auth.restore();
const toast = useToast();

const {
  uploadName,
  uploadModulus,
  elements,
  elementsCount,
  preview,
  stats,
  fileInfo,
  sourceFile,
  isParsing,
  handleFile,
  reset,
  setDefaultName
} = useDatasetUpload();

const detailId = ref("");
const submitting = ref(false);
const refreshing = ref(false);

onMounted(async () => {
  await datasetsStore.loadSummaries(true);
  setDefaultName(`${auth.user?.name || "用户"}数据集`);
});

const datasets = computed(() => datasetsStore.summaries);
const canSubmit = computed(() => {
  return (
    !submitting.value &&
    !isParsing.value &&
    elementsCount.value > 0 &&
    Boolean(uploadName.value) &&
    !!sourceFile.value
  );
});

const submitUpload = async () => {
  if (!canSubmit.value) {
    toast.push("请先完成文件解析", "error");
    return;
  }
  submitting.value = true;
  try {
    const detail = await datasetsStore.create({
      name: uploadName.value || `dataset-${Date.now()}`,
      owner: auth.user?.username || auth.user?.name || "anonymous",
      modulus: Number(uploadModulus.value) || DEFAULT_MODULUS,
      elements: elements.value,
      file: sourceFile.value
    });
    await datasetsStore.loadSummaries(true);
    toast.push("数据集上传成功", "success");
    detailId.value = detail.id;
    reset();
    uploadName.value = "";
    uploadModulus.value = DEFAULT_MODULUS;
    setDefaultName(`${auth.user?.name || "用户"}数据集`);
  } catch (err) {
    toast.push((err as Error).message || "上传失败", "error");
  } finally {
    submitting.value = false;
  }
};

const openDetail = (id: string) => {
  detailId.value = id;
};

const exportSample = (id: string) => {
  datasetsStore.exportSample(id);
};

const refresh = async () => {
  refreshing.value = true;
  try {
    await datasetsStore.loadSummaries(true);
  } finally {
    refreshing.value = false;
  }
};
</script>

<style scoped>
.layout {
  display: flex;
  flex-direction: column;
  gap: 24px;
}

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

.head p {
  margin: 6px 0 0;
  color: #5f6f92;
  font-size: 13px;
}

.upload-form {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
  gap: 16px;
  align-items: end;
}

.upload {
  background: linear-gradient(135deg, #5a78f0, #4cb4ff);
  color: #fff;
  padding: 10px 16px;
  border-radius: 10px;
  cursor: pointer;
  font-weight: 600;
  display: inline-flex;
  justify-content: center;
}

.upload-card h4 {
  margin: 0;
}

.file-row {
  display: flex;
  align-items: center;
  gap: 12px;
  flex-wrap: wrap;
}

.file-info {
  font-size: 13px;
  color: #5f6f92;
}

.file-info.warn {
  color: #ff9f40;
}

.primary {
  border: none;
  padding: 10px 16px;
  border-radius: 10px;
  background: linear-gradient(135deg, #5a78f0, #4cb4ff);
  color: #fff;
  font-weight: 600;
  cursor: pointer;
}

.primary:disabled {
  opacity: 0.6;
  cursor: not-allowed;
}

.ghost {
  border: 1px solid rgba(90, 120, 240, 0.35);
  background: transparent;
  color: #3141a0;
  padding: 8px 16px;
  border-radius: 10px;
  cursor: pointer;
}

.analysis {
  background: #f5f8ff;
  border-radius: 12px;
  padding: 16px;
  display: grid;
  gap: 12px;
}

.analysis h4 {
  margin: 0;
}

.stats {
  display: flex;
  gap: 24px;
  padding: 0;
  margin: 0;
  list-style: none;
  color: #455379;
}

.preview {
  margin: 0;
  color: #5a6d99;
  font-size: 13px;
}

.table-card {
  overflow-x: auto;
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
  white-space: nowrap;
}

.ops {
  display: flex;
  gap: 8px;
}

.ops button {
  border: none;
  padding: 6px 10px;
  border-radius: 8px;
  background: #eef3ff;
  cursor: pointer;
}
</style>
