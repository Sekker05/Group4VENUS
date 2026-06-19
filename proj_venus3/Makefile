# proj_venus3/Makefile
# Builds all C subsystems.
#
# Targets:
#   make           → build all production binaries
#   make test      → build all test binaries (no ZMQ, standalone)
#   make clean     → clean all compiled files
#   make run       → build then launch everything via main_launcher.py
#   make test-ipc  → run the Python IPC connection test (no hardware needed)

.PHONY: all test clean run test-ipc

MODULES := sensors motors communication

all:
	@echo "=== Building all production binaries ==="
	@for mod in $(MODULES); do \
	    echo "[BUILD] $$mod"; \
	    $(MAKE) -C $$mod main || exit 1; \
	done
	@echo "=== Build complete ==="

test:
	@echo "=== Building all test binaries ==="
	$(MAKE) -C sensors  test
	$(MAKE) -C motors   test
	$(MAKE) -C communication test
	@echo "=== Test binaries ready ==="

clean:
	@for mod in $(MODULES); do \
	    $(MAKE) -C $$mod clean; \
	done
	@echo "=== All cleaned ==="

run: all
	@echo "=== Launching all processes ==="
	python3 main_launcher.py

test-ipc:
	@echo "=== Running IPC connection tests (no hardware needed) ==="
	python3 test/test_connection.py
