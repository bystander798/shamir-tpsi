<template>
  <div v-if="hasToasts" class="toast-host">
    <transition-group name="toast" tag="div">
      <div v-for="toast in toasts.messages" :key="toast.id" :class="['toast', toast.level]">
        {{ toast.text }}
      </div>
    </transition-group>
  </div>
</template>

<script setup lang="ts">
import { computed } from "vue";
import { useToast } from "../../services/toast";

const toasts = useToast();
const hasToasts = computed(() => toasts.messages.length > 0);
</script>

<style scoped>
.toast-host {
  position: fixed;
  top: 24px;
  right: 24px;
  display: grid;
  gap: 12px;
  z-index: 2000;
  pointer-events: none;
}

.toast-enter-active,
.toast-leave-active {
  transition: all 0.2s ease;
}

.toast-enter-from,
.toast-leave-to {
  opacity: 0;
  transform: translateY(-8px);
}

.toast {
  padding: 12px 16px;
  border-radius: 10px;
  min-width: 220px;
  color: #fff;
  box-shadow: 0 12px 32px rgba(25, 40, 72, 0.2);
  pointer-events: auto;
}

.toast.info {
  background: linear-gradient(135deg, #5a78f0, #4cb4ff);
}

.toast.success {
  background: linear-gradient(135deg, #3ba55c, #6bd48c);
}

.toast.error {
  background: linear-gradient(135deg, #ff5e68, #ff9f40);
}
</style>
