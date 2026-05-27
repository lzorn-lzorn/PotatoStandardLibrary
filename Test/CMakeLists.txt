file(
    GLOB_RECURSE POTATO_STANDARD_TEST_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cxx"
)

if(NOT POTATO_STANDARD_TEST_SOURCES)
    message(STATUS "No test sources found under ${CMAKE_CURRENT_SOURCE_DIR}.")
    return()
endif()

foreach(test_source IN LISTS POTATO_STANDARD_TEST_SOURCES)
    file(RELATIVE_PATH test_relative_path "${CMAKE_CURRENT_SOURCE_DIR}" "${test_source}")
    string(REGEX REPLACE "[/\\\\.]" "_" test_target "${test_relative_path}")

    add_executable(${test_target} "${test_source}")
    target_link_libraries(${test_target} PRIVATE PotatoStandard::PotatoStandard)
    target_compile_features(${test_target} PRIVATE cxx_std_20)

    add_test(NAME ${test_target} COMMAND ${test_target})

    set_target_properties(
        ${test_target}
        PROPERTIES
            FOLDER "Tests"
    )
endforeach()