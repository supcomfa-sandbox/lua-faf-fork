-- Guard tests for the SCFA dialect (run: lua scfa-test/scfa.lua). Exit code 1 on any failure.
-- Each check compiles a snippet with loadstring so a syntax error is a failed check, not a crash.
local fails, total = 0, 0
local function check(name, cond, extra)
  total = total + 1
  if cond then print("PASS " .. name)
  else fails = fails + 1; print("FAIL " .. name .. (extra and (" : " .. tostring(extra)) or "")) end
end
local function run(src)
  local f, err = loadstring(src, "=t")
  if not f then return false, "compile: " .. tostring(err) end
  return pcall(f)
end
local function eq(name, src, want)
  local ok, got = run(src)
  check(name, ok and got == want, ok and ("got " .. tostring(got) .. " want " .. tostring(want)) or got)
end
local function bad(name, src, pat)
  local f, err = loadstring(src, "=t")
  check(name, f == nil and (not pat or string.find(err, pat, 1, true)), f and "compiled" or err)
end

-- lexer
eq("hash comment", "local a = 1 # trailing\n# whole line\nreturn a", 1)
eq("hash comment at eof", "return 7 # no newline", 7)
eq("bang-equals true", "return (1 != 2)", true)
eq("bang-equals false", "return (1 != 1)", false)
eq("tilde-equals still works", "return (1 ~= 2)", true)
eq("hex literal", "return 0x10", 16)
eq("hex literal upper", "return 0XFF", 255)

-- bitwise operators
eq("and", "return 12 & 10", 8)
eq("or", "return 12 | 3", 15)
eq("shl", "return 1 << 4", 16)
eq("shr", "return 256 >> 4", 16)
eq("xor via caret", "return 5 ^ 3", 6)
eq("caret is not pow", "return 2 ^ 3", 1)
eq("pow via math.pow", "return math.pow(2, 3)", 8)
eq("prec: bitwise binds tighter than +", "return 1 + 2 & 3", 3)
eq("prec: bitwise binds tighter than *", "return 2 * 3 & 4", 0)
eq("prec: shl binds tighter than +", "return 1 << 1 + 1", 3)
eq("xor chain", "return 7 ^ 6 ^ 5", 4)
eq("prec: xor tighter than &", "return 6 & 3 ^ 1", 2)
eq("prec: bitwise tighter than ..", "return 1 .. 2 & 3", "12")
eq("prec: bitwise tighter than ==", "return 3 & 1 == 1", true)
do
  local ok, err = run("return {} & 1")
  check("band error text", (not ok) and string.find(err, "operands must be numbers", 1, true), err)
  ok, err = run("return {} ^ 1")
  check("xor on table without __pow errors", not ok, err)
  ok, err = run("local mt = {__pow = function(a, b) return 99 end} return setmetatable({}, mt) ^ 1")
  check("xor on table uses __pow", ok and err == 99, err)
end

-- continue
eq("continue while", "local i, s = 0, 0 while i < 5 do i = i + 1 if i == 3 then continue end s = s + i end return s", 12)
eq("continue numeric for", "local s = 0 for i = 1, 5 do if i == 2 then continue end s = s + i end return s", 13)
eq("continue generic for", "local s = 0 for _, v in ipairs({1,2,3,4}) do if v % 2 == 0 then continue end s = s + v end return s", 4)
eq("continue repeat-until", "local i, s = 0, 0 repeat i = i + 1 if i == 2 then continue end s = s + i until i >= 4 return s", 8)
eq("continue repeat, local declared before continue used in cond", "local n, s = 0, 0 repeat n = n + 1 local done = n >= 3 if n == 1 then continue end s = s + 1 until done return s", 2)
bad("continue repeat skipping a local used by cond", "repeat if x then continue end local done = true until done", "continue")
eq("continue nested inner only",
  "local c = 0 for i = 1, 3 do for j = 1, 3 do if j == 2 then continue end c = c + 1 end end return c", 6)
eq("continue nested outer",
  "local c = 0 for i = 1, 3 do if i == 2 then continue end for j = 1, 3 do c = c + 1 end end return c", 6)
eq("continue closes upvalues",
  "local fs = {} for i = 1, 3 do local x = i * 10 fs[i] = function() return x end if i == 2 then continue end end return fs[1]() + fs[2]() + fs[3]()", 60)
eq("continue closes upvalues while",
  "local fs, i = {}, 0 while i < 3 do i = i + 1 local x = i fs[i] = function() return x end if i == 1 then continue end end return fs[1]() * 100 + fs[2]() * 10 + fs[3]()", 123)
eq("continue in if inside while keeps going", "local i, hits = 0, 0 while i < 4 do i = i + 1 if i < 3 then continue end hits = hits + 1 end return hits", 2)
eq("continue repeat, later local not used by cond is fine", "local n, s = 0, 0 repeat n = n + 1 if n == 1 then continue end local y = n s = s + y until n >= 3 return s", 5)
eq("continue repeat with upvalue in body", "local fs, n = {}, 0 repeat n = n + 1 local x = n fs[n] = function() return x end if n == 2 then continue end until n >= 3 return fs[1]() + fs[2]() * 10 + fs[3]() * 100", 321)
bad("continue outside loop", "continue", "no loop to continue")
eq("continue is reserved", "return pcall(loadstring, 'local continue = 1') and loadstring('local continue = 1') == nil", true)

