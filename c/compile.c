#include <assert.h>
#include <stdio.h>
#include <glib.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "compile.h"
#include "codegen.h"
#include "ast.h"
#include "llvm.h"
#include "code.h"
#include "scope.h"
#include "prim.h"

#define DEFUNC(n,x) static SmVar n (SmCodegen* gen, x* expr, int prealloc)
#define RETVAL(x,y,z) SmVar _res_var={.x, .y, .z}; return _res_var
#define VISIT(x) call_compile_table (gen, EXPR(x), -1)

typedef enum {
	TYPE_FUN,
	TYPE_LST,
	TYPE_EOS,
	TYPE_INT,
	TYPE_CHR,
	TYPE_STR,
	TYPE_BOOL,
	TYPE_UNK // unknown at compile time
} SmVarType;

typedef struct {
	int id;
	int isthunk;
	SmVarType type;
} SmVar;


static SmVar call_compile_table (SmCodegen* gen, SmExpr* expr, int prealloc);

static long long unsigned int tagmap[] = {
	[TYPE_FUN] = TAG_FUN,
	[TYPE_LST] = TAG_LST,
	[TYPE_EOS] = TAG_OBJ,
	[TYPE_INT] = TAG_INT,
	[TYPE_CHR] = TAG_CHR,
	[TYPE_STR] = TAG_STR,
	[TYPE_BOOL] = TAG_OBJ
};

int try_var (SmCodegen* gen, SmVar var, SmVarType type) {
	GET_CODE;
	COMMENT("try %%%d, expect %d", var.id, type);
	
	int object = var.id;
	RUNDBG("try var %p\n", object, NULL);
	if (var.type != TYPE_UNK) {
		if (var.type != type) {
			printf ("compile-time expected %d, got %d\n", type, var.type);
			exit(0);
		} else {
			return object;
		}
	} else {
		int tag = EMIT("and %%tagged %%%d, %llu", object, TAG_MASK);
		int faillabel = sm_code_get_label (code);
		int ok = sm_code_get_label (code);
		SWITCH("%%tagged %%%d", "label %%fail%d", "i64 %llu, label %%ok%d", tag, faillabel, tagmap[type], ok);

		static int consttmp = -1;
		static const char* str = "runtime expected %llu, got %llu\n";
		int len = strlen(str)+1;
		if (consttmp < 0) {
			PUSH_BLOCK(sm_codegen_get_decls_block (gen));
			consttmp = sm_code_get_temp (code);
			EMIT_ ("@.const%d = private constant [%d x i8] c\"%s\\00\", align 8", consttmp, len, str);
			POP_BLOCK;
		}
		
		LABEL("fail%d", faillabel);
		int strptr = BITCAST("[%d x i8]* @.const%d", "i8*", len, consttmp);
		CALL ("i32 (i8*, ...)* @printf(i8* %%%d, i64 %llu, i64 %%%d)", strptr, tagmap[type], tag);
		RET("void");

		LABEL("ok%d", ok);
		object = EMIT("and %%tagged %%%d, %llu", object, OBJ_MASK);
		/* object = EMIT("shl nuw %%tagged %%%d, 3", object); */
		if (type == TYPE_STR) {
			object = TOPTR("%%tagged %%%d", "i8*", object);
		} else if (type == TYPE_FUN) {
			object = TOPTR("%%tagged %%%d", "%%closure*", object);
		} else if (type == TYPE_INT || type == TYPE_EOS) {
		} else if (type == TYPE_BOOL) {
			object = EMIT("trunc i64 %%%d to i1", object);
		} else if (type == TYPE_LST) {
			object = TOPTR("%%tagged %%%d", "%%list*", object);
		} else {
			assert(FALSE);
		}
		return object;
	}
}

static int create_true_closure (SmCodegen* gen) {
	static int trueclo = -1;
	if (trueclo >= 0) {
		return trueclo;
	}

	GET_CODE;
	int closureid = sm_codegen_begin_closure_func (gen);
	COMMENT("true closure");
	int cont = SPGET(0, "%closure*");
	RUNDBG("-> true object, sp=%p\n", LOADSP, "i64*");

	COMMENT("set true object on the stack");
	STORE("i64 %llu", "i64* %%%d", OBJ_TRUE, LOADSP);
	
	COMMENT("enter cont");
	ENTER(cont);
	sm_codegen_end_closure_func (gen);
	
	trueclo = sm_codegen_create_closure (gen, closureid, -1);
	return trueclo;
}	

