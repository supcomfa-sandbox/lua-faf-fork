/*
** SCFA dialect port "keywords", adapter for the Lua 5.0 core (the changes of
** FAForever/lua-lang's lua-5.0 branch, moved behind the port).
** Implements the hooks of lua5_0-api/keywords/scfa_keywords.h. Included by llex.c,
** lparser.c and lauxlib.c, each with its SCFA_IN_<FILE> macro defined, and only
** compiles the section of the including file.
** Not hooked in this core (as in lua-lang): table constructor size hints, the repeat-until
** `continue' scoping check, BOM skipping in the lexer (luaL_loadfile skips it).
*/

#include "scfa_keywords.h"


/* ------------------------------------------------------------------ llex.c */
#if defined(SCFA_IN_LLEX)

/* `#' short comments, `!=' (synonym of `~='), and the `<<' `>>' tokens */
static int scfa_lex_hook (LexState *LS, SemInfo *seminfo, int *tok) {
  (void)seminfo;
  switch (LS->current) {
    case '#': {  /* short comment */
      while (LS->current != '\n' && LS->current != EOZ)
        next(LS);
      return SCFA_LEX_AGAIN;
    }
    case '<': {
      next(LS);
      if (LS->current == '=') { next(LS); *tok = TK_LE; }
      else if (LS->current == '<') { next(LS); *tok = TK_BSHL; }
      else *tok = '<';
      return SCFA_LEX_TOKEN;
    }
    case '>': {
      next(LS);
      if (LS->current == '=') { next(LS); *tok = TK_GE; }
      else if (LS->current == '>') { next(LS); *tok = TK_BSHR; }
      else *tok = '>';
      return SCFA_LEX_TOKEN;
    }
    case '!': {
      next(LS);
      *tok = (LS->current != '=') ? '!' : (next(LS), TK_NE);
      return SCFA_LEX_TOKEN;
    }
    default: return SCFA_LEX_PASS;
  }
}


/* hexadecimal literals: called by read_numeral when no comma started the numeral.
   Returns 1 if a hex numeral was read (seminfo set), else 0 with the buffer position
   advanced past a leading `0' that was saved. *lp is the current buffer length. */
static int scfa_read_hex (LexState *LS, SemInfo *seminfo, size_t *lp) {
  size_t l = *lp;
  if (LS->current == '0') {
    save_and_next(LS, l);
    if (LS->current == 'x' || LS->current == 'X') {
      save_and_next(LS, l);
      while (isxdigit(LS->current)) {
        checkbuffer(LS, l);
        save_and_next(LS, l);
      }
      if (LS->current == 'p' || LS->current == 'P') {
        save_and_next(LS, l);  /* read `P' */
        if (LS->current == '+' || LS->current == '-')
          save_and_next(LS, l);  /* optional exponent sign */
        while (isxdigit(LS->current)) {
          checkbuffer(LS, l);
          save_and_next(LS, l);
        }
      }
      save(LS, '\0', l);
      if (!luaO_str2d(luaZ_buffer(LS->buff), &seminfo->r))
        luaX_lexerror(LS, "malformed hex number", TK_NUMBER);
      *lp = l;
      return 1;
    }
  }
  *lp = l;
  return 0;
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
}


