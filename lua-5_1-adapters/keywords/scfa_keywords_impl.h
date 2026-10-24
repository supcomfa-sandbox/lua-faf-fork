/*
** SCFA dialect port "keywords", adapter for the Lua 5.1 core.
** Implements the hooks of lua5_0-api/keywords/scfa_keywords.h. This header is
** included by llex.c, lparser.c and lauxlib.c, each with its SCFA_IN_<FILE> macro
** defined, and only compiles the section of the including file.
*/

#include "scfa_keywords.h"


/* ------------------------------------------------------------------ llex.c */
#if defined(SCFA_IN_LLEX)

/* `#' short comments, `!=' (synonym of `~='), and the `<<' `>>' tokens */
static int scfa_lex_hook (LexState *ls, SemInfo *seminfo, int *tok) {
  (void)seminfo;
  switch (ls->current) {
    case '#': {  /* the length operator is not available: `#' starts a comment */
      while (!currIsNewline(ls) && ls->current != EOZ)
        next(ls);
      return SCFA_LEX_AGAIN;
    }
    case '<': {
      next(ls);
      if (ls->current == '=') { next(ls); *tok = TK_LE; }
      else if (ls->current == '<') { next(ls); *tok = TK_BSHL; }
      else *tok = '<';
      return SCFA_LEX_TOKEN;
    }
    case '>': {
      next(ls);
      if (ls->current == '=') { next(ls); *tok = TK_GE; }
      else if (ls->current == '>') { next(ls); *tok = TK_BSHR; }
      else *tok = '>';
      return SCFA_LEX_TOKEN;
    }
    case '!': {
      next(ls);
      *tok = (ls->current != '=') ? '!' : (next(ls), TK_NE);
      return SCFA_LEX_TOKEN;
    }
    default: return SCFA_LEX_PASS;
  }
}


/* skip a UTF-8 BOM at the start of any chunk (files and strings) */
static void scfa_lex_start (LexState *ls) {
  if (ls->current == 0xEF) {
    next(ls);
    if (ls->current != 0xBB) luaX_lexerror(ls, "malformed UTF-8 byte order mark", 0);  /* a lone EF is not swallowed */
    next(ls);
    if (ls->current != 0xBF) luaX_lexerror(ls, "malformed UTF-8 byte order mark", 0);  /* nor is EF BB */
    next(ls);
  }
}

#endif


/* --------------------------------------------------------------- lparser.c */
#if defined(SCFA_IN_LPARSER)

static BinOpr scfa_getbinopr (int op) {
  switch (op) {
    case '&': return OPR_BAND;
    case '|': return OPR_BOR;
    case TK_BSHL: return OPR_BSHL;
    case TK_BSHR: return OPR_BSHR;
    default: return OPR_NOBINOPR;
  }
}


static void scfa_block_init (BlockCnt *bl) {
  bl->contlist = NO_JUMP;
  bl->contnact = 255;
}


/* stat -> CONTINUE: a real jump to the loop's next-iteration point */
static void scfa_continuestat (LexState *ls) {
  FuncState *fs = ls->fs;
  BlockCnt *bl = fs->bl;
  int upval = 0;
  luaX_next(ls);  /* skip CONTINUE */
  while (bl && !bl->isbreakable) {
    upval |= bl->upval;
    bl = bl->previous;
  }
  if (!bl)
    luaX_syntaxerror(ls, "no loop to continue");
  if (upval)
    luaK_codeABC(fs, OP_CLOSE, bl->nactvar, 0, 0);
  if (fs->nactvar < bl->contnact) bl->contnact = fs->nactvar;
  luaK_concat(fs, &bl->contlist, luaK_jump(fs));
}


/* returns 1 if a statement was parsed (it must be the last of its block) */
static int scfa_statement (LexState *ls) {
  if (ls->t.token == TK_CONTINUE) {
    scfa_continuestat(ls);
    return 1;
  }
  return 0;
}


/* `continue' lands here: after the loop body, before the re-test / step */
static void scfa_loop_continue_here (FuncState *fs, BlockCnt *bl) {
  luaK_patchtohere(fs, bl->contlist);
}


/* repeat-until: `continue' goes to the condition. Locals declared after a
   `continue' are uninitialised on that path, so the condition must not read them. */
static void scfa_repeat_cond_begin (FuncState *fs, BlockCnt *bl) {
  luaK_patchtohere(fs, bl->contlist);
  if (bl->contlist != NO_JUMP && bl->contnact < fs->nactvar) {
    fs->contwatch = bl->contnact;
    fs->conthit = 0;
  }
}


static void scfa_repeat_cond_end (LexState *ls) {
  FuncState *fs = ls->fs;
  if (fs->contwatch >= 0) {
    int hit = fs->conthit;
    fs->contwatch = -1;
    if (hit)
      luaX_syntaxerror(ls, "`continue' jumps into the scope of a local used by the condition");
  }
}


/* table constructor size hints `{&hash &array ...}' (LuaPlus): parsed, only used as size hints */
static void scfa_constructor_hints (LexState *ls, int *hint) {
  int nhint = 0;
  hint[0] = hint[1] = 0;
  while (ls->t.token == '&') {
    luaX_next(ls);
    if (ls->t.token != TK_NUMBER)
      luaX_syntaxerror(ls, "table size hint (number) expected after `&'");
    if (nhint < 2) {  /* clamp: hints are only a performance hint */
      lua_Number r = ls->t.seminfo.r;
      hint[nhint++] = (r > 0 && r < (1 << 20)) ? cast_int(r) : 0;
    }
    luaX_next(ls);
  }
  if (nhint > 0 && !testnext(ls, ',')) testnext(ls, ';');
}


static void scfa_constructor_sizes (int *nh, int *na, const int *hint) {
  if (hint[0] > *nh) *nh = hint[0];
  if (hint[1] > *na) *na = hint[1];
}

#endif


/* --------------------------------------------------------------- lauxlib.c */
#if defined(SCFA_IN_LAUXLIB)

/* luaL_loadfile: skip an optional UTF-8 BOM; returns the first character after it */
static int scfa_loadf_skipBOM (LoadF *lf) {
  static const char bom[] = "\xEF\xBB\xBF";
  int c, i;
  lf->npre = 0;
  for (i = 0; i < 3; i++) {
    c = getc(lf->f);
    if (c == EOF || c != (unsigned char)bom[i]) return c;
    lf->pre[lf->npre++] = (char)c;
  }
  lf->npre = 0;  /* complete BOM: discard it */
  return getc(lf->f);
}


/* getF: characters read while looking for a BOM that were not one */
static const char *scfa_loadf_pre (LoadF *lf, size_t *size) {
  if (lf->npre > 0) {
    *size = lf->npre;
    lf->npre = 0;
    return lf->pre;
  }
  return NULL;
}

#endif
