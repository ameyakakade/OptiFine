/* Always-enabled internal invariants.
 *
 * assert() disappears under NDEBUG, which is acceptable for a check that only
 * documents an impossible state but not for one that stands between the
 * compiler and wrong assembly: a truncated operand, a loop trip count the
 * 8-bit counter cannot hold, a branch beyond its reach. Those stay checked in
 * every build type through OPTIFINE_INVARIANT, which reports the failed
 * condition and aborts.
 *
 * Not for malformed input. Anything a user can reach through a model file, a
 * cost table, an input file or the command line is rejected with a diagnostic
 * and an error return before code generation starts (ir_verify, the cost-table
 * parser, the input readers). A failed invariant is a compiler bug. */
#ifndef OPTIFINE_INVARIANT_H
#define OPTIFINE_INVARIANT_H

_Noreturn void optifine_invariant_failed(const char *condition, const char *file, int line);

#define OPTIFINE_INVARIANT(condition) \
    ((condition) ? (void)0 : optifine_invariant_failed(#condition, __FILE__, __LINE__))

#endif /* OPTIFINE_INVARIANT_H */
