import type { Plugin } from "@opencode-ai/plugin"

export const TrimPlugin: Plugin = async () => {
  return {
    "tool.execute.before": async (input, output) => {
      const tool = String(input?.tool ?? "").toLowerCase()
      if (tool !== "bash" && tool !== "shell") return
      const args = output?.args
      if (!args || typeof args !== "object") return

      const command = (args as Record<string, unknown>).command
      if (typeof command !== "string" || !command) return
      const target = command.trim()
      if (!target || /^(?:trim|trm)\s+/.test(target)) return

      ;(args as Record<string, unknown>).command = `trim ${target}`
    },
  }
}
