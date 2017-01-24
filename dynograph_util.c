#include "dynograph_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <getopt.h>

void
dynograph_message(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[DynoGraph] ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

void
dynograph_error(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[DynoGraph] ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    exit(1);
}

static const struct option long_options[] = {
    {"num-epochs" , required_argument, 0, 0},
    {"input-path" , required_argument, 0, 0},
    {"batch-size" , required_argument, 0, 0},
    {"alg-names"  , required_argument, 0, 0},
    {"sort-mode"  , required_argument, 0, 0},
    {"window-size", required_argument, 0, 0},
    {"num-trials" , required_argument, 0, 0},
    {"help"       , no_argument, 0, 0},
    {NULL         , 0, 0, 0}
};

static const struct option_desc
{
    const char* name;
    const char* desc;
} option_descriptions[] = {
    {"num-epochs" , "Number of epochs (algorithm updates) in the benchmark"},
    {"input-path" , "File path to the graph edge list to load (.graph.el or .graph.bin)"},
    {"batch-size" , "Number of edges in each batch of insertions"},
    {"alg-names"  , "Algorithms to run in each epoch"},
    {"sort-mode"  , "Controls batch pre-processing: \n"
                    "\t\tunsorted (no preprocessing, default),\n"
                    "\t\tpresort (sort and deduplicate before insert), or\n "
                    "\t\tsnapshot (clear out graph and reconstruct for each batch)"},
    {"window-size", "Percentage of the graph to hold in memory (computed using timestamps) "},
    {"num-trials" , "Number of times to repeat the benchmark"},
    {"help"       , "Print help"},
    {NULL         , NULL}
};

static void
print_help(const char* argv0)
{
    fprintf(stderr, "Usage: %s [OPTIONS]\n", argv0);
    for (const struct option_desc *o = option_descriptions; o->name != NULL; ++o)
    {
        fprintf(stderr, "\t--%s\t%s\n", o->name, o->desc);
    }
}

bool
validate(const struct dynograph_args *args)
{
    if (args->num_epochs < 1) {
        fprintf(stderr, "\t--num-epochs must be positive\n");
        return false;
    }
    if (strlen(args->input_path) == 0) {
        fprintf(stderr, "\t--input-path cannot be empty\n");
        return false;
    }
    if (args->batch_size < 1) {
        fprintf(stderr, "\t--batch-size must be positive\n");
        return false;
    }
    if (args->window_size < 0 || args->window_size > 1) {
        fprintf(stderr, "\t--window-size must be in the range [0.0, 1.0]\n");
        return false;
    }
    if (args->num_trials < 1) {
        fprintf(stderr, "\t--num-trials must be positive\n");
        return false;
    }
    return true;
}

void
dynograph_args_parse(int argc, char *argv[], struct dynograph_args *args)
{
    args->sort_mode = UNSORTED;
    args->window_size = 1.0;
    args->num_trials = 1;

    int option_index;
    while (1)
    {
        int c = getopt_long(argc, argv, "", long_options, &option_index);

        // Done parsing
        if (c == -1) { break; }
        // Parse error
        if (c == '?') {
            fprintf(stderr, "Invalid arguments\n");
            print_help(argv[0]);
            dynograph_die();
        }
        const char *option_name = long_options[option_index].name;

        if (!strcmp(option_name, "num-epochs")) {
            args->num_epochs = atoll(optarg);

        } else if (!strcmp(option_name, "alg-names")) {
            const char* alg_str = optarg;
            // FIXME split string, populate list, and store list length
            args->alg_names = "";

        } else if (!strcmp(option_name, "input-path")) {
            args->input_path = optarg;

        } else if (!strcmp(option_name, "batch-size")) {
            args->batch_size = atoll(optarg);

        } else if (!strcmp(option_name, "sort-mode")) {
            const char* sort_mode_str = optarg;
            // FIXME implement sort modes
            args->sort_mode = UNSORTED;
            // if      (sort_mode_str == "unsorted") { args->sort_mode = Args::SORT_MODE::UNSORTED; }
            // else if (sort_mode_str == "presort")  { args->sort_mode = Args::SORT_MODE::PRESORT;  }
            // else if (sort_mode_str == "snapshot") { args->sort_mode = Args::SORT_MODE::SNAPSHOT; }
            // else {
            //     logger << "sort-mode must be one of ['unsorted', 'presort', 'snapshot']\n";
            //     dynograph_die();
            // }

        } else if (!strcmp(option_name, "window-size")) {
            args->window_size = atof(optarg);

        } else if (!strcmp(option_name, "num-trials")) {
            args->num_trials = atoll(optarg);

        } else if (!strcmp(option_name, "help")) {
            print_help(argv[0]);
            dynograph_die();
        }
    }

    bool args_are_valid = validate(args);
    if (!args_are_valid)
    {
        fprintf(stderr, "Terminating due to invalid arguments\n");
        print_help(argv[0]);
        dynograph_die();
    }
}

struct dynograph_dataset*
dynograph_load_edges_binary(const char* path, int64_t num_batches)
{
    dynograph_message("Checking file size of %s...", path);
    FILE* fp = fopen(path, "rb");
    struct stat st;
    if (stat(path, &st) != 0)
    {
        dynograph_error("Failed to stat %s", path);
    }
    int64_t num_edges = st.st_size / sizeof(struct dynograph_edge);

    struct dynograph_dataset *dataset = malloc(
        sizeof(struct dynograph_dataset) +
        sizeof(struct dynograph_edge) * num_edges
    );
    if (dataset == NULL)
    {
        dynograph_error("Failed to allocate memory for %ld edges", num_edges);
    }
    dataset->num_edges = num_edges;
    dataset->num_batches = num_batches;
    dataset->directed = true; // FIXME detect this somehow

    dynograph_message("Preloading %ld %s edges from %s...", num_edges, dataset->directed ? "directed" : "undirected", path);

    size_t rc = fread(&dataset->edges[0], sizeof(struct dynograph_edge), num_edges, fp);
    if (rc != num_edges)
    {
        dynograph_error("Failed to load graph from %s",path);
    }
    fclose(fp);
    return dataset;
}

int64_t
dynograph_count_lines(const char* path)
{
    FILE* fp = fopen(path, "r");
    if (fp == NULL)
    {
        dynograph_error("Failed to open %s", path);
    }
    int64_t lines = 0;
    while(!feof(fp))
    {
        int ch = fgetc(fp);
        if(ch == '\n')
        {
            lines++;
        }
    }
    fclose(fp);
    return lines;
}

struct dynograph_dataset*
dynograph_load_edges_ascii(const char* path, int64_t num_batches)
{
    dynograph_message("Counting lines in %s...", path);
    int64_t num_edges = dynograph_count_lines(path);

    struct dynograph_dataset *dataset = malloc(
        sizeof(struct dynograph_dataset) +
        sizeof(struct dynograph_edge) * num_edges
    );
    if (dataset == NULL)
    {
        dynograph_error("Failed to allocate memory for %ld edges", num_edges);
    }
    dataset->num_edges = num_edges;
    dataset->num_batches = num_batches;
    dataset->directed = true; // FIXME detect this somehow

    dynograph_message("Preloading %ld %s edges from %s...", num_edges, dataset->directed ? "directed" : "undirected", path);

    FILE* fp = fopen(path, "r");
    int rc = 0;
    for (struct dynograph_edge* e = &dataset->edges[0]; rc != EOF; ++e)
    {
        rc = fscanf(fp, "%ld %ld %ld %ld\n", &e->src, &e->dst, &e->weight, &e->timestamp);
    }
    fclose(fp);
    return dataset;
}

bool
dynograph_file_is_binary(const char* path)
{
    const char* suffix = ".graph.bin";
    size_t path_len = strlen(path);
    size_t suffix_len = strlen(suffix);
    return !strcmp(path + path_len - suffix_len, suffix);
}

struct dynograph_dataset*
dynograph_load_dataset(const char* path, int64_t num_batches)
{
    // Sanity check
    if (num_batches < 1)
    {
        dynograph_error("Need at least one batch");
    }

    if (dynograph_file_is_binary(path))
    {
        return dynograph_load_edges_binary(path, num_batches);
    } else {
        return dynograph_load_edges_ascii(path, num_batches);
    }
}

int64_t
dynograph_get_timestamp_for_window(const struct dynograph_dataset *dataset, int64_t batch_id, int64_t window_size)
{
    int64_t modified_after = INT64_MIN;
    if (batch_id > window_size)
    {
        // Intentionally rounding down here
        // TODO variable number of edges per batch
        int64_t edges_per_batch = dataset->num_edges / dataset->num_batches;
        int64_t startEdge = (batch_id - window_size) * edges_per_batch;
        modified_after = dataset->edges[startEdge].timestamp;
    }
    return modified_after;
}

struct dynograph_edge_batch
dynograph_get_batch(const struct dynograph_dataset* dataset, int64_t batch_id)
{
    if (batch_id >= dataset->num_batches)
    {
        dynograph_error("Batch %i does not exist!", batch_id);
    }
    // Intentionally rounding down here
    // TODO variable number of edges per batch
    int64_t edges_per_batch = dataset->num_edges / dataset->num_batches;
    size_t offset = batch_id * edges_per_batch;

    struct dynograph_edge_batch batch = {
        .num_edges = edges_per_batch,
        .directed = dataset->directed,
        .edges = dataset->edges + offset
    };
    return batch;
}

void
dynograph_free_dataset(struct dynograph_dataset * dataset)
{
    free(dataset);
}

void
dynograph_die()
{
    exit(-1);
}