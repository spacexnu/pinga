.PHONY: build run test test-mock clean

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

clean:
	rm -rf build
