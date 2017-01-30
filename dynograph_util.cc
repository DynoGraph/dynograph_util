#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <iostream>
#include <assert.h>
#include <sstream>
#include <algorithm>
#include <string>
#include <getopt.h>

#if defined(USE_MPI)
#include <mpi.h>
#include <valarray>

#endif

#include "dynograph_util.h"

using namespace DynoGraph;
using std::cerr;
using std::string;
using std::vector;
using std::shared_ptr;
using std::make_shared;
using std::stringstream;

// Helper functions to split strings
// http://stackoverflow.com/a/236803/1877086
void split(const string &s, char delim, vector<string> &elems) {
    stringstream ss(s);
    string item;
    while (getline(ss, item, delim)) {
        elems.push_back(item);
    }
}
vector<string> split(const string &s, char delim) {
    vector<string> elems;
    split(s, delim, elems);
    return elems;
}

static const option long_options[] = {
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

static const std::pair<string, string> option_descriptions[] = {
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
};

void
Args::print_help(string argv0){
    Logger &logger = Logger::get_instance();
    stringstream oss;
    oss << "Usage: " << argv0 << " [OPTIONS]\n";
    for (auto &o : option_descriptions)
    {
        const string &name = o.first;
        const string &desc = o.second;
        oss << "\t--" << name << "\t" << desc << "\n";
    }
    logger << oss.str();
}

Args
Args::parse(int argc, char *argv[])
{
    Logger &logger = Logger::get_instance();
    Args args = {};
    args.sort_mode = Args::SORT_MODE::UNSORTED;
    args.window_size = 1.0;
    args.num_trials = 1;

    int option_index;
    while (1)
    {
        int c = getopt_long(argc, argv, "", long_options, &option_index);

        // Done parsing
        if (c == -1) { break; }
        // Parse error
        if (c == '?') {
            logger << "Invalid arguments\n";
            print_help(argv[0]);
            die();
        }
        string option_name = long_options[option_index].name;

        if (option_name == "num-epochs") {
            args.num_epochs = static_cast<int64_t>(std::stoll(optarg));

        } else if (option_name == "alg-names") {
            std::string alg_str = optarg;
            args.alg_names = split(alg_str, ' ');

        } else if (option_name == "input-path") {
            args.input_path = optarg;

        } else if (option_name == "batch-size") {
            args.batch_size = static_cast<int64_t>(std::stoll(optarg));

        } else if (option_name == "sort-mode") {
            std::string sort_mode_str = optarg;
            if      (sort_mode_str == "unsorted") { args.sort_mode = Args::SORT_MODE::UNSORTED; }
            else if (sort_mode_str == "presort")  { args.sort_mode = Args::SORT_MODE::PRESORT;  }
            else if (sort_mode_str == "snapshot") { args.sort_mode = Args::SORT_MODE::SNAPSHOT; }
            else {
                logger << "sort-mode must be one of ['unsorted', 'presort', 'snapshot']\n";
                die();
            }

        } else if (option_name == "window-size") {
            args.window_size = std::stod(optarg);

        } else if (option_name == "num-trials") {
            args.num_trials = static_cast<int64_t>(std::stoll(optarg));

        } else if (option_name == "help") {
            print_help(argv[0]);
            die();
        }
    }

    string message = args.validate();
    if (!message.empty())
    {
        logger << "Invalid arguments:\n" + message;
        print_help(argv[0]);
        die();
    }
    return args;
}

string
Args::validate() const
{
    stringstream oss;
    if (num_epochs < 1) {
        oss << "\t--num-epochs must be positive\n";
    }
    if (input_path.empty()) {
        oss << "\t--input-path cannot be empty\n";
    }
    if (batch_size < 1) {
        oss << "\t--batch-size must be positive\n";
    }
    if (window_size < 0 || window_size > 1) {
        oss << "\t--window-size must be in the range [0.0, 1.0]\n";
    }
    if (num_trials < 1) {
        oss << "\t--num-trials must be positive\n";
    }
    return oss.str();
}

std::ostream&
DynoGraph::operator <<(std::ostream& os, Args::SORT_MODE sort_mode)
{
    switch (sort_mode) {
        case Args::SORT_MODE::UNSORTED: os << "unsorted"; break;
        case Args::SORT_MODE::PRESORT: os << "presort"; break;
        case Args::SORT_MODE::SNAPSHOT: os << "snapshot"; break;
        default: assert(0);
    }
    return os;
}

std::ostream&
DynoGraph::operator <<(std::ostream& os, const Args& args)
{
    os  << "{"
        << "\"num_epochs\":"  << args.num_epochs << ","
        << "\"input_path\":\""  << args.input_path << "\","
        << "\"batch_size\":"  << args.batch_size << ","
        << "\"window_size\":" << args.window_size << ","
        << "\"num_trials\":"  << args.num_trials << ","
        << "\"sort_mode\":\""   << args.sort_mode << "\",";

    os << "\"alg_names\":[";
    for (size_t i = 0; i < args.alg_names.size(); ++i) {
        if (i != 0) { os << ","; }
        os << "\"" << args.alg_names[i] << "\"";
    }
    os << "]}";
    return os;
};

bool DynoGraph::operator<(const Edge& a, const Edge& b)
{
    // Custom sorting order to prepare for deduplication
    // Order by src ascending, then dest ascending, then timestamp descending
    // This way the edge with the most recent timestamp will be picked when deduplicating
    return (a.src != b.src) ? a.src < b.src
         : (a.dst != b.dst) ? a.dst < b.dst
         : a.timestamp > b.timestamp;
}

bool DynoGraph::operator==(const Edge& a, const Edge& b)
{
    return a.src == b.src
        && a.dst == b.dst
        && a.weight == b.weight
        && a.timestamp == b.timestamp;
}

// Count the number of lines in a text file
int64_t
count_lines(string path)
{
    Logger &logger = Logger::get_instance();
    FILE* fp = fopen(path.c_str(), "r");
    if (fp == NULL)
    {
        logger << "Failed to open " << path << "\n";
        die();
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

// Implementation of DynoGraph::Batch

Batch::Batch(iterator begin, iterator end)
 : begin_iter(begin), end_iter(end) {}

Batch::iterator
Batch::begin() const { return begin_iter; }

Batch::iterator
Batch::end() const { return end_iter; }

int64_t Batch::num_vertices_affected() const
{
    // We need to sort and deduplicate anyways, just use the implementation in DeduplicatedBatch
    auto sorted = DeduplicatedBatch(*this);
    return sorted.num_vertices_affected();
}

const Edge &
Batch::operator[](size_t i) const {
    assert(begin_iter + i < end_iter);
    return *(begin_iter + i);
}

size_t
Batch::size() const {
    return std::distance(begin_iter, end_iter);
}

bool
Batch::is_directed() const {
    return true;
}


// Implementation of DynoGraph::DeduplicatedBatch

DeduplicatedBatch::DeduplicatedBatch(const Batch &batch)
: Batch(batch), deduped_edges(std::distance(batch.begin(), batch.end())) {
    // Make a copy of the original batch
    std::vector<Edge> sorted_edges(batch.begin(), batch.end()); // TODO is this init done in parallel?
    // Sort the edge list
    std::sort(sorted_edges.begin(), sorted_edges.end());

    // Deduplicate the edge list
    // Using std::unique_copy since there is no parallel version of std::unique
    auto end = std::unique_copy(sorted_edges.begin(), sorted_edges.end(), deduped_edges.begin(),
            // We consider only source and dest when searching for duplicates
            // The input is sorted, so we'll only get the most recent timestamp
            // BUG: Does not combine weights
            [](const Edge& a, const Edge& b) { return a.src == b.src && a.dst == b.dst; });
    deduped_edges.erase(end, deduped_edges.end());

    // Reinitialize the batch pointers
    begin_iter = deduped_edges.begin();
    end_iter = deduped_edges.end();
}

int64_t
DeduplicatedBatch::num_vertices_affected() const
{
    // Get a list of just the vertex ID's in this batch
    vector<int64_t> src_vertices(deduped_edges.size());
    vector<int64_t> dst_vertices(deduped_edges.size());
    std::transform(deduped_edges.begin(), deduped_edges.end(), src_vertices.begin(),
        [](const Edge& e){ return e.src; });
    std::transform(deduped_edges.begin(), deduped_edges.end(), dst_vertices.begin(),
        [](const Edge& e){ return e.dst; });
    src_vertices.insert(src_vertices.end(), dst_vertices.begin(), dst_vertices.end());

    // Deduplicate
    vector<int64_t> unique_vertices(src_vertices.size());
    std::sort(src_vertices.begin(), src_vertices.end());
    auto end = std::unique_copy(src_vertices.begin(), src_vertices.end(), unique_vertices.begin());
    unique_vertices.erase(end, unique_vertices.end());

    return unique_vertices.size();
}

FilteredBatch::FilteredBatch(const Batch &batch, int64_t threshold)
: Batch(batch)
{
    // Skip past edges that are older than the threshold
    begin_iter = std::find_if(batch.begin(), batch.end(),
        [threshold](const Edge& e) { return e.timestamp >= threshold; });
}

// Implementation of DynoGraph::Dataset

// Helper function to test a string for a given suffix
// http://stackoverflow.com/questions/20446201
bool has_suffix(const std::string &str, const std::string &suffix)
{
    return str.size() >= suffix.size() &&
           str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

Dataset::Dataset(Args args)
: args(args), directed(true)
{
    Logger &logger = Logger::get_instance();
#if defined(USE_MPI)
    int rank, comm_size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);
    if (rank == 0){
#endif
    // Load edges from the file
    if (has_suffix(args.input_path, ".graph.bin"))
    {
        loadEdgesBinary(args.input_path);
    } else if (has_suffix(args.input_path, ".graph.el")) {
        loadEdgesAscii(args.input_path);
    } else {
        logger << "Unrecognized file extension for " << args.input_path << "\n";
        die();
    }

    // Intentionally rounding down to make it divide evenly
    int64_t num_batches = edges.size() / args.batch_size;

    // Sanity check on arguments
    if (static_cast<size_t>(args.batch_size) > edges.size())
    {
        logger << "Invalid arguments: batch size (" << args.batch_size << ") "
               << "cannot be larger than the total number of edges in the dataset "
               << " (" << edges.size() << ")\n";
        die();
    }

    if (args.num_epochs > num_batches)
    {
        logger << "Invalid arguments: number of epochs (" << args.num_epochs << ") "
               << "cannot be greater than the number of batches in the dataset "
               << "(" << num_batches << ")\n";
        die();
    }

    // Calculate max vertex id so engines can statically provision the vertex array
    auto max_edge = std::max_element(edges.begin(), edges.end(),
        [](const Edge& a, const Edge& b) { return std::max(a.src, a.dst) < std::max(b.src, b.dst); });
    max_vertex_id = std::max(max_edge->src, max_edge->dst);

    // Make sure edges are sorted by timestamp, and save min/max timestamp
    if (!std::is_sorted(edges.begin(), edges.end(),
        [](const Edge& a, const Edge& b) { return a.timestamp < b.timestamp; }))
    {
        logger << "Invalid dataset: edges not sorted by timestamp\n";
        die();
    }

    min_timestamp = edges.front().timestamp;
    max_timestamp = edges.back().timestamp;

    // Make sure there are no self-edges
    auto self_edge = std::find_if(edges.begin(), edges.end(),
        [](const Edge& e) { return e.src == e.dst; });
    if (self_edge != edges.end()) {
        logger << "Invalid dataset: no self-edges allowed\n";
        die();
    }

#if defined(USE_MPI)
        logger << "Distributing dataset to " << comm_size << " ranks...\n";
    }

    // Synchronize member variables across ranks
    MPI_Bcast(&max_vertex_id, 1, MPI_INT64_T, 0, MPI_COMM_WORLD);
    MPI_Bcast(&min_timestamp, 1, MPI_INT64_T, 0, MPI_COMM_WORLD);
    MPI_Bcast(&max_timestamp, 1, MPI_INT64_T, 0, MPI_COMM_WORLD);

    // Move the edges out of rank 0's member variable
    vector<Edge> all_edges = std::move(edges);
    edges.clear();

    /*
     * Now we need to distribute the edges to each process in the group
     * Each process will have the same number of batches, but each batch
     * will be a slice of the corresponding batch in the parent
     */
    int64_t edges_per_batch = all_edges.size() / args.num_batches;
    MPI_Bcast(&edges_per_batch, 1, MPI_INT64_T, 0, MPI_COMM_WORLD);
    vector<int> edges_per_slice(comm_size);
    std::valarray<int> displacements(comm_size+1);
    if (rank == 0)
    {
        // Divide the batch evenly among the ranks
        for (int i = 0; i < comm_size; ++i)
        {
            // Assign a portion of edges to this rank and distribute the remainder evenly
            int slice_size = std::floor(edges_per_batch / comm_size);
            if (i < edges_per_batch % comm_size) { slice_size += 1; }
            edges_per_slice[i] = slice_size;
        }
        // Calculate the boundaries of each slice in the all_edges buffer
        std::partial_sum(edges_per_slice.begin(), edges_per_slice.end(), std::begin(displacements));
        // Prefix sum gives the end of each buffer, shift to get a list of beginnings
        displacements = displacements.shift(-1);
    }

    // Send the size of each slice to the ranks
    int local_slice_size;
    MPI_Scatter(
        edges_per_slice.data(), 1, MPI_INT,
        &local_slice_size, 1, MPI_INT,
        0, MPI_COMM_WORLD
    );

    // Register the Edge type with MPI
    MPI_Datatype edge_type;
    MPI_Type_contiguous(4, MPI_INT64_T, &edge_type);
    MPI_Type_commit(&edge_type);

    // Allocate local storage for this rank's slices
    edges.resize(local_slice_size * args.num_batches);
    Edge * local_slice_begin = edges.data();
    for (int batch_id = 0; batch_id < args.num_batches; ++batch_id)
    {
        // Scatter the slices in this batch to the ranks
        MPI_Scatterv(
            // Source buffer is always the global list of edges in rank 0
            all_edges.data(),
            // List of how big each rank's slice is ( edges per batch divided by number of ranks, +/- 1)
            edges_per_slice.data(),
            // Start positions of each slice within all_edges
            &displacements[0],
            // Send data type = Edge
            edge_type,
            // Destination buffer is pointer into slice of local edge list
            local_slice_begin,
            // Number of edges in this rank's slice
            local_slice_size,
            // Receive data type = Edge
            edge_type,
            // Scatter from rank 0, use global communicator
            0, MPI_COMM_WORLD
        );

        // Move to the next batch in the source and destination buffers
        displacements += edges_per_batch;
        local_slice_begin += local_slice_size;
    }
#endif

    // Store iterators to the beginning and end of each batch
    // BUG In the MPI case, batch_size is wrong, because we have divided the edges between ranks
#ifdef USE_MPI
    logger << "FIXME: Need to recalculate batch size\n";
    die();
#endif

    for (int i = 0; i < num_batches; ++i)
    {
        size_t offset = i * args.batch_size;
        auto begin = edges.begin() + offset;
        auto end = edges.begin() + offset + args.batch_size;
        batches.push_back(Batch(begin, end));
    }
}

void
Dataset::loadEdgesBinary(string path)
{
    Logger &logger = Logger::get_instance();
    logger << "Checking file size of " << path << "...\n";
    FILE* fp = fopen(path.c_str(), "rb");
    struct stat st;
    if (stat(path.c_str(), &st) != 0)
    {
        logger << "Failed to stat " << path << "\n";
        die();
    }
    int64_t numEdges = st.st_size / sizeof(Edge);

    string directedStr = directed ? "directed" : "undirected";
    logger << "Preloading " << numEdges << " "
         << directedStr
         << " edges from " << path << "...\n";

    edges.resize(numEdges);

    size_t rc = fread(&edges[0], sizeof(Edge), numEdges, fp);
    if (rc != static_cast<size_t>(numEdges))
    {
        logger << "Failed to load graph from " << path << "\n";
        die();
    }
    fclose(fp);
}

void
Dataset::loadEdgesAscii(string path)
{
    Logger &logger = Logger::get_instance();
    logger << "Counting lines in " << path << "...\n";
    int64_t numEdges = count_lines(path);

    string directedStr = directed ? "directed" : "undirected";
    logger << "Preloading " << numEdges << " "
         << directedStr
         << " edges from " << path << "...\n";

    edges.resize(numEdges);

    FILE* fp = fopen(path.c_str(), "r");
    int rc = 0;
    for (Edge* e = &edges[0]; rc != EOF; ++e)
    {
        rc = fscanf(fp, "%ld %ld %ld %ld\n", &e->src, &e->dst, &e->weight, &e->timestamp);
    }
    fclose(fp);
}

// Round down to nearest integer
static int64_t
round_down(double x)
{
    return static_cast<int64_t>(std::floor(x));
}

// Divide two integers with double result
template <typename X, typename Y>
static double
true_div(X x, Y y)
{
    return static_cast<double>(x) / static_cast<double>(y);
}

int64_t
Dataset::getTimestampForWindow(int64_t batchId) const
{
    // Calculate width of timestamp window
    int64_t window_time = round_down(args.window_size * (max_timestamp - min_timestamp));
    // Get the timestamp of the last edge in the current batch
    int64_t latest_time = (batches[batchId].end()-1)->timestamp;

    return std::max(min_timestamp, latest_time - window_time);
};

bool
Dataset::enableAlgsForBatch(int64_t batch_id) const {
    // How many batches in each epoch, on average?
    double batches_per_epoch = true_div(batches.size(), args.num_epochs);
    // How many algs run before this batch?
    int64_t batches_before = round_down(true_div(batch_id, batches_per_epoch));
    // How many algs should run after this batch?
    int64_t batches_after = round_down(true_div((batch_id + 1), batches_per_epoch));
    // If the count changes between this batch and the next, we should run an alg now
    return (batches_after - batches_before) > 0;
}

shared_ptr<Batch>
Dataset::getBatch(int64_t batchId) const
{
    int64_t threshold = getTimestampForWindow(batchId);
    const Batch& b = batches[batchId];
    switch (args.sort_mode)
    {
        case Args::SORT_MODE::UNSORTED:
        {
            return make_shared<FilteredBatch>(b, threshold);;
        }
        case Args::SORT_MODE::PRESORT:
        {
            return make_shared<DeduplicatedBatch>(FilteredBatch(b, threshold));
        }
        case Args::SORT_MODE::SNAPSHOT:
        {
            Batch cumulative_snapshot(edges.begin(), b.end());
            return make_shared<DeduplicatedBatch>(FilteredBatch(cumulative_snapshot, threshold));
        }
        default: assert(0); return nullptr;
    }
}

bool
Dataset::isDirected() const
{
    return directed;
}

int64_t
Dataset::getMaxVertexId() const
{
    return max_vertex_id;
}

// Partial implementation of DynamicGraph

DynamicGraph::DynamicGraph(const Args args, int64_t max_vertex_id)
: args(args) {}

// Implementation of vertex_degree
vertex_degree::vertex_degree() {}

vertex_degree::vertex_degree(int64_t vertex_id, int64_t out_degree)
: vertex_id(vertex_id), out_degree(out_degree) {}

bool
DynoGraph::operator < (const vertex_degree &a, const vertex_degree &b) {
    if (a.out_degree != b.out_degree) { return a.out_degree < b.out_degree; }
    return a.vertex_id > b.vertex_id;
}

// Implementation of Logger
Logger::Logger (std::ostream &out) : out(out) {}

Logger&
Logger::get_instance() {
    static Logger instance(std::cerr);
    return instance;
}

Logger&
Logger::operator<<(std::ostream& (*manip)(std::ostream&)) {
    oss << manip;
    return *this;
}

Logger::~Logger() {
    // Flush remaining buffered output in case of forgotten newline
    if (oss.str().size() > 0) { out << msg << oss.str(); }
}

void
DynoGraph::die()
{
    exit(-1);
}