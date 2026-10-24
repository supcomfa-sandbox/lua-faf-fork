-- More guard tests for the SCFA dialect (run: lua scfa-test/scfa_guards.lua; exit code 1 on any failure).
-- Complements scfa.lua: every operator precedence pair of the dialect operators (& | << >> and ^ as xor) against the other levels, associativity observed through
-- metamethods, `continue` in every loop form (nested, upvalue closing, closures, mixed with break), table size hints {&h &a}, BOM / hash-comment corners and, when lua_Number
-- is float32 (build with -DLUA_NUMBER_FLOAT32), float32 edge values. A check marked xfail documents a known bug: it must FAIL today and the run fails when it starts to pass.
--
-- Precedence of the dialect (src/lparser.c priority[]), loosest -> tightest:
--   or < and < comparisons < `..` (right) < `+ -` < `* / %` < `& | << >>` (ONE level, left assoc, unlike C) < unary < `^` (xor, right assoc, tighter than unary)
local fails, total, xfails = 0, 0, 0
local function say(ok, name, extra)
  total = total + 1
  if ok then print("PASS " .. name) else fails = fails + 1; print("FAIL " .. name .. (extra and (" : " .. tostring(extra)) or "")) end
end
local function run(src)
  local f, err = loadstring(src, "=t")
  if not f then return false, "compile: " .. tostring(err) end
  return pcall(f)
end
local function eq(name, src, want)
  local ok, got = run(src)
  say(ok and got == want, name, ok and ("got " .. tostring(got) .. " want " .. tostring(want)) or got)
end
local function bad(name, src, pat)
  local f, err = loadstring(src, "=t")
  say(f == nil and (not pat or string.find(err, pat, 1, true)), name, f and "compiled" or err)
end
local function xfail(name, cond, why)          -- strict expected failure
  total = total + 1
  if cond then fails = fails + 1; print("XPASS " .. name .. " (bug fixed? remove the xfail): " .. why)
  else xfails = xfails + 1; print("xfail " .. name .. ": " .. why) end
end

local float32 = (16777216 + 1 == 16777216)

-- ---------------------------------------------------------------- bit operators against every other level
eq("prec + looser than &", "return 2 + 3 & 4", 2)               -- 2 + (3&4)
eq("prec & tighter than + (rhs)", "return 6 & 3 + 1", 3)        -- (6&3) + 1
eq("prec - looser than |", "return 10 - 1 | 2", 7)              -- 10 - (1|2)
eq("prec * looser than &", "return 2 * 3 & 4", 0)               -- 2 * (3&4)
eq("prec & tighter than * (rhs)", "return 6 & 3 * 2", 4)        -- (6&3) * 2
eq("prec / looser than &", "return 8 / 2 & 3", 4)               -- 8 / (2&3)
eq("prec | tighter than * (rhs)", "return 6 | 1 * 2", 14)       -- (6|1) * 2
eq("prec << tighter than +", "return 1 << 2 + 1", 5)            -- (1<<2) + 1
eq("prec >> tighter than *", "return 16 >> 2 * 2", 8)           -- (16>>2) * 2
eq("<< is left assoc", "return 1 << 2 << 3", 32)                -- right assoc would give 65536
eq(">> is left assoc", "return 256 >> 2 >> 2", 16)
eq("& | are one level, left", "return 4 | 3 & 1", 1)            -- (4|3)&1; C precedence would give 5
eq("& << are one level, left", "return 5 & 3 << 1", 2)          -- (5&3)<<1; C precedence would give 4
eq("| >> are one level, left", "return 8 | 4 >> 1", 6)          -- (8|4)>>1; C precedence would give 10
eq("<< | one level, left", "return 1 << 1 | 1", 3)
eq("parens override", "return (2 + 3) & 4", 4)
eq("prec .. looser than & (lhs)", "return 1 .. 2 & 3", "12")
eq("prec .. looser than & (rhs)", "return 1 & 3 .. 2", "12")
eq("prec .. looser than <<", "return 1 .. 1 << 2", "14")
eq("prec == looser than & (lhs)", "return 3 & 1 == 1", true)
eq("prec == looser than & (rhs)", "return 1 == 3 & 1", true)
eq("prec < looser than &", "return 1 < 2 & 3", true)
eq("prec > looser than >>", "return 8 >> 1 > 3", true)
eq("prec ~= looser than |", "return 1 | 2 ~= 3", false)
eq("prec != looser than |", "return 1 | 2 != 4", true)
eq("prec and looser than &", "return 1 & 3 and 5", 5)
eq("prec or looser than &", "return nil or 6 & 3", 2)
eq("prec not binds tighter than ==", "return not nil == true", true)

