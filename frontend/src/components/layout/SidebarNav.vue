<template>
  <aside class="sidebar-nav">
    <div class="logo">
      <span class="mark">TPSI</span>
      <span class="title">安全计算</span>
    </div>
    <nav>
      <RouterLink
        v-for="item in navItems"
        :key="item.path"
        :to="item.path"
        class="nav-link"
        active-class="active"
      >
        <span class="emoji">{{ item.icon }}</span>
        <span>{{ item.label }}</span>
      </RouterLink>
    </nav>
  </aside>
</template>

<script setup lang="ts">
const navItems = [
  { path: "/", label: "总览", icon: "📊" },
  { path: "/sessions", label: "求交任务", icon: "🔄" },
  { path: "/datasets", label: "数据集", icon: "🗂️" },
  { path: "/messages", label: "消息中心", icon: "🔔" },
  { path: "/settings", label: "系统参数", icon: "⚙️" },
  { path: "/profile", label: "个人中心", icon: "👤" }
];
</script>

<style scoped>
.sidebar-nav {
  display: flex;
  flex-direction: column;
  height: 100%;
  padding: 24px 16px;
  gap: 24px;
}

.logo {
  display: flex;
  flex-direction: column;
  gap: 4px;
  font-weight: 700;
  letter-spacing: 0.08em;
}

.mark {
  font-size: 22px;
}

.title {
  font-size: 14px;
  color: #9cb3db;
}

nav {
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.nav-link {
  display: flex;
  gap: 12px;
  align-items: center;
  padding: 12px 16px;
  border-radius: 12px;
  color: #c7d5f1;
  transition: background 0.2s ease, color 0.2s ease;
}

.nav-link:hover {
  background: rgba(255, 255, 255, 0.12);
}

.nav-link.active {
  background: linear-gradient(135deg, #5a78f0, #4cb4ff);
  color: #0d1934;
}

.emoji {
  font-size: 18px;
}
</style>
