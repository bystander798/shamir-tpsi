<template>
  <div class="portal">
    <aside class="sidebar">
      <h2>发送方门户</h2>
      <RouterLink to="/sender" exact-active-class="active">概览</RouterLink>
      <RouterLink to="/sender/datasets" active-class="active">我的数据集</RouterLink>
      <RouterLink to="/sender/requests" active-class="active">协同请求</RouterLink>
    </aside>
    <main class="content">
      <header class="top">
        <h3>发送方工作台</h3>
        <UserBadge />
      </header>
      <router-view />
    </main>
  </div>
</template>

<script setup lang="ts">
import UserBadge from "../../components/layout/UserBadge.vue";
</script>

<style scoped>
.portal {
  display: grid;
  grid-template-columns: 240px 1fr;
  min-height: 100vh;
}

.sidebar {
  background: #132049;
  color: #fff;
  padding: 32px 24px;
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.sidebar h2 {
  margin: 0 0 12px;
}

.sidebar a {
  color: rgba(255, 255, 255, 0.85);
  padding: 10px 12px;
  border-radius: 8px;
}

.sidebar a.active {
  background: rgba(255, 255, 255, 0.2);
  color: #fff;
}

.content {
  background: #f6f8ff;
  padding: 32px 40px;
  display: flex;
  flex-direction: column;
  gap: 24px;
}

.top {
  display: flex;
  justify-content: space-between;
  align-items: center;
  position: relative;
  z-index: 2;
}
</style>