-- ---------------------------------------------------------------- ^ is xor: tighter than everything, right associative
eq("xor value", "return 5 ^ 3", 6)
eq("xor tighter than *", "return 2 * 3 ^ 1", 4)                 -- 2 * (3^1)
eq("xor tighter than +", "return 1 + 3 ^ 1", 3)
eq("xor tighter than &", "return 1 ^ 3 & 6", 2)                 -- (1^3)&6
eq("xor tighter than & (rhs)", "return 6 & 3 ^ 1", 2)
eq("xor tighter than <<", "return 1 << 3 ^ 1", 4)               -- 1 << (3^1)
eq("xor tighter than unary minus", "return -3 ^ 1", -2)         -- -(3^1)
eq("xor tighter than ==", "return 3 ^ 1 == 2", true)
eq("xor tighter than ..", "return 1 .. 3 ^ 1", "12")
eq("xor is not pow", "return 2 ^ 3", 1)
eq("math.pow is pow", "return math.pow(2, 3)", 8)
eq("^ right assoc (via __pow)", [[
  local mt = {}
  mt.__pow = function(x, y) return setmetatable({s = '(' .. (x.s or tostring(x)) .. '^' .. (y.s or tostring(y)) .. ')'}, mt) end
  local a, b, c = setmetatable({s = 'a'}, mt), setmetatable({s = 'b'}, mt), setmetatable({s = 'c'}, mt)
  return (a ^ b ^ c).s]], "(a^(b^c))")
eq(".. right assoc (via __concat)", [[
  local mt = {}
  mt.__concat = function(x, y) return setmetatable({s = '(' .. (x.s or tostring(x)) .. '.' .. (y.s or tostring(y)) .. ')'}, mt) end
  local a, b, c = setmetatable({s = 'a'}, mt), setmetatable({s = 'b'}, mt), setmetatable({s = 'c'}, mt)
  return (a .. b .. c).s]], "(a.(b.c))")
eq("- left assoc", "return 10 - 3 - 2", 5)
eq("/ left assoc", "return 8 / 4 / 2", 1)
eq("% same level as * (5.1 modulo)", "return 10 % 4 * 2", 4)
eq("& tighter than %", "return 7 % 6 & 3", 1)                  -- 7 % (6&3)
eq("^ tighter than %", "return 9 % 3 ^ 1", 1)                  -- 9 % 2
eq("shift of a non-number errors", "return pcall(function() return 'x' << 1 end)", false)
eq("band error text", "local ok, e = pcall(function() return {} & 1 end) return string.find(e, 'operands must be numbers', 1, true) ~= nil", true)

-- ---------------------------------------------------------------- continue in every loop form
eq("continue while", "local i, s = 0, 0 while i < 6 do i = i + 1 if i == 2 or i == 4 then continue end s = s + i end return s", 15)
eq("continue numeric for", "local s = 0 for i = 1, 6 do if i == 2 or i == 4 then continue end s = s + i end return s", 15)
eq("continue numeric for, negative step", "local s = 0 for i = 6, 1, -1 do if i == 5 then continue end s = s * 10 + i end return s", 64321)
eq("continue generic for ipairs", "local s = 0 for _, v in ipairs({1, 2, 3, 4}) do if v == 3 then continue end s = s + v end return s", 7)
eq("continue generic for next", "local s = 0 for _, v in next, {5, 6, 7} do if v == 6 then continue end s = s + v end return s", 12)
eq("continue for-in-table (5.0 style)", "local s = 0 for k, v in {a = 1, b = 2, c = 3} do if v == 2 then continue end s = s + v end return s", 4)
eq("continue repeat-until", "local i, s = 0, 0 repeat i = i + 1 if i == 2 then continue end s = s + i until i >= 4 return s", 8)
eq("continue in repeat still evaluates the condition", "local n, hits = 0, 0 repeat n = n + 1 if n < 3 then continue end hits = hits + 1 until n >= 5 return hits", 3)
eq("continue nested inner", "local c = 0 for i = 1, 3 do for j = 1, 3 do if j == 2 then continue end c = c + 1 end end return c", 6)
eq("continue nested outer", "local c = 0 for i = 1, 3 do if i == 2 then continue end for j = 1, 3 do c = c + 1 end end return c", 6)
eq("continue while in for in repeat",
  "local c, r = 0, 0 repeat r = r + 1 for i = 1, 3 do local k = 0 while k < 3 do k = k + 1 if k == 2 then continue end c = c + 1 end if i == 2 then continue end c = c + 100 end until r >= 2 return c", 412)
