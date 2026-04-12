<template>
  <section class="card">
    <h3>通知偏好</h3>
    <form @submit.prevent="handleSubmit" class="form">
      <label>
        <input type="checkbox" v-model="prefs.approvals" /> 审批提醒
      </label>
      <label>
        <input type="checkbox" v-model="prefs.jobs" /> 求交任务完成通知
      </label>
      <label>
        <input type="checkbox" v-model="prefs.alerts" /> 异常告警
      </label>
      <button type="submit" class="primary" :disabled="saving">
        {{ saving ? "保存中..." : "保存设置" }}
      </button>
    </form>
  </section>
</template>

<script setup lang="ts">
import { reactive, ref, watch } from "vue";
import { useAuthStore } from "../../stores/auth";
import { useToast } from "../../services/toast";

const auth = useAuthStore();
const toast = useToast();
auth.restore();

const prefs = reactive({
  approvals: true,
  jobs: true,
  alerts: true
});

watch(
  () => auth.user?.notifications,
  (notifications) => {
    prefs.approvals = notifications?.approvals ?? true;
    prefs.jobs = notifications?.jobs ?? true;
    prefs.alerts = notifications?.alerts ?? true;
  },
  { immediate: true }
);

const saving = ref(false);

const handleSubmit = async () => {
  saving.value = true;
  try {
    await auth.updateNotifications({ ...prefs });
  } catch (err) {
    toast.push((err as Error).message ?? "保存失败", "error");
  } finally {
    saving.value = false;
  }
};
</script>

<style scoped>
.card {
  background: #fff;
  border-radius: 16px;
  padding: 24px;
  box-shadow: 0 12px 32px rgba(24, 43, 90, 0.1);
  max-width: 360px;
}

.form {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

label {
  display: flex;
  align-items: center;
  gap: 10px;
  font-size: 14px;
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