static int create_false_closure (SmCodegen* gen) {
	static int falseclo = -1;
	if (falseclo >= 0) {
		return falseclo;
	}

	GET_CODE;
	int closureid = sm_codegen_begin_closure_func (gen);
	COMMENT("false closure");
	int cont = SPGET(0, "%closure*");
	RUNDBG("-> false object, sp=%p\n", LOADSP, "i64*");

	COMMENT("set false object on the stack");
	STORE("i64 %llu", "i64* %%%d", OBJ_FALSE, LOADSP);
	
	COMMENT("enter cont");
	ENTER(cont);
	sm_codegen_end_closure_func (gen);
	
	falseclo = sm_codegen_create_closure (gen, closureid, -1);
	return falseclo;
}

static int create_eos_closure (SmCodegen* gen) {
	static int eosclo = -1;
	if (eosclo >= 0) {
		return eosclo;
	}

	GET_CODE;
	int closureid = sm_codegen_begin_closure_func (gen);
	COMMENT("eos closure");
	RUNDBG("-> eos object, sp=%p\n", LOADSP, "i64*");
	int cont = SPGET(0, "%closure*");

	COMMENT("set eos object on the stack");
	STORE("i64 %llu", "i64* %%%d", OBJ_EOS, LOADSP);
	
	COMMENT("enter cont");
	ENTER(cont);
	sm_codegen_end_closure_func (gen);
	
	eosclo = sm_codegen_create_closure (gen, closureid, -1);
	return eosclo;
}

static int create_prim_binary (SmCodegen* gen, const char* op) {
	// FIXME: create fixed binary closures
	
	GET_CODE;
	
	int closureid = sm_codegen_begin_closure_func (gen);
	COMMENT("binary %s func", op);
	RUNDBG("-> prim binary, sp=%p\n", LOADSP, "i64*");

	COMMENT("right op");
	int right = SPGET(0, NULL);
	COMMENT("left op");
	int left = SPGET(1, NULL);
	COMMENT("get cont");
	int cont = SPGET(2, "%closure*");

	#define CASE(x) (!strcmp(op, x))
	const char* cmp;
	if (CASE("<")) cmp = "slt";
	else if (CASE(">")) cmp = "sgt";
	else if (CASE("<=")) cmp = "sle";
	else if (CASE(">=")) cmp = "sge";
	else if (CASE("==")) cmp = "eq";
	else if (CASE("!=")) cmp = "ne";
	else assert(FALSE);

	SmVar leftvar = { .id=left, .isthunk=FALSE, .type=TYPE_UNK };
	SmVar rightvar = { .id=right, .isthunk=FALSE, .type=TYPE_UNK };
	left = try_var (gen, leftvar, TYPE_INT);
	right = try_var (gen, rightvar, TYPE_INT);
	
	int result = EMIT("icmp %s i64 %%%d, %%%d", cmp, left, right);
	result = EMIT("zext i1 %%%d to i64", result);
	result = EMIT("or i64 %%%d, %llu", result, TAG_OBJ);
	
	COMMENT("set result");
	FINSP(2, result, NULL);

	RUNDBG("enter cont %p\n", cont, "%closure*");
	ENTER(cont);
	sm_codegen_end_closure_func (gen);

	COMMENT("create binary prim for %s", op);
	int closure = sm_codegen_create_closure (gen, closureid, -1);
	return closure;
}

static int create_prim_cond (SmCodegen* gen) {
	static int closure = -1;
	if (closure >= 0) {
		return closure;
	}
	
	GET_CODE;
	
	int closureid = sm_codegen_begin_closure_func (gen);
	COMMENT("prim cond func");
	RUNDBG("-> prim cond, sp=%p\n", LOADSP, "i64*");

	int cond = SPGET(0, NULL);

	SmVar condvar = { .id=cond, .isthunk=FALSE, .type=TYPE_UNK };
	cond = try_var (gen, condvar, TYPE_BOOL);
	BR("i1 %%%d, label %%btrue, label %%bfalse", cond);

	int cont;
	LABEL("btrue");
	cont = SPGET(1, "%closure*");
	VARSP(3);
	RUNDBG("enter true %p\n", cont, "%closure*");
	ENTER(cont);

	LABEL("bfalse");
	cont = SPGET(2, "%closure*");
	VARSP(3);
	RUNDBG("enter false %p\n", cont, "%closure*");
	ENTER(cont);
	
	sm_codegen_end_closure_func (gen);

	COMMENT("create cond prim");
	closure = sm_codegen_create_closure (gen, closureid, -1);
	return closure;
}

