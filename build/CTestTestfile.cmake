# CMake generated Testfile for 
# Source directory: C:/Users/ajay yadav/OneDrive/Desktop/low-latency-trading-engine
# Build directory: C:/Users/ajay yadav/OneDrive/Desktop/low-latency-trading-engine/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(matching_tests "C:/Users/ajay yadav/OneDrive/Desktop/low-latency-trading-engine/build/run_tests.exe")
set_tests_properties(matching_tests PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/ajay yadav/OneDrive/Desktop/low-latency-trading-engine/CMakeLists.txt;35;add_test;C:/Users/ajay yadav/OneDrive/Desktop/low-latency-trading-engine/CMakeLists.txt;0;")
add_test(ringbuffer_tests "C:/Users/ajay yadav/OneDrive/Desktop/low-latency-trading-engine/build/run_ringbuffer_tests.exe")
set_tests_properties(ringbuffer_tests PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/ajay yadav/OneDrive/Desktop/low-latency-trading-engine/CMakeLists.txt;51;add_test;C:/Users/ajay yadav/OneDrive/Desktop/low-latency-trading-engine/CMakeLists.txt;0;")
add_test(concurrent_tests "C:/Users/ajay yadav/OneDrive/Desktop/low-latency-trading-engine/build/run_concurrent_tests.exe")
set_tests_properties(concurrent_tests PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/ajay yadav/OneDrive/Desktop/low-latency-trading-engine/CMakeLists.txt;63;add_test;C:/Users/ajay yadav/OneDrive/Desktop/low-latency-trading-engine/CMakeLists.txt;0;")
subdirs("_deps/googletest-build")
