add_executable(pubsub pubsub.c)
target_compile_definitions(pubsub PRIVATE _POSIX_C_SOURCE=200809L)
target_link_libraries(pubsub PRIVATE b64 sha1 http_parser)
target_link_libraries(pubsub PRIVATE libevent::libevent Threads::Threads)

# search for Python_EXECUTABLE
find_package(Python COMPONENTS Interpreter REQUIRED)
set(TEST_PYTHON_SRC
  basic.py
  limits.py
)
find_program(PKILL_EXECUTABLE pkill REQUIRED)
get_filename_component(WEBDIS_LAUNCHER bglaunch.sh ABSOLUTE)

add_test(NAME test_run_webdis_start
         COMMAND "${WEBDIS_LAUNCHER}" "$<TARGET_FILE:webdis>"
         WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
set_tests_properties(test_run_webdis_start PROPERTIES FIXTURES_SETUP test_run_fixture)
add_test(NAME test_run_webdis_stop
         COMMAND "${PKILL_EXECUTABLE}" "webdis")
set_tests_properties(test_run_webdis_stop PROPERTIES FIXTURES_CLEANUP test_run_fixture)

foreach(_test_file ${TEST_PYTHON_SRC})
  get_filename_component(_test_name ${_test_file} NAME_WE)
  get_filename_component(_test_abs_path ${_test_file} ABSOLUTE)
  add_test(NAME test_${_test_name} COMMAND ${Python_EXECUTABLE} ${_test_abs_path})
  set_tests_properties(test_${_test_name} PROPERTIES RUN_SERIAL ON)
  set_tests_properties(test_${_test_name} PROPERTIES FIXTURES_REQUIRED test_run_fixture)
endforeach()

#add_test(NAME test_pubsub COMMAND "$<TARGET_FILE:pubsub>" "-p" "7379")
#set_tests_properties(test_pubsub PROPERTIES RUN_SERIAL ON)
#set_tests_properties(test_pubsub PROPERTIES FIXTURES_REQUIRED test_run_fixture)

add_test(NAME test_bench COMMAND "./tests/bench.sh" WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
set_tests_properties(test_bench PROPERTIES RUN_SERIAL ON)
set_tests_properties(test_bench PROPERTIES FIXTURES_REQUIRED test_run_fixture)