DEFUNC(compile_member_expr, SmMemberExpr) {
	GET_CODE;
	if (expr->inner) {
		printf("unsupported inner member\n");
		exit(0);
	}

	if (!strcmp (expr->name, "true")) {
		RETVAL(id=create_true_closure(gen), isthunk=TRUE, type=TYPE_BOOL);
	}

	if (!strcmp (expr->name, "false")) {
		RETVAL(id=create_false_closure(gen), isthunk=TRUE, type=TYPE_BOOL);
	}

	if (!strcmp (expr->name, "eos")) {
		RETVAL(id=create_eos_closure(gen), isthunk=TRUE, type=TYPE_EOS);
	}

	if (!strcmp (expr->name, "_")) {
		printf("cannot anonymous symbol '_'\n");
		exit(0);
	}

	int varid = sm_scope_lookup (sm_codegen_get_scope (gen), expr->name);
	if (varid < 0) {
		printf("not in scope %s\n", expr->name);
		exit(0);
	}

	int parent_size = sm_scope_get_size (sm_scope_get_parent (sm_codegen_get_scope (gen)));

	RUNDBG(g_strdup_printf("-> member %s, sp=%%p\n", expr->name), LOADSP, "i64*");
	int obj;
	if (sm_codegen_get_use_temps (gen)) {
		if (varid < parent_size) {
			COMMENT("member %s(%d) from closure", expr->name, varid);
			// 0 = closure param
			int objptr = GETPTR("%%closure* %%0, i32 0, i32 %d, i32 %d", CLOSURE_SCOPE, varid);
			obj = LOAD("%%closure** %%%d", objptr);
			RUNDBG("use temps, closure member %p\n", obj, "%closure*");
		} else {
			// from the stack
			COMMENT("member %s(%d) from stack", expr->name, varid);
			obj = SPGET(varid-parent_size, "%closure*");
			RUNDBG("stack member %p\n", obj, "%closure*");
		}
	} else {
		// 0 = closure param
		int objptr = GETPTR("%%closure* %%0, i32 0, i32 %d, i32 %d", CLOSURE_SCOPE, varid);
		obj = LOAD("%%closure** %%%d", objptr);
		RUNDBG("no temps, closure member %p\n", obj, "%closure*");
	}
	RETVAL(id=obj, isthunk=TRUE, type=TYPE_UNK);
}

static int create_list_at_closure (SmCodegen* gen, int pos) {
	GET_CODE;

	int closureid = sm_codegen_begin_closure_func (gen);
	COMMENT("list at func for pos %d", pos);
	RUNDBG("-> list at func, sp=%p\n", LOADSP, "i64*");
	int list = SPGET(0, NULL);
	SmVar var = { .id=list, .isthunk=FALSE, .type=TYPE_UNK };
	list = try_var (gen, var, TYPE_LST);

	COMMENT("get element at pos %d", pos);
	int elem = GETPTR("%%list* %%%d, i32 0, i32 %d, i32 %d", list, LIST_ELEMS, pos);
	elem = LOAD("%%closure** %%%d", elem);

	// pop argument
	VARSP(1);

	RUNDBG("enter elem %p\n", elem, "%closure*");
	ENTER(elem);
	sm_codegen_end_closure_func (gen);

	COMMENT("create match pos %d closure", pos);
	int closure = sm_codegen_create_custom_closure (gen, 0, closureid);
	return closure;
}

