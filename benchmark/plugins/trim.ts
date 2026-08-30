import type { Plugin } from "@opencode-ai/plugin"

// trim auto-prefix plugin — forces every bash command through trim's 1000-char cap.
// Mirror of rtk.ts: same tool.execute.before rewrite, but prefixes `trim ` instead.

export const TrimPlugin: Plugin = async () => {
  return {
    "tool.execute.before": async (input, output) => {
      const tool = String(input?.tool ?? "").toLowerCase()
      if (tool !== "bash" && tool !== "shell") return
      const args = output?.args
      if (!args || typeof args !== "object") return

      const command = (args as Record<string, unknown>).command
      if (typeof command !== "string" || !command) return
      const cmd = command.trim()
      if (!cmd || cmd === "trim" || cmd.startsWith("trim ")) return
      // prefix each chained segment so every `trim` caps its own output
      const segs = cmd.split(/\s*&&\s*|\s*;\s*/).map((s) => s.trim()).filter(Boolean)
      const rewritten = segs.map((s) => (s.startsWith("trim ") || s === "trim" ? s : `trim ${s}`)).join("; ")
      ;(args as Record<string, unknown>).command = rewritten
    },
  }
}
