/*
** SCFA dialect port "keywords": the syntax the Supreme Commander: Forged Alliance
** Lua (GPG's Lua 5.0.1) adds on top of stock Lua. Core independent.
**
** A core (Lua 5.0 or 5.1) uses this port in two ways:
**  1. the X-macro lists below expand into the token enum, token strings, operator
**     enum, opcode enum/names/modes and priority table (order matters: the lists are
**     inserted at the places marked "SCFA" in the core, which keeps the exe's order);
**  2. small hook calls whose implementation lives in the matching adapter header
**     (lua-5_0-adapters/keywords/scfa_keywords_impl.h or lua-5_1-adapters/...), selected
**     by the include path. The hooks are `static', defined in the adapter and included
**     into the core file that calls them (guarded by SCFA_IN_<FILE>).
**
** Hooks (name: contract):
**   scfa_lex_hook(ls, seminfo, &tok)  llex.c, at the top of the token loop. Returns
**        SCFA_LEX_PASS (not handled), SCFA_LEX_AGAIN (input consumed, scan again) or
**        SCFA_LEX_TOKEN (return `tok'). Implements `#' comments, `!=', `<<', `>>'.
**   scfa_lex_start(ls)                llex.c, after the first character is read. Skips a BOM.
**   scfa_getbinopr(tok)               lparser.c, default case of getbinopr: maps `& | << >> ^'.
**   scfa_statement(ls)                lparser.c, default case of statement(): parses `continue'.
**                                     Returns 1 if it parsed a statement that must be the last one.
**   scfa_block_init(bl)               lparser.c, enterblock: initialise the per-block fields.
**   scfa_loop_continue_here(fs, bl)   lparser.c, the point where `continue' lands in a loop.
**   scfa_repeat_cond_begin(fs, bl)    lparser.c, repeatstat before the until-condition.
**   scfa_repeat_cond_end(ls)          lparser.c, repeatstat after the until-condition.
**   scfa_constructor_hints(ls, hint)  lparser.c, constructor, right after `{': `&hash &array'.
**   scfa_constructor_sizes(&nh,&na,h) lparser.c, constructor, end: apply the hints.
**   scfa_loadfile_skip(...)           lauxlib.c, luaL_loadfile: skip a BOM (5.1 only; 5.0 lexer does it).
*/

#ifndef scfa_keywords_h
#define scfa_keywords_h

/* hook results for scfa_lex_hook */
#define SCFA_LEX_PASS	0
#define SCFA_LEX_AGAIN	1
#define SCFA_LEX_TOKEN	2

/* reserved words added by the dialect, placed after `break' */
#define SCFA_RESERVED_WORDS(X)	X(CONTINUE, "continue")

/* extra (non reserved) tokens, placed after the string token, before <eof> */
#define SCFA_EXTRA_TOKENS(X)	X(BSHL, "<<") X(BSHR, ">>")

/* bitwise binary operators in operator/opcode order: X(name, left priority, right priority)
   `&' `|' `<<' `>>' bind tighter than `*' and `/', left associative */
#define SCFA_BIT_OPS(X)	X(BAND, 8, 8) X(BOR, 8, 8) X(BSHL, 8, 8) X(BSHR, 8, 8)

/* the xor operator replaces `^' (pow): same slot, priorities as the stock pow */
#define SCFA_XOR_LEFT	10
#define SCFA_XOR_RIGHT	9

/* ---- expansions used by the cores ---- */
#define SCFA_X_TK(n, s)		TK_##n,
#define SCFA_X_STR(n, s)	s,
#define SCFA_TK_RESERVED	SCFA_RESERVED_WORDS(SCFA_X_TK)
#define SCFA_STR_RESERVED	SCFA_RESERVED_WORDS(SCFA_X_STR)
#define SCFA_TK_EXTRA		SCFA_EXTRA_TOKENS(SCFA_X_TK)
#define SCFA_STR_EXTRA		SCFA_EXTRA_TOKENS(SCFA_X_STR)

#define SCFA_X_OPR(n, l, r)	OPR_##n,
#define SCFA_X_OP(n, l, r)	OP_##n,
#define SCFA_X_OPNAME(n, l, r)	#n,
#define SCFA_X_PRIO(n, l, r)	{l, r},
#define SCFA_X_OPCASE(n, l, r)	case OP_##n:
#define SCFA_X_OPRCASE(n, l, r)	case OPR_##n:
#define SCFA_X_CODE(n, l, r)	case OPR_##n: codearith(fs, OP_##n, e1, e2); break;
#define SCFA_OPR_BITS		SCFA_BIT_OPS(SCFA_X_OPR)
#define SCFA_OP_BITS		SCFA_BIT_OPS(SCFA_X_OP)
#define SCFA_OPNAMES_BITS	SCFA_BIT_OPS(SCFA_X_OPNAME)
#define SCFA_PRIORITY_BITS	SCFA_BIT_OPS(SCFA_X_PRIO)
#define SCFA_OPCASES_BITS	SCFA_BIT_OPS(SCFA_X_OPCASE)
#define SCFA_OPRCASES_BITS	SCFA_BIT_OPS(SCFA_X_OPRCASE)
#define SCFA_CODE_BITS		SCFA_BIT_OPS(SCFA_X_CODE)

/* ---- state the dialect adds to parser/loader structures (struct members) ---- */
/* BlockCnt: `continue' jumps of a loop, fewest active locals at any `continue' */
#define SCFA_BLOCK_FIELDS	int contlist; int contnact;
/* FuncState: first local a `continue' would skip (repeat-until check) and whether one was read */
#define SCFA_FUNCSTATE_FIELDS	int contwatch; int conthit;
#define SCFA_FUNCSTATE_INIT(fs)	((fs)->contwatch = -1, (fs)->conthit = 0)
#define SCFA_NOTE_LOCAL_READ(fs, i) ((void)(((fs)->contwatch >= 0 && (i) >= (fs)->contwatch) ? ((fs)->conthit = 1) : 0))
/* LoadF (luaL_loadfile): characters read while looking for a BOM */
#define SCFA_LOADF_FIELDS	int npre; char pre[3];

#endif
