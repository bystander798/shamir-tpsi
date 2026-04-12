<template>
  <div class="profile-shell">
    <aside class="sidebar">
      <h3>个人中心</h3>
      <RouterLink to="/profile" exact-active-class="active">基础信息</RouterLink>
      <RouterLink to="/profile/security" active-class="active">安全设置</RouterLink>
      <RouterLink to="/profile/notifications" active-class="active">通知偏好</RouterLink>
    </aside>
    <main class="content">
      <router-view />
    </main>
  </div>
</template>

<style scoped>
.profile-shell {
  min-height: 100vh;
  display: grid;
  grid-template-columns: 220px 1fr;
  background: #f5f7ff;
}

.sidebar {
  background: #142347;
  color: #fff;
  padding: 32px 24px;
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.sidebar h3 {
  margin: 0 0 12px;
}

.sidebar a {
  color: rgba(255, 255, 255, 0.85);
  padding: 8px 12px;
  border-radius: 8px;
}

.sidebar a.active {
  background: rgba(255, 255, 255, 0.2);
  color: #fff;
}

.content {
  padding: 40px;
}
</style>
