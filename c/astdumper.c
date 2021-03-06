#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <glib.h>

#include "ast.h"
#include "astdumper.h"

static char* str(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	char* res;
	vasprintf(&res, fmt, ap);
	va_end (ap);
	return res;
}

static char* dump_member_expr (SmMemberExpr* expr) {
	if (expr->inner) {
		char* inner = sm_ast_dump (expr->inner);
		char* res = str("%s.%s", inner, expr->name);
		free (inner);
		return res;
	} else {
		return strdup(expr->name);
	}
}

static char* dump_literal (SmLiteral* expr) {
	char* res = NULL;
	if (expr->str) {
		// TODO: quote
		asprintf(&res, "\"%s\"", expr->str);
	} else {
		asprintf(&res, "%g", expr->num);
	}
	return res;
}

static char* dump_assign_expr (SmAssignExpr* expr) {
	char* res = strdup("");
	char* old;

	GPtrArray* names = expr->names;
	for (int i=0; i < names->len; i++) {
		const char* name = (const char*) names->pdata[i];
		old = res;
		if (i==0) {
			res = strdup (name);
		} else {
			res = str("%s, %s", res, name);
		}
		free (old);
	}

	old = res;
	char* inner = sm_ast_dump (expr->value);
	res = str("%s = %s", res, inner);
	free (old);
	free (inner);
	return res;
}

static char* dump_func_expr (SmFuncExpr* expr) {
	char* res = strdup("");
	char* old;

	GPtrArray* params = expr->params;
	for (int i=0; i < params->len; i++) {
		const char* param = (const char*) params->pdata[i];
		old = res;
		if (i==0) {
			res = strdup (param);
		} else {
			res = str("%s %s", res, param);
		}
		free (old);
	}

	old = res;
	char* inner = sm_ast_dump (expr->body);
	res = str("%s: %s", res, inner);
	free (old);
	free (inner);
	return res;
}

static char* dump_seq_expr (SmSeqExpr* expr) {
	char* res = NULL;
	char* old;
	char* inner;
	for (int i=0; i < expr->assigns->len; i++) {
		SmAssignExpr* a = (SmAssignExpr*) expr->assigns->pdata[i];
		old = res;
		inner = sm_ast_dump (EXPR(a));
		res = str("%s%s;\n", res ? res : "", inner);
		free (inner);
		free (old);
	}

	old = res;
	inner = sm_ast_dump (expr->result);
	res = str("%s%s", res ? res : "", inner);
	free (old);
	free (inner);
	return res;
}

static char* dump_call_expr (SmCallExpr* expr) {
	char* func = sm_ast_dump (expr->func);
	char* res = func;
	for (int i=0; i < expr->args->len; i++) {
		char* arg = sm_ast_dump (EXPR(expr->args->pdata[i]));
		char* old = res;
		res = str("%s %s", res, arg);
		free (old);
	}

	return res;
}

#define CAST(x) (char* (*)(SmExpr*))(x)
char* (*dump_table[])(SmExpr*) = {
	[SM_MEMBER_EXPR] = CAST(dump_member_expr),
	[SM_SEQ_EXPR] = CAST(dump_seq_expr),
	[SM_ASSIGN_EXPR] = CAST(dump_assign_expr),
	[SM_LITERAL] = CAST(dump_literal),
	[SM_FUNC_EXPR] = CAST(dump_func_expr),
	[SM_CALL_EXPR] = CAST(dump_call_expr)
};

char* sm_ast_dump (SmExpr* expr) {
	return dump_table[expr->type] (expr);
}
