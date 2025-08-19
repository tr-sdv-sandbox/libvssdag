# can_transformer executable CMakeLists.txt

# Create can_transformer executable
add_executable(can_transformer
    can_transformer.cpp
)

# Link the executable against the VSSDAG library
target_link_libraries(can_transformer
    PRIVATE
        VSSDAG
)

# Set the runtime output directory to maintain build structure
set_target_properties(can_transformer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)