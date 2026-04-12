<template>
  <div class="page-shell">
    <SidebarNav class="sidebar" />
    <main class="page-main">
      <header class="page-header">
        <div>
          <h2>TPSI 管理控制台</h2>
          <p class="page-subtitle">阈值私有集合求交 · 运行监控与审批</p>
        </div>
        <UserBadge />
      </header>
      <router-view />
    </main>
  </div>
</template>

<script setup lang="ts">
import SidebarNav from "../components/layout/SidebarNav.vue";
import UserBadge from "../components/layout/UserBadge.vue";
</script>

<style scoped>
.page-shell {
  min-height: 100vh;
  display: grid;
  grid-template-columns: 240px 1fr;
}

.sidebar {
  background: #101b3a;
  color: #fff;
}

.page-main {
  padding: 32px 40px;
  display: flex;
  flex-direction: column;
  gap: 24px;
  background: #f5f7ff;
}

.page-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  position: relative;
  z-index: 2;
}

h2 {
  margin: 0;
}

.page-subtitle {
  margin: 6px 0 0;
  color: #5a6d99;
  font-size: 14px;
}
</style>
