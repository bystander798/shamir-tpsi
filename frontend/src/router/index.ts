import { createRouter, createWebHistory, RouteRecordRaw } from "vue-router";
import AuthLayout from "../views/auth/AuthLayout.vue";
import LoginView from "../views/auth/LoginView.vue";
import RegisterView from "../views/auth/RegisterView.vue";
import DashboardLayout from "../views/DashboardLayout.vue";
import ReceiverLayout from "../views/receiver/ReceiverLayout.vue";
import SenderLayout from "../views/sender/SenderLayout.vue";
import DashboardHome from "../views/DashboardHome.vue";
import SessionsView from "../views/SessionsView.vue";
import DatasetsView from "../views/DatasetsView.vue";
import MessagesView from "../views/MessagesView.vue";
import SettingsView from "../views/SettingsView.vue";
import ReceiverOverview from "../views/receiver/ReceiverOverview.vue";
import ReceiverDatasets from "../views/receiver/ReceiverDatasets.vue";
import ReceiverRequests from "../views/receiver/ReceiverRequests.vue";
import ReceiverResults from "../views/receiver/ReceiverResults.vue";
import SenderOverview from "../views/sender/SenderOverview.vue";
import SenderDatasets from "../views/sender/SenderDatasets.vue";
import SenderRequests from "../views/sender/SenderRequests.vue";
import AdminMessages from "../views/admin/AdminMessages.vue";
import ProfileLayout from "../views/profile/ProfileLayout.vue";
import ProfileBasic from "../views/profile/ProfileBasic.vue";
import ProfileSecurity from "../views/profile/ProfileSecurity.vue";
import ProfileNotifications from "../views/profile/ProfileNotifications.vue";
import { pinia } from "../stores";
import { useAuthStore, type UserRole } from "../stores/auth";

declare module "vue-router" {
  interface RouteMeta {
    requiresAuth?: boolean;
    guestOnly?: boolean;
    roles?: UserRole[];
  }
}

const routes: RouteRecordRaw[] = [
  {
    path: "/auth",
    component: AuthLayout,
    meta: { guestOnly: true },
    children: [
      { path: "login", name: "login", component: LoginView },
      { path: "register", name: "register", component: RegisterView }
    ]
  },
  {
    path: "/",
    component: DashboardLayout,
    meta: { requiresAuth: true, roles: ["admin"] },
    children: [
      { path: "", name: "dashboard", component: DashboardHome },
      { path: "sessions", name: "sessions", component: SessionsView },
      { path: "datasets", name: "datasets", component: DatasetsView },
      { path: "messages", name: "messages", component: MessagesView },
      { path: "settings", name: "settings", component: SettingsView },
      { path: "approvals", name: "admin-approvals", component: AdminMessages }
    ]
  },
  {
    path: "/receiver",
    component: ReceiverLayout,
    meta: { requiresAuth: true, roles: ["receiver"] },
    children: [
      { path: "", name: "receiver-home", component: ReceiverOverview },
      { path: "datasets", name: "receiver-datasets", component: ReceiverDatasets },
      { path: "requests", name: "receiver-requests", component: ReceiverRequests },
      { path: "results", name: "receiver-results", component: ReceiverResults }
    ]
  },
  {
    path: "/sender",
    component: SenderLayout,
    meta: { requiresAuth: true, roles: ["sender"] },
    children: [
      { path: "", name: "sender-home", component: SenderOverview },
      { path: "datasets", name: "sender-datasets", component: SenderDatasets },
      { path: "requests", name: "sender-requests", component: SenderRequests }
    ]
  },
  {
    path: "/profile",
    component: ProfileLayout,
    meta: { requiresAuth: true },
    children: [
      { path: "", name: "profile-basic", component: ProfileBasic },
      { path: "security", name: "profile-security", component: ProfileSecurity },
      { path: "notifications", name: "profile-notifications", component: ProfileNotifications }
    ]
  },
  { path: "/:pathMatch(.*)*", redirect: "/auth/login" }
];

const router = createRouter({
  history: createWebHistory(),
  routes
});

router.beforeEach((to, from, next) => {
  const auth = useAuthStore(pinia);
  auth.restore();

  if (to.meta.guestOnly && auth.isAuthenticated) {
    return next(auth.defaultRouteForRole(auth.user!.role));
  }

  if (to.meta.requiresAuth && !auth.isAuthenticated) {
    return next({ name: "login", query: { redirect: to.fullPath } });
  }

  if (to.meta.roles && auth.user && !to.meta.roles.includes(auth.user.role)) {
    return next(auth.defaultRouteForRole(auth.user.role));
  }

  next();
});

export default router;
