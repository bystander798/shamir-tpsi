<template>
  <section class="card">
    <h3>基础信息</h3>
    <form @submit.prevent="handleSubmit" class="form">
      <label>
        姓名
        <input v-model.trim="profile.name" type="text" required />
      </label>
      <label>
        所属单位
        <input v-model.trim="profile.organization" type="text" />
      </label>
      <label>
        邮箱
        <input v-model.trim="profile.email" type="email" />
      </label>
      <label>
        联系电话
        <input v-model.trim="profile.phone" type="tel" />
      </label>
      <div class="actions">
        <button type="submit" class="primary" :disabled="saving">
          {{ saving ? "保存中..." : "保存信息" }}
        </button>
      </div>
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

const profile = reactive({
  name: "",
  organization: "",
  email: "",
  phone: ""
});

watch(
  () => auth.user,
  (user) => {
    profile.name = user?.name ?? "";
    profile.organization = user?.organization ?? "";
    profile.email = user?.email ?? "";
    profile.phone = user?.phone ?? "";
  },
  { immediate: true }
);

const saving = ref(false);

const handleSubmit = async () => {
  if (!auth.user) return;
  saving.value = true;
  try {
    await auth.updateProfile({
      name: profile.name,
      organization: profile.organization,
      email: profile.email,
      phone: profile.phone
    });
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
  max-width: 480px;
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

.actions {
  display: flex;
  justify-content: flex-start;
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
</style>
