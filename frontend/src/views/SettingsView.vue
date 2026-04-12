<template>
  <section class="card">
    <header>
      <h3>系统参数</h3>
      <p>配置默认模数、阈值策略与安全阈值。保存后将影响新任务。</p>
    </header>
    <form @submit.prevent="handleSubmit" class="form">
      <label>
        默认模数
        <input v-model.number="form.modulus" type="number" min="2" required />
      </label>
      <label>
        阈值占比默认值
        <input v-model.number="form.thresholdRatio" type="number" step="0.01" min="0" max="1" required />
      </label>
      <label>
        最大集合规模
        <input v-model.number="form.maxSetSize" type="number" min="1" required />
      </label>
      <label>
        日志保留天数
        <input v-model.number="form.logRetention" type="number" min="1" />
      </label>
      <button class="primary" type="submit">保存配置</button>
    </form>
  </section>
</template>

<script setup lang="ts">
import { reactive } from "vue";
import { useToast } from "../services/toast";

const toast = useToast();

const form = reactive({
  modulus: 104857601,
  thresholdRatio: 0.95,
  maxSetSize: 1 << 20,
  logRetention: 30
});

const handleSubmit = () => {
  // TODO: 调用后端保存接口
  toast.push("系统参数已保存", "success");
};
</script>

<style scoped>
.card {
  background: #fff;
  border-radius: 16px;
  padding: 24px;
  box-shadow: 0 12px 32px rgba(24, 43, 90, 0.1);
  max-width: 480px;
}

header {
  margin-bottom: 16px;
}

header h3 {
  margin: 0;
}

header p {
  color: #5f6f92;
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

input {
  padding: 12px;
  border: 1px solid #d6ddf2;
  border-radius: 10px;
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
</style>
