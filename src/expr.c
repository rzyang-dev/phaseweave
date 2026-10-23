/* expr.c - recursive descent expression compiler + postfix VM */
#include "expr.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    OP_CONST = 1, OP_VAR,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_POW, OP_NEG,
    OP_SIN, OP_COS, OP_TAN, OP_ASIN, OP_ACOS, OP_ATAN,
    OP_SQRT, OP_ABS, OP_EXP, OP_LOG, OP_FLOOR, OP_CEIL,
    OP_ATAN2, OP_MIN, OP_MAX, OP_POWF
};

typedef struct {
    const char *s;
    const char *p;
    const char *const *vars;
    int nvars;
    Expr *e;
    char *err;
    size_t errsz;
    int failed;
} Parser;

static void perr(Parser *P, const char *fmt, ...)
{
    va_list ap;
    if (P->failed) return;
    P->failed = 1;
    if (!P->err || P->errsz == 0) return;
    va_start(ap, fmt);
    vsnprintf(P->err, P->errsz, fmt, ap);
    va_end(ap);
}

static void skip(Parser *P)
{
    while (*P->p && isspace((unsigned char)*P->p)) P->p++;
}

static int emit(Parser *P, int op, int arg, double val)
{
    ExprInsn *i;
    if (P->e->n >= EXPR_MAX_INSNS) { perr(P, "expression too long"); return 0; }
    i = &P->e->code[P->e->n++];
    i->op = op; i->arg = arg; i->val = val;
    return 1;
}

static int is_ident_start(int c) { return isalpha(c) || c == '_'; }
static int is_ident_char(int c) { return isalnum(c) || c == '_'; }

static int parse_expr(Parser *P);

static int parse_primary(Parser *P)
{
    skip(P);
    if (*P->p == '(') {
        P->p++;
        if (!parse_expr(P)) return 0;
        skip(P);
        if (*P->p != ')') { perr(P, "expected ')'"); return 0; }
        P->p++;
        return 1;
    }
    if (is_ident_start((unsigned char)*P->p)) {
        char name[32];
        size_t n = 0;
        int i;
        while (is_ident_char((unsigned char)*P->p) && n + 1 < sizeof(name))
            name[n++] = *P->p++;
        name[n] = 0;
        if (*P->p == '(') {
            int fn = 0, args = 0;
            P->p++;
            skip(P);
            if (strcmp(name, "sin") == 0) fn = OP_SIN;
            else if (strcmp(name, "cos") == 0) fn = OP_COS;
            else if (strcmp(name, "tan") == 0) fn = OP_TAN;
            else if (strcmp(name, "asin") == 0) fn = OP_ASIN;
            else if (strcmp(name, "acos") == 0) fn = OP_ACOS;
            else if (strcmp(name, "atan") == 0) fn = OP_ATAN;
            else if (strcmp(name, "sqrt") == 0) fn = OP_SQRT;
            else if (strcmp(name, "abs") == 0) fn = OP_ABS;
            else if (strcmp(name, "exp") == 0) fn = OP_EXP;
            else if (strcmp(name, "log") == 0) fn = OP_LOG;
            else if (strcmp(name, "floor") == 0) fn = OP_FLOOR;
            else if (strcmp(name, "ceil") == 0) fn = OP_CEIL;
            else if (strcmp(name, "atan2") == 0) fn = OP_ATAN2;
            else if (strcmp(name, "min") == 0) fn = OP_MIN;
            else if (strcmp(name, "max") == 0) fn = OP_MAX;
            else if (strcmp(name, "pow") == 0) fn = OP_POWF;
            else { perr(P, "unknown function '%s'", name); return 0; }
            if (!(fn == OP_ATAN2 || fn == OP_MIN || fn == OP_MAX || fn == OP_POWF)) {
                if (!parse_expr(P)) return 0;
                args = 1;
            } else {
                if (!parse_expr(P)) return 0;
                skip(P);
                if (*P->p != ',') { perr(P, "expected ',' in %s", name); return 0; }
                P->p++;
                if (!parse_expr(P)) return 0;
                args = 2;
            }
            skip(P);
            if (*P->p != ')') { perr(P, "expected ')' after %s", name); return 0; }
            P->p++;
            if (!emit(P, fn, args, 0.0)) return 0;
            return 1;
        }
        if (strcmp(name, "pi") == 0) return emit(P, OP_CONST, 0, 3.14159265358979323846);
        if (strcmp(name, "e") == 0) return emit(P, OP_CONST, 0, 2.71828182845904523536);
        for (i = 0; i < P->nvars; ++i) {
            if (strcmp(name, P->vars[i]) == 0)
                return emit(P, OP_VAR, i, 0.0);
        }
        perr(P, "unknown variable '%s'", name);
        return 0;
    }
    if (isdigit((unsigned char)*P->p) || *P->p == '.') {
        char *end = NULL;
        double v = strtod(P->p, &end);
        if (end == P->p) { perr(P, "bad number"); return 0; }
        P->p = end;
        return emit(P, OP_CONST, 0, v);
    }
    perr(P, "unexpected character '%c'", *P->p ? *P->p : '?');
    return 0;
}