static void create_match_closure (SmCodegen* gen, int prealloc, int thunk, int pos) {
	GET_CODE;

	int closureid = sm_codegen_begin_closure_func (gen);
	COMMENT("match func for thunk %%%d at pos %d", thunk, pos);
	RUNDBG("-> match func, sp=%p\n", LOADSP, "i64*");
	
	COMMENT("get list thunk from closure");
	int list = GETPTR("%%closure* %%0, i32 0, i32 %d, i32 0", CLOSURE_SCOPE);
	list = LOAD("%%closure** %%%d", list);

	COMMENT("push list at %d closure", pos);
	int posclo = create_list_at_closure (gen, pos);
	FINSP(-1, posclo, "%closure*");
	
	RUNDBG("enter %p\n", list, "%closure*");
	ENTER(list);
	sm_codegen_end_closure_func (gen);

	COMMENT("store func in match closure");
	int ptr = GETPTR("%%closure* %%%d, i32 0, i32 %d", prealloc, CLOSURE_FUNC);
	STORE("%%closurefunc " FUNC("closure_%d_eval"), "%%closurefunc* %%%d", closureid, ptr);

	COMMENT("store thunk in match closure");
	ptr = GETPTR("%%closure* %%%d, i32 0, i32 %d, i32 0", prealloc, CLOSURE_SCOPE);
	STORE("%%closure* %%%d", "%%closure** %%%d", thunk, ptr);
}

DEFUNC(compile_seq_expr, SmSeqExpr) {
	GET_CODE;

	SmFuncExpr* func = (expr->base.parent && expr->base.parent->type == SM_FUNC_EXPR) ? (SmFuncExpr*) expr->base.parent : NULL;

	SmScope* scope = sm_scope_new (sm_codegen_get_scope (gen));
	sm_codegen_set_scope (gen, scope);

	int nparams = func ? func->params->len : 0;
	
	int closureid = sm_codegen_begin_closure_func (gen);
	sm_codegen_set_use_temps (gen, TRUE);
	COMMENT("seq/func closure");
	RUNDBG("-> seq, sp=%p\n", LOADSP, "i64*");
	
	int nlocals = 0;
	/* assign ids to locals and preallocate thunks */
	/* as a big lazy hack, keep track of the number of temporaries that we use to allocate a closure */
	int cur_alloc = 0;
	int temp_diff = 0;
	for (int i=0; i < expr->assigns->len; i++) {
		SmAssignExpr* assign = (SmAssignExpr*) expr->assigns->pdata[i];
		for (int j=0; j < assign->names->len; j++) {
			const char* name = (const char*) assign->names->pdata[j];
			if (!strcmp (name, "_")) {
				continue;
			}

			int existing = sm_scope_lookup (scope, name);
			if (existing >= 0) {
				printf("shadowing %s\n", name);
				exit(0);
			}

			int alloc = sm_codegen_allocate_closure (gen);
			temp_diff = alloc-cur_alloc;
			cur_alloc = alloc;
			SPSET(-nlocals-1, alloc, "%closure*");

			sm_scope_set (scope, name, nlocals++);
		}
	}

	/* assign ids to arguments */
	for (int i=0; i < nparams; i++) {
		const char* name = (const char*) func->params->pdata[i];
		
		int existing = sm_scope_lookup (scope, name);
		if (existing >= 0) {
			printf("shadowing %s\n", name);
			exit(0);
		}

		sm_scope_set (scope, name, nlocals+i);
	}
	
	// make room for locals
	VARSP(-nlocals);

	/* visit assignments */
	for (int i=0; i < expr->assigns->len; i++) {
		SmAssignExpr* assign = (SmAssignExpr*) expr->assigns->pdata[i];
		GPtrArray* names = assign->names;
		if (names->len == 1) {
			const char* name = (const char*) names->pdata[0];
			COMMENT("assign for %s(%d)", name, i);
			RUNDBG("assign %p\n", cur_alloc, "%closure*");
			(void) call_compile_table (gen, EXPR(assign->value), cur_alloc);
			cur_alloc -= temp_diff;
		} else {
			SmVar var = VISIT(assign->value);
			for (int j=0; j < names->len; j++) {
				const char* name = (const char*) names->pdata[j];
				if (!strcmp (name, "_")) {
					continue;
				}
				
				RUNDBG("assign match %p\n", cur_alloc, "%closure*");
				create_match_closure (gen, cur_alloc, var.id, j);
				cur_alloc -= temp_diff;
			}
		}
	}

	COMMENT("visit seq result");
	SmVar result = VISIT(expr->result);
	COMMENT("pop parameters and locals");
	VARSP(nparams+nlocals);
	COMMENT("enter result");
	RUNDBG("enter %p\n", result.id, "%closure*");
	ENTER(result.id);
	sm_codegen_end_closure_func (gen);

	sm_codegen_set_scope (gen, sm_scope_get_parent (scope));
	sm_scope_free (scope);

	COMMENT("create seq closure");
	COMMENT("ast: %s", g_strescape (sm_ast_dump(EXPR(expr)), NULL));
	int closure = sm_codegen_create_closure (gen, closureid, prealloc);

	if (!func) {
		RETVAL(id=closure, isthunk=TRUE, type=result.type);
	} else {
		// closure object
		COMMENT("tag closure");
		closure = TOINT("%%closure* %%%d", "%%tagged", closure);
		/* closure = EMIT("lshr exact %%tagged %%%d, 3", closure); */
		closure = EMIT("or %%tagged %%%d, %llu", closure, TAG_FUN);
		RETVAL(id=closure, isthunk=FALSE, type=TYPE_FUN);
	}
}

