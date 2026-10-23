# 32-bit mingw (msys2): put C:\msys64\mingw32\bin first on PATH, then `make test`.
CC ?= gcc
CFLAGS = -std=gnu99 -Wall -Wextra -O1 -m32 -I../include -I../third_party/tomlc99 -I.
SRC = ../src/lua_adapter.c ../providers/lua50_passthrough.c ../third_party/tomlc99/toml.c
.PHONY: test clean
test: test_adapter_map.exe test_adapter_steel.exe
	./test_adapter_map.exe
	./test_adapter_steel.exe lua50.toml 50
	./test_adapter_steel.exe lua51.toml 51float
	./test_adapter_steel.exe mixed.toml 51float
clean:
	rm -f *.exe
test_adapter_map.exe: test_adapter_map.c $(SRC)
	$(CC) $(CFLAGS) -o $@ $^
test_adapter_steel.exe: test_adapter_steel.c rt_shim.c $(SRC)
	$(CC) $(CFLAGS) -o $@ $^
