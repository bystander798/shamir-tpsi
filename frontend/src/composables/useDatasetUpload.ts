import { computed, ref } from "vue";
import { useToast } from "../services/toast";

export const DEFAULT_MODULUS = 104857601;

const toNumberArray = (items: unknown[]) =>
  items
    .map((item) => Number(item))
    .filter((num) => Number.isFinite(num))
    .map((num) => Math.trunc(num));

const extractNumbersFromText = (content: string) => {
  const matches = content.match(/[-+]?\d+(?:\.\d+)?/g);
  if (!matches) return [];
  return toNumberArray(matches);
};

export const useDatasetUpload = () => {
  const toast = useToast();
  const uploadName = ref("");
  const uploadModulus = ref<number>(DEFAULT_MODULUS);
  const elements = ref<number[]>([]);
  const fileInfo = ref("");
  const sourceFile = ref<File | null>(null);
  const isParsing = ref(false);

  const elementsCount = computed(() => elements.value.length);
  const preview = computed(() => elements.value.slice(0, 16));
  const stats = computed(() => {
    if (!elements.value.length) return null;
    let min = Number.POSITIVE_INFINITY;
    let max = Number.NEGATIVE_INFINITY;
    const dedup = new Set<number>();
    elements.value.forEach((value) => {
      if (value < min) min = value;
      if (value > max) max = value;
      dedup.add(value);
    });
    return {
      min,
      max,
      duplicates: elements.value.length - dedup.size
    };
  });

  const reset = () => {
    elements.value = [];
    fileInfo.value = "";
    sourceFile.value = null;
    isParsing.value = false;
  };

  const setDefaultName = (name: string) => {
    if (!uploadName.value) {
      uploadName.value = name;
    }
  };

  const handleFile = (event: Event) => {
    const target = event.target as HTMLInputElement;
    if (!target.files || !target.files.length) return;

    const file = target.files[0];
    const reader = new FileReader();
    isParsing.value = true;
    sourceFile.value = file;

    reader.onload = () => {
      try {
        if (typeof reader.result !== "string") throw new Error("无法读取文件内容");
        let numbers: number[] = [];
        if (file.name.toLowerCase().endsWith(".json")) {
          const parsed = JSON.parse(reader.result);
          if (Array.isArray(parsed)) {
            numbers = toNumberArray(parsed);
          } else if (parsed && Array.isArray((parsed as Record<string, unknown>).elements)) {
            numbers = toNumberArray((parsed as Record<string, unknown>).elements);
          } else {
            throw new Error("JSON 格式应为数组或包含 elements 数组");
          }
        } else {
          numbers = extractNumbersFromText(reader.result);
        }
        if (!numbers.length) throw new Error("解析结果为空");
        elements.value = numbers;
        fileInfo.value = `${file.name} (${numbers.length} 条)`;
        toast.push("文件解析成功", "success");
      } catch (err) {
        elements.value = [];
        fileInfo.value = "";
        sourceFile.value = null;
        toast.push(`文件解析失败：${(err as Error).message}`, "error");
      } finally {
        isParsing.value = false;
      }
    };

    reader.onerror = () => {
      elements.value = [];
      fileInfo.value = "";
      sourceFile.value = null;
      isParsing.value = false;
      toast.push("文件读取失败", "error");
    };

    reader.readAsText(file);
    target.value = "";
  };

  return {
    uploadName,
    uploadModulus,
    elements,
    elementsCount,
    preview,
    stats,
    fileInfo,
    sourceFile,
    isParsing,
    handleFile,
    reset,
    setDefaultName
  };
};