DEFUNC(compile_func_expr, SmFuncExpr) {
	GET_CODE;
	int closureid = sm_codegen_begin_closure_func (gen);
	COMMENT("func thunk");
	COMMENT("get cont");
	int cont = SPGET(0, "%closure*");
	RUNDBG("-> func, sp=%p\n", 1, "i64*");
	
	COMMENT("visit body");
	SmVar result = VISIT(expr->body);
	COMMENT("push func object");
	SPSET(0, result.id, NULL);
	
	COMMENT("enter cont");
	RUNDBG("enter %p\n", cont, "%closure*");
	ENTER(cont);
	sm_codegen_end_closure_func (gen);
	
	int closure = sm_codegen_create_closure (gen, closureid, prealloc);
	RETVAL(id=closure, isthunk=TRUE, type=TYPE_FUN);
}

DEFUNC(compile_int_literal, SmLiteral) {
	GET_CODE;
	// FIXME: do not create a thunk

	int closureid = sm_codegen_begin_closure_func (gen);
	COMMENT("int thunk code for %d\n", expr->intval);
	RUNDBG("-> int literal, sp=%p\n", LOADSP, "i64*");
	
	int cont = SPGET(0, "%closure*");
	STORE("i64 %llu", "i64* %%%d", TAG_INT|expr->intval, LOADSP);
	
	RUNDBG("enter %p\n", cont, "%closure*");
	ENTER(cont);
	sm_codegen_end_closure_func (gen);
	
	// build thunk
	COMMENT("create int thunk");
	int closure = sm_codegen_create_closure (gen, closureid, prealloc);
	RETVAL(id=closure, isthunk=TRUE, type=TYPE_INT);
}

DEFUNC(compile_chr_literal, SmLiteral) {
	GET_CODE;
	// FIXME: do not create a thunk

	int closureid = sm_codegen_begin_closure_func (gen);
	COMMENT("chr thunk code for %x\n", expr->chr);
	RUNDBG("-> chr literal, sp=%p\n", LOADSP, "i64*");
	
	int cont = SPGET(0, "%closure*");
	STORE("i64 %llu", "i64* %%%d", TAG_CHR|expr->chr, LOADSP);
	
	RUNDBG("enter %p\n", cont, "%closure*");
	ENTER(cont);
	sm_codegen_end_closure_func (gen);
	
	// build thunk
	COMMENT("create chr thunk");
	int closure = sm_codegen_create_closure (gen, closureid, prealloc);
	RETVAL(id=closure, isthunk=TRUE, type=TYPE_CHR);
}

DEFUNC(compile_str_literal, SmLiteral) {
	GET_CODE;
	// define constant string
	// FIXME: do not create a thunk
	
	PUSH_BLOCK(sm_codegen_get_decls_block (gen));
	int consttmp = sm_code_get_temp (code);
	int len = strlen(expr->str)+1;
// FIXME:
	EMIT_ ("@.const%d = private constant [%d x i8] c\"%s\\00\", align 8", consttmp, len, expr->str);
	POP_BLOCK;
	
	// expression code
	int closureid = sm_codegen_begin_closure_func (gen);
	COMMENT("string thunk code for '%s' string", expr->str);
	RUNDBG("-> string literal, sp=%p\n", LOADSP, "i64*");
	
	int cont = SPGET(0, "%closure*");
	int obj = GETPTR("[%d x i8]* @.const%d, i32 0, i32 0", len, consttmp);
	obj = EMIT ("ptrtoint i8* %%%d to %%tagged", obj);
	/* obj = EMIT ("lshr exact %%tagged %%%d, 3", obj); */
	obj = EMIT ("or %%tagged %%%d, %llu", obj, TAG_STR);
	SPSET(0, obj, NULL);
	
	RUNDBG("enter %p\n", cont, "%closure*");
	ENTER(cont);
	sm_codegen_end_closure_func (gen);
	
	// build thunk
	COMMENT("create string thunk");
	int closure = sm_codegen_create_closure (gen, closureid, prealloc);
	RETVAL(id=closure, isthunk=TRUE, type=TYPE_STR);
}

