add_executable (zookeeper-remove-by-list main.cpp ${SRCS})
target_link_libraries (zookeeper-remove-by-list clickhouse_common_io ${Boost_PROGRAM_OPTIONS_LIBRARY})
