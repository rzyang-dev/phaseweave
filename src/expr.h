/* expr.h - small infix expression compiler/evaluator (CPU bytecode).
 * This is intentionally not Lua eval per pixel: the Lua script supplies
 * expression strings once, C compiles them to postfix and evaluates per cell. */
#ifndef DP_EXPR_H
#define DP_EXPR_H

#include <stddef.h>

#define EXPR_MAX_INSNS 256
#define EXPR_MAX_VARS  16

typedef struct {
    int    op;
    int    arg;
    double val;
} ExprInsn;

typedef struct {
    ExprInsn code[EXPR_MAX_INSNS];
    int      n;
    int      nvars;
    char     var_names[EXPR_MAX_VARS][24];
} Expr;

int    expr_compile(Expr *e, const char *src,
                    const char *const *vars, int nvars,
                    char *err, size_t errsz);
double expr_eval(const Expr *e, const double *vars);

#endif