static int parse_unary(Parser *P)
{
    skip(P);
    if (*P->p == '+' ) { P->p++; return parse_unary(P); }
    if (*P->p == '-' ) { P->p++; if (!parse_unary(P)) return 0; return emit(P, OP_NEG, 0, 0.0); }
    return parse_primary(P);
}

static int parse_factor(Parser *P)
{
    if (!parse_unary(P)) return 0;
    skip(P);
    if (*P->p == '^') {
        P->p++;
        if (!parse_factor(P)) return 0; /* right associative */
        return emit(P, OP_POW, 0, 0.0);
    }
    return 1;
}

static int parse_term(Parser *P)
{
    if (!parse_factor(P)) return 0;
    for (;;) {
        int op;
        skip(P);
        if (*P->p == '*') op = OP_MUL;
        else if (*P->p == '/') op = OP_DIV;
        else if (*P->p == '%') op = OP_MOD;
        else break;
        P->p++;
        if (!parse_factor(P)) return 0;
        if (!emit(P, op, 0, 0.0)) return 0;
    }
    return 1;
}

static int parse_expr(Parser *P)
{
    if (!parse_term(P)) return 0;
    for (;;) {
        int op;
        skip(P);
        if (*P->p == '+') op = OP_ADD;
        else if (*P->p == '-') op = OP_SUB;
        else break;
        P->p++;
        if (!parse_term(P)) return 0;
        if (!emit(P, op, 0, 0.0)) return 0;
    }
    return 1;
}

int expr_compile(Expr *e, const char *src,
                 const char *const *vars, int nvars,
                 char *err, size_t errsz)
{
    Parser P;
    int i;
    if (err && errsz) err[0] = 0;
    if (!e || !src) return 0;
    memset(e, 0, sizeof(*e));
    if (nvars > EXPR_MAX_VARS) nvars = EXPR_MAX_VARS;
    e->nvars = nvars;
    for (i = 0; i < nvars; ++i) {
        strncpy(e->var_names[i], vars[i], sizeof(e->var_names[i]) - 1);
        e->var_names[i][sizeof(e->var_names[i]) - 1] = 0;
    }
    P.s = src; P.p = src;
    P.vars = vars; P.nvars = nvars;
    P.e = e; P.err = err; P.errsz = errsz; P.failed = 0;
    if (!parse_expr(&P)) return 0;
    skip(&P);
    if (*P.p) { perr(&P, "unexpected '%s'", P.p); return 0; }
    if (e->n == 0) { perr(&P, "empty expression"); return 0; }
    return 1;
}

double expr_eval(const Expr *e, const double *vars)
{
    double st[64];
    int sp = 0, i;
    if (!e) return 0.0;
    for (i = 0; i < e->n; ++i) {
        const ExprInsn *in = &e->code[i];
        switch (in->op) {
        case OP_CONST: st[sp++] = in->val; break;
        case OP_VAR: st[sp++] = (vars && in->arg >= 0 && in->arg < e->nvars) ? vars[in->arg] : 0.0; break;
        case OP_ADD: sp--; st[sp-1] += st[sp]; break;
        case OP_SUB: sp--; st[sp-1] -= st[sp]; break;
        case OP_MUL: sp--; st[sp-1] *= st[sp]; break;
        case OP_DIV: sp--; st[sp-1] /= st[sp]; break;
        case OP_MOD: sp--; st[sp-1] = fmod(st[sp-1], st[sp]); break;
        case OP_POW: sp--; st[sp-1] = pow(st[sp-1], st[sp]); break;
        case OP_NEG: st[sp-1] = -st[sp-1]; break;
        case OP_SIN: st[sp-1] = sin(st[sp-1]); break;
        case OP_COS: st[sp-1] = cos(st[sp-1]); break;
        case OP_TAN: st[sp-1] = tan(st[sp-1]); break;
        case OP_ASIN: st[sp-1] = asin(st[sp-1]); break;
        case OP_ACOS: st[sp-1] = acos(st[sp-1]); break;
        case OP_ATAN: st[sp-1] = atan(st[sp-1]); break;
        case OP_SQRT: st[sp-1] = sqrt(st[sp-1]); break;
        case OP_ABS: st[sp-1] = fabs(st[sp-1]); break;
        case OP_EXP: st[sp-1] = exp(st[sp-1]); break;
        case OP_LOG: st[sp-1] = log(st[sp-1]); break;
        case OP_FLOOR: st[sp-1] = floor(st[sp-1]); break;
        case OP_CEIL: st[sp-1] = ceil(st[sp-1]); break;
        case OP_ATAN2: sp--; st[sp-1] = atan2(st[sp-1], st[sp]); break;
        case OP_MIN: sp--; st[sp-1] = fmin(st[sp-1], st[sp]); break;
        case OP_MAX: sp--; st[sp-1] = fmax(st[sp-1], st[sp]); break;
        case OP_POWF: sp--; st[sp-1] = pow(st[sp-1], st[sp]); break;
        default: return 0.0;
        }
        if (sp < 0 || sp > 64) return 0.0;
    }
    return sp == 1 ? st[0] : 0.0;
}
