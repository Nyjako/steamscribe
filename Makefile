BUILD_DIR := build

.PHONY: build clean

build:
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)
