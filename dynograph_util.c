#include "dynograph_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

// Known bug in emu toolchain - can't use getopt/getopt_long
#define GETOPT_IS_BROKEN
#ifdef GETOPT_IS_BROKEN
#define required_argument 0
#define no_argument 1
struct option {
    const char *name;
    int         has_arg;
    int        *flag;
    int         val;
};
#else
#include <getopt.h>
#endif

// stat fails with "unknown syscall"
#define STAT_IS_BROKEN
#ifndef STAT_IS_BROKEN
#include <sys/stat.h>
#endif

void
dynograph_die()
{
    exit(-1);
}

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
    dynograph_die();
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
    dynograph_message("Usage: %s [OPTIONS]", argv0);
    for (const struct option_desc *o = option_descriptions; o->name != NULL; ++o)
    {
        dynograph_message("\t--%s\t%s", o->name, o->desc);
    }
}

bool
validate(const struct dynograph_args *args)
{
    if (args->num_epochs < 1) {
        dynograph_message("\t--num-epochs must be positive");
        return false;
    }
    if (strlen(args->input_path) == 0) {
        dynograph_message("\t--input-path cannot be empty");
        return false;
    }
    if (args->batch_size < 1) {
        dynograph_message("\t--batch-size must be positive");
        return false;
    }
    if (args->window_size < 0 || args->window_size > 1) {
        dynograph_message("\t--window-size must be in the range [0.0, 1.0]");
        return false;
    }
    if (args->num_trials < 1) {
        dynograph_message("\t--num-trials must be positive");
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

#ifdef GETOPT_IS_BROKEN
    if (argc != 2) {
        print_help(argv[0]);
        dynograph_die();
    }

    FILE* arg_file = fopen(argv[1], "r");
    if (arg_file == NULL)
    {
        dynograph_error("Unable to load arg file %s", argv[1]);
    }
    char option_name[128];
    char optarg[256];
    while (fscanf(arg_file, "%s %s", &option_name, &optarg) == 2)
    {
#else
    int option_index;
    while (1)
    {
        int c = getopt_long(argc, argv, "", long_options, &option_index);

        // Done parsing
        if (c == -1) { break; }
        // Parse error
        if (c == '?') {
            dynograph_message("Invalid arguments");
            print_help(argv[0]);
            dynograph_die();
        }

        const char *option_name = long_options[option_index].name;
#endif
        if (!strcmp(option_name, "num-epochs")) {
            args->num_epochs = atoll(optarg);

        } else if (!strcmp(option_name, "alg-names")) {
            // FIXME split string, populate list, and store list length
            strcpy(args->alg_names, optarg);

        } else if (!strcmp(option_name, "input-path")) {
            strcpy(args->input_path, optarg);

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
        dynograph_message("Terminating due to invalid arguments");
        print_help(argv[0]);
        dynograph_die();
    }

#ifdef GETOPT_IS_BROKEN
    fclose(arg_file);
#endif
}

static int64_t
count_edges(const char* path)
{
    #ifdef STAT_IS_BROKEN
        FILE* fp = fopen(path, "rb");
        fseek(fp, 0L, SEEK_END);
        size_t size = ftell(fp);
        int64_t num_edges = size / sizeof(struct dynograph_edge);
    #else
        struct stat st;
        if (stat(path, &st) != 0)
        {
            dynograph_error("Failed to stat %s", path);
        }
        int64_t num_edges = st.st_size / sizeof(struct dynograph_edge);
    #endif

    return num_edges;
}

struct dynograph_dataset*
dynograph_load_edges_binary(const char* path)
{
    dynograph_message("Checking file size of %s...", path);
    FILE* fp = fopen(path, "rb");
    if (fp == NULL) {
        dynograph_message("Unable to open %s", path);
        dynograph_die();
    }

    int64_t num_edges = count_edges(path);
    struct dynograph_dataset *dataset = malloc(
        sizeof(struct dynograph_dataset) +
        sizeof(struct dynograph_edge) * num_edges
    );
    if (dataset == NULL)
    {
        dynograph_error("Failed to allocate memory for %ld edges", num_edges);
    }
    dataset->num_edges = num_edges;
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
dynograph_load_edges_ascii(const char* path)
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

static int64_t max3(int64_t a, int64_t b, int64_t c)
{
    if (a > b){
        if (a > c) { return a; }
        else { return c; }
    } else {
        if (b > c) { return b; }
        else { return c; }
    }
}

static int64_t max2(int64_t a, int64_t b)
{
    return a > b ? a : b;
}

struct dynograph_dataset*
dynograph_load_dataset(const struct dynograph_args * args)
{
    struct dynograph_dataset * dataset;
    if (dynograph_file_is_binary(args->input_path))
    {
        dataset = dynograph_load_edges_binary(args->input_path);
    } else {
        dataset = dynograph_load_edges_ascii(args->input_path);
    }

    dataset->args = args;
    dataset->num_batches = dataset->num_edges / dataset->args->batch_size;

    // FIXME make parallel or load from file
    int64_t max_vertex_id = 0;
    for (int64_t i = 0; i < dataset->num_edges; ++i)
    {
        max_vertex_id = max3(max_vertex_id, dataset->edges[i].src, dataset->edges[i].dst);
    }
    dataset->max_vertex_id = max_vertex_id;

    return dataset;
}

// Round down to nearest integer
static int64_t
round_down(double x)
{
    return (int64_t)x;
}

int64_t
dynograph_get_timestamp_for_window(const struct dynograph_dataset *dataset, const struct dynograph_edge_batch *batch)
{
    // Calculate width of timestamp window
    int64_t min_timestamp = dataset->edges[0].timestamp;
    int64_t max_timestamp = dataset->edges[dataset->num_edges-1].timestamp;
    int64_t window_time = round_down(dataset->args->window_size * (max_timestamp - min_timestamp));
    // Get the timestamp of the last edge in the current batch
    int64_t latest_time = batch->edges[batch->num_edges-1].timestamp;

    return max2(min_timestamp, latest_time - window_time);
}

#define true_div(X,Y) ((double)X / (double)Y)

bool
dynograph_enable_algs_for_batch(const struct dynograph_dataset *dataset, int64_t batch_id) {
    // How many batches in each epoch, on average?
    double batches_per_epoch = true_div(dataset->num_batches, dataset->args->num_epochs);
    // How many algs run before this batch?
    int64_t batches_before = round_down(true_div(batch_id, batches_per_epoch));
    // How many algs should run after this batch?
    int64_t batches_after = round_down(true_div((batch_id + 1), batches_per_epoch));
    // If the count changes between this batch and the next, we should run an alg now
    return (batches_after - batches_before) > 0;
}

struct dynograph_edge_batch
dynograph_get_batch(const struct dynograph_dataset* dataset, int64_t batch_id)
{
    if (batch_id >= dataset->num_batches)
    {
        dynograph_message("Batch %i does not exist!", batch_id);
        dynograph_die();
    }
    // Intentionally rounding down here
    // TODO variable number of edges per batch
    int64_t edges_per_batch = dataset->args->batch_size;
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