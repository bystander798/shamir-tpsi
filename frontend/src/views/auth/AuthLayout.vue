<template>
  <div class="auth-shell">
    <header class="auth-header">
      <div class="brand">
        <span class="logo">TPSI</span>
        <span class="title">隐匿集合求交 · 安全计算平台</span>
      </div>
      <RouterLink class="home-link" to="/">返回首页</RouterLink>
    </header>
    <main class="auth-main">
      <section class="illustration">
        <h2>隐私保护，与时间赛跑</h2>
        <p>上传真实数据集、发起求交协作、管理员实时审批与监控。</p>
        <ul>
          <li>支持接收方与发送方独立门户</li>
          <li>管理员全流程审批与真实性校验</li>
          <li>阶段耗时、通信量、交集结果透明可追溯</li>
        </ul>
      </section>
      <section class="auth-panel">
        <router-view />
      </section>
    </main>
    <footer class="auth-footer">
      <small>© TPSI Threshold PSI Platform · 提供安全可信的数据协作能力</small>
    </footer>
  </div>
</template>

<style scoped>
.auth-shell {
  min-height: 100vh;
  display: flex;
  flex-direction: column;
  background: linear-gradient(160deg, #f4f7ff 0%, #dfe9ff 100%);
  color: #1f2a44;
}

.auth-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 24px 48px;
}

.brand {
  display: flex;
  align-items: baseline;
  gap: 12px;
  font-weight: 600;
}

.logo {
  font-size: 24px;
  color: #3055ff;
}

.title {
  font-size: 16px;
  color: #4b5d87;
}

.home-link {
  color: #3055ff;
  text-decoration: none;
  font-weight: 500;
}

.auth-main {
  flex: 1;
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(320px, 1fr));
  gap: 24px;
  padding: 48px 64px;
  align-items: center;
}

.illustration {
  max-width: 520px;
}

.illustration h2 {
  font-size: 30px;
  margin-bottom: 12px;
}

.illustration p {
  color: #4b5d87;
  line-height: 1.6;
}

.illustration ul {
  margin-top: 16px;
  padding-left: 20px;
  color: #4b5d87;
}

.auth-panel {
  background: #fff;
  border-radius: 16px;
  box-shadow: 0 24px 48px rgba(36, 64, 112, 0.15);
  padding: 32px;
  min-width: 360px;
}

.auth-footer {
  padding: 16px 48px;
  text-align: center;
  color: #6a7aa7;
}
</style>
