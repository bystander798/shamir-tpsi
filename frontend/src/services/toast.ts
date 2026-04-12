import { ref } from "vue";

export type ToastLevel = "info" | "success" | "error";

export interface ToastMessage {
  id: number;
  text: string;
  level: ToastLevel;
  timeout: number;
}

const messages = ref<ToastMessage[]>([]);

export const useToast = () => {
  const push = (text: string, level: ToastLevel = "info", timeout = 3000) => {
    const id = Date.now() + Math.random();
    messages.value.push({ id, text, level, timeout });
    window.setTimeout(() => {
      messages.value = messages.value.filter((msg) => msg.id !== id);
    }, timeout);
  };

  return {
    messages,
    push
  };
};
