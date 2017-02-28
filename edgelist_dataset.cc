//
// Created by ehein6 on 2/27/17.
//

#include "edgelist_dataset.h"
#include "mpi_macros.h"
#include "helpers.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

using namespace DynoGraph;
using std::shared_ptr;
using std::make_shared;

EdgeListDataset::EdgeListDataset(Args args)
        : args(args), directed(true)
{
    MPI_RANK_0_ONLY {
        Logger &logger = Logger::get_instance();
        // Load edges from the file
        if (has_suffix(args.input_path, ".graph.bin")) {
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

        for (int i = 0; i < num_batches; ++i)
        {
            size_t offset = i * args.batch_size;
            auto begin = edges.begin() + offset;
            auto end = edges.begin() + offset + args.batch_size;
            batches.push_back(Batch(begin, end));
        }

    } // end MPI_RANK_0_ONLY
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

void
EdgeListDataset::loadEdgesBinary(string path)
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
EdgeListDataset::loadEdgesAscii(string path)
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

int64_t
EdgeListDataset::getTimestampForWindow(int64_t batchId) const
{
    int64_t timestamp;
    MPI_RANK_0_ONLY {
    // Calculate width of timestamp window
    int64_t window_time = round_down(args.window_size * (max_timestamp - min_timestamp));
    // Get the timestamp of the last edge in the current batch
    int64_t latest_time = (batches[batchId].end()-1)->timestamp;

    timestamp = std::max(min_timestamp, latest_time - window_time);
    }

    MPI_BROADCAST_RESULT(timestamp);
    return timestamp;
};

bool
EdgeListDataset::enableAlgsForBatch(int64_t batch_id) const {
    bool enable;
    MPI_RANK_0_ONLY {
            // How many batches in each epoch, on average?
            double batches_per_epoch = true_div(batches.size(), args.num_epochs);
            // How many algs run before this batch?
            int64_t batches_before = round_down(true_div(batch_id, batches_per_epoch));
            // How many algs should run after this batch?
            int64_t batches_after = round_down(true_div((batch_id + 1), batches_per_epoch));
            // If the count changes between this batch and the next, we should run an alg now
            enable = (batches_after - batches_before) > 0;
    }
    MPI_BROADCAST_RESULT(enable);
    return enable;
}

shared_ptr<Batch>
EdgeListDataset::getBatch(int64_t batchId)
{
    MPI_RANK_0_ONLY {
    return make_shared<Batch>(batches[batchId]);
    }
    // For MPI, ranks other than zero get an empty batch
    return make_shared<Batch>(edges.end(), edges.end());
}

shared_ptr<Batch>
EdgeListDataset::getBatchesUpTo(int64_t batchId)
{
    MPI_RANK_0_ONLY {
    return make_shared<Batch>(edges.begin(), batches[batchId].end());
    }
    // For MPI, ranks other than zero get an empty batch
    return make_shared<Batch>(edges.end(), edges.end());
}

bool
EdgeListDataset::isDirected() const
{
    bool retval;
    MPI_RANK_0_ONLY { retval = directed; }
    MPI_BROADCAST_RESULT(retval);
    return retval;
}

int64_t
EdgeListDataset::getMaxVertexId() const
{
    int64_t retval = max_vertex_id;
    MPI_RANK_0_ONLY { retval = max_vertex_id; }
    MPI_BROADCAST_RESULT(retval);
    return retval;
}

int64_t EdgeListDataset::getNumBatches() const {
    int64_t retval;
    MPI_RANK_0_ONLY { retval = static_cast<int64_t>(batches.size()); }
    MPI_BROADCAST_RESULT(retval);
    return retval;
};

int64_t EdgeListDataset::getNumEdges() const {
    int64_t retval;
    MPI_RANK_0_ONLY { retval = static_cast<int64_t>(edges.size()); }
    MPI_BROADCAST_RESULT(retval);
    return retval;
};