static int create_real_call_closure (SmCodegen* gen, SmCallExpr* expr) {
	GET_CODE;

	int closureid = sm_codegen_begin_closure_func (gen);
	COMMENT("real call func");

	COMMENT("get func");
	RUNDBG("-> real call, sp=%p\n", LOADSP, "i64*");
	int func = SPGET(0, NULL);

	SmVar funcvar = { .id=func, .isthunk=FALSE, .type=TYPE_UNK };
	func = try_var (gen, funcvar, TYPE_FUN);

	COMMENT("set arguments");
	for (int i=0; i < expr->args->len; i++) {
		COMMENT("visit arg %d", i);
		SmVar arg = VISIT(EXPR(expr->args->pdata[i]));
		SPSET(i-expr->args->len+1, arg.id, "%closure*");
	}
	
	COMMENT("push args onto the stack");
	VARSP(-expr->args->len+1);
	COMMENT("enter real func");
	RUNDBG("enter %p\n", func, "%closure*");
	ENTER(func);

	sm_codegen_end_closure_func (gen);

	COMMENT("create real call closure");
	int closure = sm_codegen_create_closure (gen, closureid, -1);
	return closure;
}

DEFUNC(compile_call_expr, SmCallExpr) {
	GET_CODE;

	int closureid = sm_codegen_begin_closure_func (gen);
	COMMENT("call thunk func");
	RUNDBG("-> call, sp=%p\n", LOADSP, "i64*");

	int off = sm_codegen_push_update_frame (gen, -1);

	const char* prim_name = NULL;
	if (expr->func->type == SM_MEMBER_EXPR) {
		SmMemberExpr* member = (SmMemberExpr*) expr->func;
		if (strstr (member->name, "prim") == member->name) {
			prim_name = member->name;
		}
	}

	if (prim_name) {
		int prim = LOAD("%%closure** @%s", prim_name);
		RUNDBG("prim call %p\n", prim, "%closure*");

		FINSP(off, prim, "%closure*");

		// only support one argument for now
		SmVar arg = VISIT(EXPR(expr->args->pdata[0]));
		RUNDBG("enter arg %p", arg.id, "%closure*");
		ENTER(arg.id);
	} else {
		COMMENT("visit func");
		SmVar func = VISIT(expr->func);
		
		int realfunc = create_real_call_closure (gen, expr);
		FINSP(off, realfunc, "%closure*");
		
		COMMENT("force func");
		RUNDBG("enter %p\n", func.id, "%closure*");
		ENTER(func.id);
	}		
	sm_codegen_end_closure_func (gen);
	
	// build thunk
	COMMENT("create call thunk");
	int closure = sm_codegen_create_closure (gen, closureid, prealloc);
	RETVAL(id=closure, isthunk=TRUE, type=TYPE_UNK);
}

DEFUNC(compile_binary_expr, SmBinaryExpr) {
	GET_CODE;

	// eval left closure
	int evaleft = sm_codegen_begin_closure_func (gen);
	COMMENT("eval right op %s thunk func", expr->op);
	RUNDBG("-> right op thunk, sp=%p\n", LOADSP, "i64*");
	int left = SPGET(0, NULL);
	int right = SPGET(1, "%closure*");

	COMMENT("swap");
	SPSET(1, left, NULL);
	
	COMMENT("create prim binary closure");
	int prim = create_prim_binary (gen, expr->op);
	SPSET(0, prim, "%closure*");

	COMMENT("enter right");
	RUNDBG("enter right %p\n", right, "%closure*");
	ENTER(right);
	sm_codegen_end_closure_func (gen);

	// binary closure
	int closureid = sm_codegen_begin_closure_func (gen);
	RUNDBG("-> binary func, sp=%p\n", LOADSP, "i64*");
	
	COMMENT("visit left");
	SmVar leftvar = VISIT(expr->left);
	COMMENT("visit right");
	SmVar rightvar = VISIT(expr->right);
	COMMENT("create eval left thunk");
	int evalclo = sm_codegen_create_closure (gen, evaleft, -1);

	SPSET(-1, rightvar.id, "%closure*");
	FINSP(-2, evalclo, "%closure*");
	
	COMMENT("force left");
	RUNDBG("enter %p\n", leftvar.id, "%closure*");
	ENTER(leftvar.id);
	
	sm_codegen_end_closure_func (gen);
	
	// build thunk
	COMMENT("create binary thunk");
	int closure = sm_codegen_create_closure (gen, closureid, prealloc);
	RETVAL(id=closure, isthunk=TRUE, type=TYPE_UNK);
}

