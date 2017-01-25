#if !defined(DYNOGRAPH_H_)
#define DYNOGRAPH_H_
#include <stdint.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <stdbool.h>

struct dynograph_args
{
    // Number of epochs in the benchmark
    int64_t num_epochs;
    // File path for edge list to load
    const char input_path[256];
    // Number of edges to insert in each batch of insertions
    int64_t batch_size;
    // Algorithms to run after each epoch
    const char alg_names[256];
    // Batch sort mode:
    enum SORT_MODE {
        // Do not pre-sort batches
        UNSORTED,
        // Sort and deduplicate each batch before returning it
        PRESORT,
        // Each batch is a cumulative snapshot of all edges in previous batches
        SNAPSHOT
    } sort_mode;
    // Percentage of the graph to hold in memory
    double window_size;
    // Number of times to repeat the benchmark
    int64_t num_trials;
};

struct dynograph_edge {
    int64_t src;
    int64_t dst;
    int64_t weight;
    int64_t timestamp;
};

struct dynograph_dataset {
    int64_t num_batches;
    int64_t num_edges;
    int64_t directed;
    int64_t max_vertex_id;
    const struct dynograph_args *args;
    struct dynograph_edge edges[0];
};

struct dynograph_edge_batch {
    const int64_t num_edges;
    const int64_t directed;
    const struct dynograph_edge* edges;
};

void dynograph_message(const char* fmt, ...);
void dynograph_error(const char* fmt, ...);
void dynograph_die();

void dynograph_args_parse(int argc, char *argv[], struct dynograph_args *args);


struct dynograph_dataset * dynograph_load_dataset(const char* path, int64_t num_batches);
struct dynograph_edge_batch dynograph_get_batch(const struct dynograph_dataset * dataset, int64_t batch_id);
void dynograph_free_dataset(struct dynograph_dataset * dataset);
int64_t dynograph_get_timestamp_for_window(const struct dynograph_dataset * dataset, int64_t batch_id);

#endif /* __DYNOGRAPH_H_ */
