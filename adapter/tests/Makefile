# 32-bit mingw (msys2): put C:\msys64\mingw32\bin first on PATH, then `make test`.
CC ?= gcc
CFLAGS = -std=gnu99 -Wall -Wextra -O1 -m32 -I../include -I../third_party/tomlc99 -I. -Wno-cast-function-type
SRC = ../src/lua_adapter.c ../providers/lua50_passthrough.c ../third_party/tomlc99/toml.c
.PHONY: test clean
test: test_adapter_map.exe test_adapter_steel.exe test_number_default.exe test_number_exe.exe
	./test_number_default.exe
	./test_number_exe.exe
	./test_adapter_map.exe
	./test_adapter_steel.exe lua50.toml 50 lua50
	./test_adapter_steel.exe lua51.toml 51float lua51
	./test_adapter_steel.exe mixed.toml 51float lua51
clean:
	rm -f *.exe
# lua_Number: double by default, float32 in the in-game (exe) build -DILUA50_EXE_BUILD
test_number_default.exe: test_number.c
	$(CC) $(CFLAGS) -DEXPECT_NUMBER_SIZE=8 -o $@ $^
test_number_exe.exe: test_number.c
	$(CC) $(CFLAGS) -DILUA50_EXE_BUILD -DEXPECT_NUMBER_SIZE=4 -o $@ $^
test_adapter_map.exe: test_adapter_map.c $(SRC)
	$(CC) $(CFLAGS) -o $@ $^
test_adapter_steel.exe: test_adapter_steel.c rt_shim.c $(SRC)
	$(CC) $(CFLAGS) -o $@ $^
