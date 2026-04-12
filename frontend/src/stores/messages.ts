import { defineStore } from "pinia";
import { reactive } from "vue";

interface MessageItem {
  id: string;
  title: string;
  content: string;
  time: string;
  link?: { name: string; params?: Record<string, string> };
  read: boolean;
}

export const useMessagesStore = defineStore("messages", () => {
  const state = reactive<{ items: MessageItem[] }>({ items: [] });

  const load = () => {
    if (state.items.length) return;
    state.items = [
      {
        id: "msg-001",
        title: "求交申请待审批",
        content: "接收方A 发起对 发送方B 的求交请求，点击前往审批。",
        time: new Date().toISOString(),
        link: { name: "admin-approvals" },
        read: false
      },
      {
        id: "msg-002",
        title: "任务告警",
        content: "任务 session-xyz 离线阶段耗时异常，请关注。",
        time: new Date(Date.now() - 3600_000).toISOString(),
        link: { name: "sessions", query: { focus: "session-xyz" } },
        read: false
      }
    ];
  };

  const markRead = (id: string) => {
    const target = state.items.find((item) => item.id === id);
    if (target) target.read = true;
  };

  const remove = (id: string) => {
    state.items = state.items.filter((item) => item.id !== id);
  };

  return {
    items: state.items,
    load,
    markRead,
    remove
  };
});