/* stat -> CONTINUE: a real jump to the loop's next-iteration point */
static void scfa_continuestat (LexState *ls) {
  FuncState *fs = ls->fs;
  BlockCnt *bl = fs->bl;
  int upval = 0;
  next(ls);  /* skip CONTINUE */
  while (bl && !bl->isbreakable) {
    upval |= bl->upval;
    bl = bl->previous;
  }
  if (!bl)
    luaX_syntaxerror(ls, "no loop to continue");
  if (upval)
    luaK_codeABC(fs, OP_CLOSE, bl->nactvar, 0, 0);
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


/* repeat-until: `continue' goes to the condition (no scoping check in this core) */
static void scfa_repeat_cond_begin (FuncState *fs, BlockCnt *bl) {
  luaK_patchtohere(fs, bl->contlist);
}

#endif


/* --------------------------------------------------------------- lauxlib.c */
#if defined(SCFA_IN_LAUXLIB)

/* luaL_loadfile that skips a UTF-8 BOM and a first line starting with `#'
   (copied from Lua 5.3 by lua-lang); replaces the stock loader in lauxlib.c */
#define SCFA_LOADFILE

typedef struct LoadF {
	int n;  /* number of pre-read characters */
  FILE *f;
  char buff[LUAL_BUFFERSIZE];
} LoadF;


static const char *getF (lua_State *L, void *ud, size_t *size) {
  LoadF *lf = (LoadF *)ud;
  (void)L;
  if (lf->n > 0) {  /* are there pre-read characters to be read? */
    *size = lf->n;  /* return them (chars already in buffer) */
    lf->n = 0;  /* no more pre-read characters */
  } else {
		if (feof(lf->f)) return NULL;
	  *size = fread(lf->buff, 1, LUAL_BUFFERSIZE, lf->f);
	}
  return (*size > 0) ? lf->buff : NULL;
}


static int errfile (lua_State *L, int fnameindex) {
  const char *filename = lua_tostring(L, fnameindex) + 1;
  lua_pushfstring(L, "cannot read %s: %s", filename, strerror(errno));
  lua_remove(L, fnameindex);
  return LUA_ERRFILE;
}


/* Copied from lua5.3 */
static int skipBOM (LoadF *lf) {
  const char *p = "\xEF\xBB\xBF";  /* UTF-8 BOM mark */
  int c;
  lf->n = 0;
  do {
    c = getc(lf->f);
    if (c == EOF || c != *(const unsigned char *)p++) return c;
    lf->buff[lf->n++] = c;  /* to be read by the parser */
  } while (*p != '\0');
  lf->n = 0;  /* prefix matched; discard it */
  return getc(lf->f);  /* return next character */
}


/*
** Copied from lua5.3
**
** reads the first character of file 'f' and skips an optional BOM mark
** in its beginning plus its first line if it starts with '#'. Returns
** true if it skipped the first line.  In any case, '*cp' has the
** first "valid" character of the file (after the optional BOM and
** a first-line comment).
*/
static int skipcomment (LoadF *lf, int *cp) {
  int c = *cp = skipBOM(lf);
  if (c == '#') {  /* first line is a comment (Unix exec. file)? */
    do {  /* skip first line */
      c = getc(lf->f);
    } while (c != EOF && c != '\n');
    *cp = getc(lf->f);  /* skip end-of-line, if present */
    return 1;  /* there was a comment */
  }
  else return 0;  /* no comment */
}


LUALIB_API int luaL_loadfile (lua_State *L, const char *filename) {
  LoadF lf;
  int status, readstatus;
  int c;
  int fnameindex = lua_gettop(L) + 1;  /* index of filename on the stack */
  if (filename == NULL) {
    lua_pushliteral(L, "=stdin");
    lf.f = stdin;
  }
  else {
    lua_pushfstring(L, "@%s", filename);
    lf.f = fopen(filename, "r");
		if (lf.f == NULL) return errfile(L, fnameindex);  /* unable to open file */
  }
	if (skipcomment(&lf, &c))  /* read initial portion */
    lf.buff[lf.n++] = '\n';  /* add line to correct line numbers */
	if (c == LUA_SIGNATURE[0] && filename) {  /* binary file? */
    lf.f = freopen(filename, "rb", lf.f);  /* reopen in binary mode */
    if (lf.f == NULL) return errfile(L, fnameindex);
    skipcomment(&lf, &c);  /* re-read initial portion */
  }
  if (c != EOF)
    lf.buff[lf.n++] = c;  /* 'c' is the first character of the stream */
  status = lua_load(L, getF, &lf, lua_tostring(L, -1));
  readstatus = ferror(lf.f);
  if (lf.f != stdin) fclose(lf.f);  /* close file (even in case of errors) */
  if (readstatus) {
    lua_settop(L, fnameindex);  /* ignore results from `lua_load' */
    return errfile(L, fnameindex);
  }
  lua_remove(L, fnameindex);
  return status;
}

#endif
