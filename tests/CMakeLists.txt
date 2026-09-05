# 测试暂以后续 QTest 用例扩充；当前空实现保证构建通过
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/unit)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/unit)
endif()
