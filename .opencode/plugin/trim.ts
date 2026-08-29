import type { Plugin } from "@opencode-ai/plugin"

const TRIM_PREFIX = /^(?:trim|trm)\s+/

export default (async () => {
  return {
    "tool.execute.before": async (input, output) => {
      if (input.tool !== "bash") return
      const cmd = output.command
      if (typeof cmd !== "string") return
      const trimmed = cmd.trim()
      if (!trimmed) return
      if (TRIM_PREFIX.test(trimmed)) return
      output.command = `trim ${trimmed}`
    },
  }
}) satisfies Plugin
