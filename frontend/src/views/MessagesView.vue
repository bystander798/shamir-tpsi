<template>
  <section class="card">
    <header>
      <h3>系统消息</h3>
      <p>包含审批提醒、任务告警与系统提示。</p>
    </header>
    <ul class="message-list" v-if="messages.length">
      <li v-for="item in messages" :key="item.id">
        <div>
          <strong>{{ item.title }}</strong>
          <p>{{ item.content }}</p>
          <span class="time">{{ item.time }}</span>
        </div>
        <div class="actions">
          <RouterLink v-if="item.link" :to="item.link">查看</RouterLink>
          <button @click="markRead(item.id)">标记已读</button>
          <button @click="remove(item.id)" class="danger">删除</button>
        </div>
      </li>
    </ul>
    <p v-else>暂无消息。</p>
  </section>
</template>

<script setup lang="ts">
import { computed } from "vue";
import { useMessagesStore } from "../stores/messages";

const store = useMessagesStore();
store.load();

const messages = computed(() => store.items);

const markRead = (id: string) => store.markRead(id);
const remove = (id: string) => store.remove(id);
</script>

<style scoped>
.card {
  background: #fff;
  border-radius: 16px;
  padding: 24px;
  box-shadow: 0 12px 32px rgba(24, 43, 90, 0.1);
}

header {
  display: flex;
  flex-direction: column;
  gap: 8px;
  margin-bottom: 16px;
}

.message-list {
  list-style: none;
  padding: 0;
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.message-list li {
  border: 1px solid #e5ecff;
  border-radius: 12px;
  padding: 16px;
  display: flex;
  justify-content: space-between;
  gap: 16px;
}

.time {
  font-size: 12px;
  color: #7c8ab5;
}

.actions {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.actions button,
.actions a {
  border: none;
  background: #eef3ff;
  color: #1f2a44;
  padding: 6px 10px;
  border-radius: 8px;
  cursor: pointer;
  text-align: center;
}

.actions .danger {
  background: rgba(255, 94, 104, 0.16);
  color: #ff5e68;
}
</style>
