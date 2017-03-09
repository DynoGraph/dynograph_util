set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99")
add_library(dynograph_util dynograph_util.c hooks.c)

# Copy test data to the build directory
file(
    COPY
    data/ring-of-cliques.graph.bin
    data/worldcup-10K.graph.bin
    DESTINATION
    data/
)
