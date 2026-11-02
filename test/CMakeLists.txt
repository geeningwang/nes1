cmake_minimum_required(VERSION 3.16)
project(nesdump_contract_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(input_check input_check.cpp)
if(MSVC)
  target_compile_options(input_check PRIVATE /W4 /WX)
else()
  target_compile_options(input_check PRIVATE -Wall -Wextra -Werror)
endif()

enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME input_contract
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../tool/test_input.py"
          --checker "$<TARGET_FILE:input_check>")
add_test(NAME comparison_contract
  COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}" -p "test_*.py" -v)