DEFUNC(compile_cond_expr, SmCondExpr) {
	GET_CODE;

	int closureid = sm_codegen_begin_closure_func (gen);
	COMMENT("cond closure func");

	COMMENT("visit cond");
	SmVar cond = VISIT(expr->cond);
	COMMENT("visit true");
	SmVar truebody = VISIT(expr->truebody);
	COMMENT("visit false");
	SmVar falsebody = VISIT(expr->falsebody);

	COMMENT("create prim cond closure");
	int prim = create_prim_cond (gen);
	SPSET(-1, falsebody.id, "%closure*");
	SPSET(-2, truebody.id, "%closure*");
	FINSP(-3, prim, "%closure*");
	
	COMMENT("force cond");
	RUNDBG("enter cond %p\n", cond.id, "%closure*");
	ENTER(cond.id);
	
	sm_codegen_end_closure_func (gen);
	
	COMMENT("create cond thunk");
	int closure = sm_codegen_create_closure (gen, closureid, prealloc);
	RETVAL(id=closure, isthunk=TRUE, type=TYPE_BOOL);
}

DEFUNC(compile_list_expr, SmListExpr) {
	GET_CODE;

	int closureid = sm_codegen_begin_closure_func (gen);
	COMMENT("list closure func");
	RUNDBG("-> list closure, sp=%p\n", LOADSP, "i64*");
	int cont = SPGET(0, "%closure*");
	
	COMMENT("allocate list of size %d", expr->elems->len);
	VARHP(-(int)(sizeof(void*)*LIST_ELEMS + sizeof(void*)*expr->elems->len));
	int list = LOADHP;
	list = BITCAST("i64* %%%d", "%%list*", list);

	COMMENT("assign size to list");
	int size = GETPTR("%%list* %%%d, i32 0, i32 0", list);
	STORE("i64 %d", "i64* %%%d", expr->elems->len, size);

	for (int i=0; i < expr->elems->len; i++) {
		SmExpr* elem = EXPR(expr->elems->pdata[i]);
		COMMENT("visit element %d\n", i);
		SmVar var = VISIT(elem);
		COMMENT("assign element %d to list\n", i);
		int ptr = GETPTR("%%list* %%%d, i32 0, i32 %d, i32 %d", list, LIST_ELEMS, i);
		STORE("%%closure* %%%d", "%%closure** %%%d", var.id, ptr);
	}

	COMMENT("tag list");
	list = TOINT("%%list* %%%d", "%%tagged", list);
	list = EMIT("or %%tagged %%%d, %llu", list, TAG_LST);

	SPSET(0, list, NULL);
	
	RUNDBG("enter cont %p\n", cont, "%closure*");
	ENTER(cont);
	sm_codegen_end_closure_func (gen);

	COMMENT("create list thunk");
	int closure = sm_codegen_create_closure (gen, closureid, prealloc);
	RETVAL(id=closure, isthunk=TRUE, type=TYPE_LST);
}

#define CAST(x) (SmVar (*)(SmCodegen*, SmExpr*, int prealloc))(x)
SmVar (*compile_table[])(SmCodegen*, SmExpr*, int prealloc) = {
	[SM_MEMBER_EXPR] = CAST(compile_member_expr),
	[SM_SEQ_EXPR] = CAST(compile_seq_expr),
	[SM_STR_LITERAL] = CAST(compile_str_literal),
	[SM_INT_LITERAL] = CAST(compile_int_literal),
	[SM_CHR_LITERAL] = CAST(compile_chr_literal),
	[SM_FUNC_EXPR] = CAST(compile_func_expr),
	[SM_CALL_EXPR] = CAST(compile_call_expr),
	[SM_BINARY_EXPR] = CAST(compile_binary_expr),
	[SM_COND_EXPR] = CAST(compile_cond_expr),
	[SM_LIST_EXPR] = CAST(compile_list_expr)
};

