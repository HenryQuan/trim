import type { Plugin } from "@opencode-ai/plugin"

// trim-on-rtk plugin: let `rtk rewrite` pick the optimal compressed command,
// then force every result through trim's cap. Requires rtk and trim on PATH.

export const TrimRtkPlugin: Plugin = async ({ $ }) => {
  try {
    await $`which rtk`.quiet()
  } catch {
    console.warn("[trimrtk] rtk binary not found in PATH — plugin disabled")
    return {}
  }

  return {
    "tool.execute.before": async (input, output) => {
      const tool = String(input?.tool ?? "").toLowerCase()
      if (tool !== "bash" && tool !== "shell") return
      const args = output?.args
      if (!args || typeof args !== "object") return

      const command = (args as Record<string, unknown>).command
      if (typeof command !== "string" || !command) return
      const target = command.trim()
      if (!target || target.startsWith("trim ") || target.startsWith("rtk ")) return

      let cmd = target
      try {
        const res = await $`rtk rewrite ${target}`.quiet().nothrow()
        const rewritten = String(res.stdout).trim()
        if (rewritten && rewritten !== target) cmd = rewritten
      } catch {
        // rewrite failed — use original
      }
      ;(args as Record<string, unknown>).command = `trim ${cmd}`
    },
  }
}
