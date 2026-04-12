<template>
  <div class="user-badge" v-if="auth.user">
    <div class="info">
      <span class="name">{{ auth.user.name }}</span>
      <span class="role">{{ roleLabel }}</span>
    </div>
    <div class="actions">
      <button type="button" class="link" @click="handleProfile">个人中心</button>
      <button class="logout" type="button" @click="handleLogout">退出</button>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed } from "vue";
import { useRouter } from "vue-router";
import { useAuthStore } from "../../stores/auth";

const auth = useAuthStore();
auth.restore();
const router = useRouter();

const roleLabel = computed(() => {
  if (!auth.user) return "";
  switch (auth.user.role) {
    case "receiver":
      return "接收方";
    case "sender":
      return "发送方";
    case "admin":
    default:
      return "管理员";
  }
});

const handleLogout = () => {
  auth.logout();
  router.push({ name: "login" });
};

const handleProfile = () => {
  router.push({ name: "profile-basic" });
};
</script>

<style scoped>
.user-badge {
  display: inline-flex;
  align-items: center;
  gap: 16px;
  padding: 0;
  position: relative;
  z-index: 20;
  pointer-events: auto;
  background: transparent;
}

.info {
  display: flex;
  flex-direction: column;
  font-size: 13px;
  line-height: 1.2;
}

.name {
  font-weight: 600;
  color: #1c2550;
}

.role {
  color: #5f6d9b;
}

.actions {
  display: inline-flex;
  align-items: center;
  gap: 12px;
  pointer-events: auto;
}

.link,
.logout {
  border: none;
  background: none;
  cursor: pointer;
  color: #3055ff;
  font-size: 13px;
  padding: 0;
  text-decoration: none;
}

.link:hover,
.logout:hover {
  text-decoration: underline;
}

.user-badge * {
  pointer-events: auto;
}
</style>