static SmVar call_compile_table (SmCodegen* gen, SmExpr* expr, int prealloc) {
	return compile_table[expr->type](gen, expr, prealloc);
}

static int create_nop_closure (SmCodegen* gen) {
	GET_CODE;
	int nopid = sm_codegen_begin_closure_func (gen);
	
	COMMENT("nop func"); // discards one object from the stack
	RUNDBG("nop, sp=%d\n", LOADSP, "i64*");
	// end of the program
	RET("void");

	sm_codegen_end_closure_func (gen);
	COMMENT("nop closure");
	
	int nopclo = sm_codegen_create_custom_closure (gen, 0, nopid);
	return nopclo;
}

SmJit* sm_compile (SmCodegenOpts opts, const char* name, SmExpr* expr) {
	static int initialized = 0;
	if (!initialized) {
		initialized = 1;
		sm_jit_init ();
	}

	SmCodegen* gen = sm_codegen_new (opts);
	GET_CODE;
	
	PUSH_BLOCK(sm_codegen_get_decls_block (gen));
	DECLARE ("i32 @printf(i8*, ...)");
	DECLARE ("i8* @aligned_alloc(i32, i32)");
	DECLARE ("void @llvm.memcpy.p0i8.p0i8.i32(i8*, i8*, i32, i32, i1)");
	DECLARE ("void @llvm.donothing() nounwind readnone");
	DECLARE ("void @llvm.debugtrap() nounwind");
	EMIT_ ("%%tagged = type i64");
	EMIT_ ("%%closurefunc = type void (%%closure*, i64*, i64*)*");
	EMIT_ ("@stack = global i64* null, align 8");
	EMIT_ ("@heap = global i64* null, align 8");
	DEFINE_STRUCT ("closure", "%%closurefunc, %%tagged, [0 x %%closure*]"); // func, cached object, scope
	DEFINE_STRUCT ("list", "i64, [0 x %%closure*]"); // size, thunks
	POP_BLOCK;

	PUSH_NEW_BLOCK;
	BEGIN_FUNC("void", "main", "");
	COMMENT("main");
	LABEL("entry");

	COMMENT("alloc stack");
	int stack = CALL("i8* @aligned_alloc(i32 8, i32 %d)", (int)(4096*sizeof(void*)));
	stack = BITCAST("i8* %%%d", "i64*", stack);
	STORE("i64* %%%d", "i64** @stack", stack);
	
	sm_codegen_set_stack_pointer (gen, stack);
	VARSP(4096-8);
	RUNDBG("top stack=%p\n", stack, "i64*");
	RUNDBG("bottom sp=%p\n", LOADSP, "i64*");

	COMMENT("alloc heap");
	int heap = CALL("i8* @aligned_alloc(i32 8, i32 %d)", (int)(4096*sizeof(void*)));
	heap = BITCAST("i8* %%%d", "i64*", heap);
	STORE("i64* %%%d", "i64** @heap", heap);
	
	sm_codegen_set_heap_pointer (gen, heap);
	VARHP(4096-8);
	RUNDBG("top heap=%p\n", heap, "i64*");
	RUNDBG("bottom hp=%p\n", LOADHP, "i64*");	

	int nopclo = create_nop_closure (gen);
	sm_codegen_init_update_frame (gen);
	sm_prim_print (gen);

	COMMENT("push nop");
	SPSET(0, nopclo, "%closure*");

	COMMENT("visit root expression");
	SmVar root = VISIT(expr);
	RUNDBG("enter root %p\n", root.id, "%closure*");
	ENTER(root.id);
	/* RET("void"); */
	END_FUNC;
	POP_BLOCK;

	char* unit = sm_code_link (code);
	/* puts(unit); */
	sm_code_unref (code);
	
	SmJit* mod = sm_jit_compile ("<stdin>", unit);
	/* free (unit); */

	return mod;
}

void sm_run (SmJit* mod) {
	void (*entrypoint)() = (void (*)()) sm_jit_get_function (mod, FUNC("main"));
	if (!entrypoint) {
		return;
	}
	entrypoint();
}

