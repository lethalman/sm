#include <stdlib.h>
#include <stdio.h>

#include "uthash/src/utlist.h"
#include "uthash/src/utarray.h"

#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "astdumper.h"

struct _SmParser {
	SmLexer lexer;
	SmToken cur;
};

SmParser* sm_parser_new (void) {
	SmParser* parser = (SmParser*) calloc (1, sizeof (SmParser));
	return parser;
}

void sm_parser_free (SmParser* parser) {
	sm_token_destroy (&parser->cur);
	free (parser);
}

#define NEXT (sm_token_destroy(&parser->cur), parser->cur = sm_lexer_next(&parser->lexer), parser->cur)
#define PTOK(t) puts(t.type)
#define FUNC(n) static SmExpr* n (SmParser* parser)
#define FUNC2(n) static SmExpr* n (SmParser* parser, SmExpr* inner)
#define TYPE (parser->cur.type)
#define EXPECT(x) if (TYPE != x) { puts("expected " x); return NULL; }
#define ACCEPT(x) ((TYPE == x) ? (NEXT, 1) : 0)
#define ACCEPT_ID(x) ((TYPE == "id") ? (!strcmp(STR, x) ? (NEXT, 1) : 0) : 0)
#define SKIP(x) EXPECT(x); NEXT;
#define STR (parser->cur.str)
#define NEW(n,x,t) x* n = (x*)calloc(1, sizeof(x)); (n)->base.type=t;
#define SAVE (*parser)
#define RESTORE(x) sm_token_destroy(&parser->cur); *parser = x
#define CHECK(x) if (!x) return NULL
#define CASE(x) (TYPE == x)

static char* identifier (SmParser* parser) {
	EXPECT("id");
	char* val = STR;
	STR=NULL;
	NEXT;
	return val;
}

FUNC2(member) {
	char* id = identifier(parser);
	CHECK(id);
	
	NEW(expr, SmMemberExpr, SM_MEMBER_EXPR);
	expr->inner = inner;
	expr->name = id;
	return EXPR(expr);
}

FUNC(primary) {
	SmExpr* expr = NULL;
	if (CASE("id")) {
		expr = member(parser, NULL);
	} else if (CASE("str")) {
		NEW(tmp, SmLiteral, SM_LITERAL);
		tmp->str = STR;
		STR=NULL;
		NEXT;
		expr = EXPR(tmp);
	} else {
		printf("unexpected %s", TYPE);
		return NULL;
	}
	
	return expr;
}

FUNC(assign) {
	SmParser begin = SAVE;

	if (CASE("id")) {
		char* name = identifier(parser);
		UT_array* names;
		utarray_new (names, &ut_str_icd);
		utarray_push_back (names, &name);

		while (ACCEPT (",")) {
			if (CASE("id")) {
				utarray_push_back (names, &STR);
				STR=NULL;
			} else {
				goto rollback;
			}
		}

		if (ACCEPT ("=")) {
			SmExpr* expr = primary(parser);
			CHECK(expr);
			NEW(a, SmAssignExpr, SM_ASSIGN_EXPR);
			a->names = names;
			a->value = expr;
			return EXPR(a);
		} else {
			goto rollback;
		}

	rollback:
		utarray_free (names);
		RESTORE(begin);
	}
		
	return primary(parser);
}

FUNC(seq) {
	SmExpr* expr = assign(parser);
	CHECK(expr);
	if (expr->type != SM_ASSIGN_EXPR) {
		return expr;
	}

	NEW(seq, SmSeqExpr, SM_SEQ_EXPR);
	while (ACCEPT(";")) {
		SmAssignList* entry = (SmAssignList*) calloc(1, sizeof(SmAssignList));
		entry->expr = (SmAssignExpr*) expr;
		DL_APPEND(seq->assigns, entry);

		expr = assign(parser);
		CHECK(expr);
		if (expr->type != SM_ASSIGN_EXPR) {
			seq->result = expr;
			break;
		}
	}
	
	return EXPR(seq);
}

SmExpr* sm_parser_parse (SmParser* parser, SmLexer lexer) {
	parser->lexer = lexer;
	parser->cur = {0};
	
	NEXT;
	SmExpr* expr = seq(parser);
	CHECK(expr);
	EXPECT("eof");
	return expr;
}

