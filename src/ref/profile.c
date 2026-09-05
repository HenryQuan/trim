/* profile.c — language profiles and the per-language index pass.
   Kinds verified against ast-grep 0.45 for every profiled language. */
#include "../trim.h"
#include "rf.h"

static const RefProfile PROFILES[] = {
    {"dart", "dart", "call_expression",
     "function_declaration,method_declaration"},
    {"javascript", "js,mjs,cjs", "call_expression",
     "function_declaration,method_definition"},
    {"typescript", "ts", "call_expression",
     "function_declaration,method_definition"},
    {"tsx", "tsx", "call_expression", "function_declaration,method_definition"},
    {"python", "py", "call", "function_definition"},
    {"go", "go", "call_expression", "function_declaration,method_declaration"},
    {"rust", "rs", "call_expression", "function_item"},
    {"java", "java", "method_invocation",
     "method_declaration,constructor_declaration"},
    {"c", "c,h", "call_expression", "function_definition"},
    {"cpp", "cc,cpp,cxx,hpp,hh", "call_expression", "function_definition"},
    {"csharp", "cs", "invocation_expression",
     "method_declaration,constructor_declaration"},
    {"kotlin", "kt,kts", "call_expression", "function_declaration"},
    {"ruby", "rb", "call", "method"},
    {"php", "php", "function_call_expression",
     "function_definition,method_declaration"},
    {"swift", "swift", "call_expression", "function_declaration"},
    {"lua", "lua", "function_call", "function_declaration,function_definition"},
    {"bash", "sh,bash", "command", "function_definition"},
    {"elixir", "ex,exs", "call", "call"},
};
#define NPROFILES (sizeof(PROFILES) / sizeof(PROFILES[0]))

/* one call pass + def passes per profile; a profile is skipped when its
   call kind produces nothing (language absent from the path) */
void rf_scan(const char *path) {
    for (size_t pi = 0; pi < NPROFILES; pi++) {
        const RefProfile *pf = &PROFILES[pi];
        if (!rf_run_kind(pf->callKind, pf->lang, path, 0))
            continue;
        const char *dk = pf->defKinds;
        while (*dk) {
            const char *comma = strchr(dk, ',');
            size_t kl = comma ? (size_t)(comma - dk) : strlen(dk);
            char kind[64];
            if (kl >= sizeof(kind))
                kl = sizeof(kind) - 1;
            memcpy(kind, dk, kl);
            kind[kl] = '\0';
            rf_run_kind(kind, pf->lang, path, 1);
            dk = comma ? comma + 1 : dk + strlen(dk);
        }
    }
}
