<template>
  <div class="auth-form">
    <nav class="tabs">
      <button
        v-for="tab in roleTabs"
        :key="tab.role"
        :class="['tab', { active: currentRole === tab.role }]"
        type="button"
        @click="currentRole = tab.role"
      >
        {{ tab.label }}
      </button>
    </nav>
    <form @submit.prevent="handleSubmit">
      <label>
        登录账号
        <input v-model.trim="username" type="text" placeholder="请输入账号" required />
      </label>
      <label>
        登录密码
        <input v-model.trim="password" type="password" placeholder="请输入密码" required />
      </label>
      <div class="actions">
        <button class="submit" type="submit">登 录</button>
        <RouterLink class="link" :to="{ name: 'register', query: { role: currentRole } }">还没有账号？马上注册</RouterLink>
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

const roleTabs = [
  { role: "receiver" as UserRole, label: "接收方登录" },
  { role: "sender" as UserRole, label: "发送方登录" },
  { role: "admin" as UserRole, label: "管理员登录" }
];

const currentRole = ref<UserRole>((route.query.role as UserRole) || "receiver");

const handleSubmit = async () => {
  try {
    await auth.login({ username: username.value, password: password.value, role: currentRole.value });
    const target = (route.query.redirect as string) || auth.defaultRouteForRole(currentRole.value);
    router.push(target);
  } catch (err) {
    window.alert((err as Error).message);
  }
};
</script>

<style scoped>
.auth-form {
  display: flex;
  flex-direction: column;
  gap: 24px;
}

.tabs {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 8px;
}

.tab {
  padding: 10px;
  border-radius: 10px;
  border: 1px solid transparent;
  background: #eef3ff;
  color: #3a4c7a;
  font-weight: 600;
  cursor: pointer;
}

.tab.active {
  background: linear-gradient(135deg, #5a78f0, #4cb4ff);
  color: #fff;
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
  color: #2d3f68;
}

input {
  padding: 12px;
  border: 1px solid #d6ddf2;
  border-radius: 10px;
  font-size: 14px;
}

.actions {
  display: flex;
  flex-direction: column;
  gap: 12px;
  align-items: flex-start;
}

.submit {
  width: 100%;
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