-- strings
eq("string methods on strings", "return ('hello'):sub(2, 3)", "el")
eq("lualex exists", "return type(string.lualex)", "function")

-- 5.1 behaviour kept
eq("modulo kept", "return 7 % 4", 3)
eq("vararg select kept", "return select('#', 1, 2, 3)", 3)
eq("string.format %d", "return string.format('%d', 5)", "5")
eq("table.getn kept", "return table.getn({1,2,3})", 3)

-- length operator is gone: # is a comment, table.getn is the way
eq("length via table.getn", "return table.getn({1, 2, 3})", 3)

-- extra table functions (FAF exe: getsize, empty, unpack(t,i,j), clone)
eq("table.getsize counts array+hash", "return table.getsize({1, 2, a = 1, b = 2, c = 3})", 5)
eq("table.getsize non-table", "return table.getsize(5)", 0)
eq("table.empty true", "return table.empty({})", true)
eq("table.empty false", "return table.empty({a = 1})", false)
eq("table.empty non-table", "return table.empty(nil)", true)
eq("table.unpack range", "local a, b = table.unpack({1, 2, 3, 4}, 2, 3) return a * 10 + b", 23)
eq("table.unpack default", "local a, b, c = table.unpack({7, 8, 9}) return a + b + c", 24)
eq("table.clone deep", "local t = {a = {b = 1}} local c = table.clone(t) c.a.b = 2 return t.a.b * 10 + c.a.b", 12)
eq("table.clone cycle-safe", "local t = {} t.self = t local c = table.clone(t) return c.self == c and c ~= t", true)

-- Lua 5.0 behaviours the game relies on, kept on the 5.1 VM
eq("for k,v in table do (5.0 style, no pairs)", "local t = {a = 1, b = 2} local n = 0 for k, v in t do n = n + v end return n", 3)
eq("for in table: array part", "local s = 0 for i, v in {5, 6, 7} do s = s + i * v end return s", 5 + 12 + 21)
eq("for in function iterator still works", "local s = 0 for i, v in ipairs({5, 6}) do s = s + v end return s", 11)
eq("table.setn sets the count seen by getn", "local t = {1, 2, 3} table.setn(t, 10) return table.getn(t)", 10)
eq("table.getn without n is the border", "return table.getn({1, 2, 3})", 3)
eq("math.mod is fmod", "return math.mod(-7, 3)", -1)
eq("string.gfind exists", "local n = 0 for w in string.gfind('a b c', '%a') do n = n + 1 end return n", 3)

-- table preallocation hints {&hash &array} (LuaPlus): parsed, no visible effect
eq("prealloc hints both", "local t = {&1 &8} return type(t)", "table")
eq("prealloc hints empty", "local t = {&1 &8} return next(t) == nil", true)
eq("prealloc hints with content", "local t = {&2 &4 a = 1, b = 2, 10, 20} return t.a + t.b + t[1] + t[2]", 33)
eq("prealloc hints no space", "local t = {&1&8} return type(t)", "table")
eq("prealloc hints then separator", "local t = {&0 &2; 5, 6} return t[1] * t[2]", 30)
eq("prealloc single hint", "local t = {&3 x = 1} return t.x", 1)
eq("prealloc content larger than hint", "local t = {&1 &1 1, 2, 3, 4} return t[4]", 4)
bad("prealloc needs a number", "local t = {&x}")
eq("bitand still works near constructor", "local t = {1 & 3, 2} return t[1] + t[2]", 3)

-- dialect details kept from the fork
do
  local f = loadstring("return arg")
  setfenv(f, {})  -- the standalone interpreter defines a global `arg`; look at the chunk itself
  check("no implicit arg in main chunk", f() == nil)
end
eq("implicit arg in vararg function (5.0 compat)", "local function f(...) return arg.n end return f(1, 2, 3)", 3)
bad("ambiguous call across newline", "local f = print\nf\n('x')", "ambiguous")

-- BOM / strings via loadstring
eq("loadstring skips UTF-8 BOM", "return loadstring('\\239\\187\\191return 6')()", 6)

-- file loading: BOM and shebang
do
  local name = (os.getenv("TEMP") or ".") .. os.tmpname()
  local f = io.open(name, "wb"); f:write("\239\187\191return 41 + 1"); f:close()
  local chunk, err = loadfile(name)
  check("loadfile skips UTF-8 BOM", chunk and chunk() == 42, err)
  f = io.open(name, "wb"); f:write("#!/usr/bin/lua\nreturn 5"); f:close()
  chunk, err = loadfile(name)
  check("loadfile skips shebang line", chunk and chunk() == 5, err)
  f = io.open(name, "wb"); f:write("\239\187\191# comment\nreturn __x"); f:close()
  chunk, err = loadfile(name)
  check("loadfile BOM then #", chunk ~= nil, err)
  os.remove(name)
end

print(string.format("%d checks, %d failed", total, fails))
os.exit(fails == 0 and 0 or 1)
