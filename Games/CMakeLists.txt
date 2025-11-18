add_library(Games INTERFACE
    include/Games/RE7.h
    include/Games/RE8.h
)

target_include_directories(Games INTERFACE include)
target_link_libraries(Games INTERFACE Core)
