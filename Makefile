.PHONY: build run test test-mock install uninstall clean

PREFIX ?=
USER_PREFIX := $(HOME)/.local
SYSTEM_PREFIX := /usr/local

build:
	cmake -S . -B build
	cmake --build build

run: build
	./build/pinga config.httpbin.json

test: build
ifneq ($(ENABLE_NETWORK_TESTS),)
	cmake -S . -B build -DENABLE_NETWORK_TESTS=ON
endif
	ctest --test-dir build --output-on-failure

test-mock: build
	python3 scripts/mock_test.py

install: build
	@set -e; \
	install_build_dir=build; \
	if [ -f build/install_manifest.txt ] && [ ! -w build/install_manifest.txt ]; then \
		echo "build/install_manifest.txt is not writable; using build-user for install."; \
		install_build_dir=build-user; \
		cmake -S . -B "$$install_build_dir"; \
		cmake --build "$$install_build_dir"; \
	fi; \
	if [ -n "$(PREFIX)" ]; then \
		prefix="$(PREFIX)"; \
	elif [ -d "$(USER_PREFIX)/bin" ]; then \
		prefix="$(USER_PREFIX)"; \
	else \
		echo "User prefix $(USER_PREFIX)/bin not found; installing to $(SYSTEM_PREFIX)."; \
		echo "You may need to run: sudo make install"; \
		prefix="$(SYSTEM_PREFIX)"; \
	fi; \
	cmake --install "$$install_build_dir" --prefix "$$prefix"

uninstall:
	@set -e; \
	manifest=""; \
	for dir in build build-user; do \
		if [ -f "$$dir/install_manifest.txt" ]; then \
			manifest="$$dir/install_manifest.txt"; \
			break; \
		fi; \
	done; \
	if [ -z "$$manifest" ]; then \
		echo "No install_manifest.txt found in build/ or build-user/. Run 'make install' first."; \
		exit 1; \
	fi; \
	while IFS= read -r file; do \
		rm -f "$$file"; \
	done < "$$manifest"

clean:
	rm -rf build build-user