eq("continue mixed with break", "local s = 0 for i = 1, 10 do if i % 2 == 0 then continue end if i > 7 then break end s = s + i end return s", 16)
eq("continue closes upvalues (numeric for)", "local fs = {} for i = 1, 3 do local x = i * 10 fs[i] = function() return x end if i == 2 then continue end end return fs[1]() + fs[2]() + fs[3]()", 60)
eq("continue closes upvalues (generic for, shared state)",
  "local fs, n = {}, 0 for _, v in ipairs({1, 2, 3}) do local x = v fs[v] = function() x = x + 1 return x end if v == 1 then continue end n = n + 1 end return fs[1]() + fs[1]() * 10 + fs[2]() * 100 + fs[3]() * 1000 + n * 10000", 24332)
eq("continue closes upvalues (while)", "local fs, i = {}, 0 while i < 3 do i = i + 1 local x = i fs[i] = function() return x end if i == 1 then continue end end return fs[1]() * 100 + fs[2]() * 10 + fs[3]()", 123)
eq("continue closes upvalues (repeat)", "local fs, n = {}, 0 repeat n = n + 1 local x = n fs[n] = function() return x end if n == 2 then continue end until n >= 3 return fs[1]() + fs[2]() * 10 + fs[3]() * 100", 321)
eq("upvalue shared by two closures survives continue", "local get, set for i = 1, 2 do local v = i if i == 1 then get = function() return v end set = function(x) v = x end continue end end set(42) return get()", 42)
eq("continue inside nested ifs", "local s = 0 for i = 1, 4 do if i > 1 then if i < 4 then do continue end end end s = s + i end return s", 5)
eq("continue as the only statement", "local n = 0 for i = 1, 3 do n = n + 1 continue end return n", 3)
eq("loop in a function in a loop has its own continue", "local s = 0 for i = 1, 2 do local f = function() local c = 0 for j = 1, 3 do if j == 2 then continue end c = c + j end return c end s = s + f() end return s", 8)
bad("continue in a function inside a loop", "for i = 1, 2 do local f = function() continue end end", "no loop to continue")
bad("continue outside any loop", "continue", "no loop to continue")
bad("continue is reserved", "local continue = 1")
bad("repeat: local declared after continue used by the condition", "repeat if x then continue end local done = true until done", "continue")

-- ---------------------------------------------------------------- table size hints {&hash &array}
eq("hints both", "local t = {&1 &8} return type(t)", "table")
eq("hints content", "local t = {&2 &4 a = 1, b = 2, 10, 20} return t.a + t.b + t[1] + t[2]", 33)
eq("hints then semicolon", "local t = {&0 &2; 5, 6} return t[1] * t[2]", 30)
eq("hints then comma", "local t = {&0 &2, 5, 6} return t[1] * t[2]", 30)
eq("third hint is ignored", "local t = {&1 &2 &3 7} return t[1]", 7)
eq("hex hint", "local t = {&0x10 &0x10 4} return t[1]", 4)
eq("huge hint is clamped, not allocated", "local t = {&1e9 &1e9 3} return t[1]", 3)
eq("fractional hint", "local t = {&1.5 &2.5 9} return t[1]", 9)
eq("hint on a nested constructor", "local t = {&1 &1 {&0 &2 5, 6}} return t[1][2]", 6)
eq("hints in a call argument", "local function f(t) return t[1] end return f{&0 &1 8}", 8)
bad("negative hint is not a number token", "local t = {&-1}")
bad("hint after content", "local t = {1, &2}")
bad("non-number hint", "local t = {&x}")
eq("band of a constructor value", "local a = 6 local t = {a & 3} return t[1]", 2)

