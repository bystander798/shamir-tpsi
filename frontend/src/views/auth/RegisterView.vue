<template>
  <div class="auth-form">
    <h3>注册新账号</h3>
    <p class="hint">填写信息并选择角色，管理员审核后即可使用全流程功能。</p>
    <form @submit.prevent="handleSubmit">
      <label>
        登录账号
        <input v-model.trim="username" type="text" required placeholder="请输入账号" />
      </label>
      <label>
        登录密码
        <input v-model.trim="password" type="password" required placeholder="请输入密码" />
      </label>
      <label>
        确认密码
        <input v-model.trim="passwordConfirm" type="password" required placeholder="再次输入密码" />
      </label>
      <label>
        所属单位
        <input v-model.trim="organization" type="text" placeholder="请输入单位或部门" />
      </label>
      <label>
        选择角色
        <select v-model="role">
          <option value="receiver">接收方</option>
          <option value="sender">发送方</option>
        </select>
      </label>
      <div class="actions">
        <button type="submit" class="submit">注 册</button>
        <RouterLink class="link" :to="{ name: 'login', query: { role } }">已有账号？返回登录</RouterLink>
      </div>
    </form>
  </div>
</template>

<script setup lang="ts">
import { ref } from "vue";
import { useRouter, useRoute } from "vue-router";
import { useAuthStore, type UserRole } from "../../stores/auth";

const router = useRouter();
const route = useRoute();
const auth = useAuthStore();
auth.restore();

const username = ref("");
const password = ref("");
const passwordConfirm = ref("");
const organization = ref("");
const role = ref<UserRole>((route.query.role as UserRole) || "receiver");

const handleSubmit = async () => {
  if (password.value !== passwordConfirm.value) {
    window.alert("两次输入的密码不一致");
    return;
  }
  try {
    await auth.register({ username: username.value, password: password.value, organization: organization.value, role: role.value });
    router.push(auth.defaultRouteForRole(role.value));
  } catch (err) {
    window.alert((err as Error).message);
  }
};
</script>

<style scoped>
.auth-form {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.hint {
  margin: 0;
  color: #4b5d87;
  font-size: 13px;
}

form {
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

input,
select {
  padding: 12px;
  border: 1px solid #d6ddf2;
  border-radius: 10px;
}

.actions {
  display: flex;
  flex-direction: column;
  gap: 12px;
  align-items: flex-start;
}

.submit {
  padding: 12px;
  border: none;
  border-radius: 10px;
  background: linear-gradient(135deg, #5a78f0, #4cb4ff);
  color: #fff;
  font-size: 16px;
  font-weight: 600;
  cursor: pointer;
}

.link {
  font-size: 13px;
  color: #3055ff;
  text-decoration: none;
}
</style>
