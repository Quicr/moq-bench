# SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

BUILD_DIR=build
CLANG_FORMAT=clang-format -i

.PHONY: all clean cclean format

all: ${BUILD_DIR}
	cmake --build ${BUILD_DIR} --parallel 8

${BUILD_DIR}: CMakeLists.txt
	cmake -B ${BUILD_DIR} -DCMAKE_POLICY_VERSION_MINIMUM=3.5 .

clean:
	cmake --build ${BUILD_DIR} --target clean

cclean:
	rm -rf ${BUILD_DIR}

format:
	find include -iname "*.h" -or -iname "*.cpp" | xargs ${CLANG_FORMAT}
	find src -iname "*.h" -or -iname "*.cpp" | xargs ${CLANG_FORMAT}