-- ---------------------------------------------------------------- BOM, hash comments
eq("BOM before code", "return loadstring('\\239\\187\\191return 6')()", 6)
eq("BOM before a # line", "return loadstring('\\239\\187\\191# c\\nreturn 7')()", 7)
eq("BOM only", "return loadstring('\\239\\187\\191') ~= nil", true)
eq("BOM in the middle is not skipped", "return loadstring('return 1 \\239\\187\\191') == nil", true)
eq("second BOM is not skipped", "return loadstring('\\239\\187\\191\\239\\187\\191return 1') == nil", true)
bad("BOM: a partial prefix EF BB is a syntax error, not skipped", "\239\187return 1", "byte order mark")
bad("BOM: a lone EF is a syntax error, not skipped", "\239return 1", "byte order mark")
bad("BOM: EF at the very end of the chunk", "\239", "byte order mark")
eq("hash comment trailing", "local a = 1 # note\nreturn a", 1)
eq("hash comment at eof", "return 7 # no newline", 7)
eq("hash after an operator ends the line", "return 1 + 2 # + 3", 3)
eq("hash inside a string is text", "return '#x'", "#x")
bad("lone bang", "return 1 ! 2")

-- ---------------------------------------------------------------- float32 edge values (only when lua_Number is float32)
if float32 then
  eq("2^24+1 literal rounds", "return 16777217 == 16777216", true)
  eq("2^24+2 is exact", "return 16777216 + 2 == 16777218", true)
  eq("0.1+0.2 == 0.3", "return 0.1 + 0.2 == 0.3", true)
  eq("denormal survives", "return 1e-40 > 0", true)
  eq("below the smallest denormal underflows", "return 1e-46 == 0", true)
  eq("overflow to inf", "return 1e39 == 1 / 0", true)
  eq("max float is finite", "return 3.4028234e38 < 1 / 0", true)
  eq("1 + 1e-8 == 1", "return 1 + 1e-8 == 1", true)
  eq("tostring uses 7 digits", "return tostring(0.1)", "0.1")
  eq("tostring of 2^24 is exponent form", "return tostring(16777216):find('e', 1, true) ~= nil", true)
else
  print("skip float32 edge values: lua_Number is double in this build (build with -DLUA_NUMBER_FLOAT32)")
end
eq("negative zero by division", "local z = 0 return 1 / -z < 0", true)
eq("negative zero equals zero", "local z = 0 return -z == 0", true)
eq("nan is not equal to itself", "local n = 0 / 0 return n == n", false)
eq("nan comparisons are false", "local n = 0 / 0 return (n < 1) == false and (n > 1) == false and (n <= n) == false and (n >= n) == false", true)
eq("inf compares", "local i = 1 / 0 return i > 1e30 and -i < -1e30 and i == i", true)
eq("inf - inf is nan", "local i = 1 / 0 return i - i ~= i - i", true)
eq("0 * inf is nan", "local i = 1 / 0 return 0 * i ~= 0 * i", true)
eq("tostring(inf) names infinity", "local s = tostring(1 / 0) return s:find('inf', 1, true) ~= nil or s:find('INF', 1, true) ~= nil", true)
eq("tostring(nan) names nan", "local s = tostring(0 / 0) return s:find('nan', 1, true) ~= nil or s:find('IND', 1, true) ~= nil or s:find('NaN', 1, true) ~= nil", true)
eq("nan as a table key errors", "return pcall(function() local t = {} t[0 / 0] = 1 end)", false)
eq("nan index read is nil", "local t = {} return t[0 / 0]", nil)

print(string.format("%d checks, %d failed, %d expected failures", total, fails, xfails))
os.exit(fails == 0 and 0 or 1)
