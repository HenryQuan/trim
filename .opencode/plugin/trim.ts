import { type Plugin, tool } from "@opencode-ai/plugin"

export const TrimPlugin: Plugin = async ({ $ }) => {
  return {
    tool: {
      read: tool({
        description:
          "Read a file via the trim CLI. Small files are read whole; large files return an outline plus first/last lines to save context.",
        args: {
          filePath: tool.schema
            .string()
            .describe("Absolute or relative path to the file."),
        },
        async execute(args, context) {
          const r = await $`trim read ${args.filePath}`.cwd(
            context.directory,
          )
          return r.stdout.toString()
        },
      }),
      glob: tool({
        description:
          "Search file contents with ripgrep via the trim CLI (trim rg).",
        args: {
          pattern: tool.schema
            .string()
            .describe("Regex pattern to search for, e.g. 'foo' or 'fn\\s+bar'."),
          include: tool.schema
            .string()
            .optional()
            .describe("Glob to restrict searched files, e.g. '*.ts'."),
        },
        async execute(args, context) {
          const r = args.include
            ? await $`trim rg -g ${args.include} ${args.pattern}`.cwd(
                context.directory,
              )
            : await $`trim rg ${args.pattern}`.cwd(context.directory)
          return r.stdout.toString()
        },
      }),
    },
    "tool.execute.before": async (input, output) => {
      const tool = String(input?.tool ?? "").toLowerCase()
      if (tool !== "bash" && tool !== "shell") return
      const args = output?.args
      if (!args || typeof args !== "object") return

      const command = (args as Record<string, unknown>).command
      if (typeof command !== "string" || !command) return
      const target = command.trim()
      if (!target || /^(?:trim|trm)\s+/.test(target)) return
      // Skip compound shell statements; prepending trim to these breaks them.
      if (/^(?:for|while|until|if|case|select|function|time|!|do|done|fi|then|else)\b|^\{|^\(/.test(target))
        return

      ;(args as Record<string, unknown>).command = `trim ${target}`
    },
  }
}
