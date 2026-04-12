<template>
  <section class="card">
    <h3>概览</h3>
    <p>查看来自接收方的待确认求交请求，维护自身数据集。</p>
    <RouterLink to="/sender/requests" class="link">前往协同请求</RouterLink>
  </section>
</template>

<style scoped>
.card {
  background: #fff;
  border-radius: 16px;
  padding: 24px;
  box-shadow: 0 12px 32px rgba(24, 43, 90, 0.1);
}

.link {
  display: inline-block;
  margin-top: 16px;
  padding: 8px 12px;
  border-radius: 8px;
  background: #eef3ff;
}
</style>
