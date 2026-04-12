<template>
  <section class="card">
    <h3>安全设置</h3>
    <form @submit.prevent="handleChangePassword" class="form">
      <label>
        当前密码
        <input v-model.trim="form.oldPassword" type="password" required />
      </label>
      <label>
        新密码
        <input v-model.trim="form.newPassword" type="password" required />
      </label>
      <button type="submit" class="primary" :disabled="passwordSaving">
        {{ passwordSaving ? "修改中..." : "修改密码" }}
      </button>
    </form>
    <div class="twofa">
      <h4>双因素认证</h4>
      <p>提升账号安全，可结合手机令牌使用。</p>
      <button @click="handleToggle2FA" class="primary" type="button" :disabled="twofaProcessing">
        <template v-if="twofaProcessing">处理中...</template>
        <template v-else>{{ twoFAEnabled ? '关闭二次验证' : '开启二次验证' }}</template>
      </button>
    </div>
  </section>
</template>

<script setup lang="ts">
import { reactive, ref, watch } from "vue";
import { useToast } from "../../services/toast";
import { useAuthStore } from "../../stores/auth";

const toast = useToast();
const auth = useAuthStore();
auth.restore();

const twoFAEnabled = ref(false);

watch(
  () => auth.user?.twoFactorEnabled,
  (enabled) => {
    twoFAEnabled.value = !!enabled;
  },
  { immediate: true }
);

const form = reactive({
  oldPassword: "",
  newPassword: ""
});

const passwordSaving = ref(false);
const twofaProcessing = ref(false);

const handleChangePassword = async () => {
  if (!form.oldPassword || !form.newPassword) return;
  passwordSaving.value = true;
  try {
    await auth.changePassword({
      oldPassword: form.oldPassword,
      newPassword: form.newPassword
    });
    form.oldPassword = "";
    form.newPassword = "";
  } catch (err) {
    toast.push((err as Error).message ?? "修改失败", "error");
  } finally {
    passwordSaving.value = false;
  }
};

const handleToggle2FA = async () => {
  twofaProcessing.value = true;
  try {
    const secret = await auth.toggleTwoFactor(!twoFAEnabled.value);
    if (secret) {
      toast.push(`请妥善保存二次验证密钥：${secret}`, "info");
    }
  } catch (err) {
    toast.push((err as Error).message ?? "操作失败", "error");
  } finally {
    twofaProcessing.value = false;
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
  display: flex;
  flex-direction: column;
  gap: 24px;
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

.twofa {
  display: flex;
  flex-direction: column;
  gap: 8px;
}
</style>
