# CAN Transformer Example

add_executable(vssdag-example-can-transformer
    main.cpp
)

target_link_libraries(vssdag-example-can-transformer
    PRIVATE
        vssdag
)

# Add include directories for dependencies
target_include_directories(vssdag-example-can-transformer
    PRIVATE
        ${moodycamel_SOURCE_DIR}
)

# Set the runtime output directory
set_target_properties(vssdag-example-can-transformer PROPERTIES
    RUNTIME_OUTPUT_NAME "can-transformer"
)