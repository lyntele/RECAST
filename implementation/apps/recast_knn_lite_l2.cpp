#include <algorithm>
#include <chrono>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <numeric>
#include <queue>
#include <string>
#include <vector>
#include <unistd.h>

using namespace std;

static int dimension = 0;
static int num_points = 0;
static int num_queries = 0;
static vector<float> points_flat;
static vector<vector<float>> queries;
static long long dc_count = 0;
static vector<int> seed_stamp;
static vector<float> seed_dist;
static int seed_token = 1;

static inline float l2_point_query_sq(int id, const float *query) {
    dc_count++;
    const float *point = &points_flat[(long long)id * dimension];
    float total = 0.0f;
    for (int d = 0; d < dimension; d++) {
        float diff = point[d] - query[d];
        total += diff * diff;
    }
    return total;
}

static inline float l2_vec_vec_sq(const vector<float> &left, const vector<float> &right) {
    dc_count++;
    float total = 0.0f;
    for (int d = 0; d < dimension; d++) {
        float diff = left[d] - right[d];
        total += diff * diff;
    }
    return total;
}

static void load_data(const char *path) {
    ifstream in(path);
    int ignored = 0;
    in >> dimension >> num_points >> ignored;
    points_flat.assign((long long)num_points * dimension, 0.0f);
    for (int i = 0; i < num_points; i++)
        for (int d = 0; d < dimension; d++)
            in >> points_flat[(long long)i * dimension + d];
    seed_stamp.assign(num_points, 0);
    seed_dist.assign(num_points, 0.0f);
}

static void load_queries(const char *path) {
    ifstream in(path);
    in >> num_queries;
    queries.assign(num_queries, vector<float>(dimension));
    float ignored_radius = 0.0f;
    for (int i = 0; i < num_queries; i++) {
        in >> ignored_radius;
        for (int d = 0; d < dimension; d++)
            in >> queries[i][d];
    }
}

struct Hit {
    float dist = 0.0f;
    int id = -1;
};

struct HitWorse {
    bool operator()(const Hit &left, const Hit &right) const {
        if (left.dist != right.dist)
            return left.dist < right.dist;
        return left.id < right.id;
    }
};

static inline float kth_radius(const priority_queue<Hit, vector<Hit>, HitWorse> &topk, int k) {
    if ((int)topk.size() < k)
        return FLT_MAX;
    return topk.top().dist;
}

static inline void add_hit(priority_queue<Hit, vector<Hit>, HitWorse> &topk, int k, int id, float dist) {
    if ((int)topk.size() < k) {
        topk.push({dist, id});
        return;
    }
    const Hit &worst = topk.top();
    if (dist < worst.dist || (dist == worst.dist && id < worst.id)) {
        topk.pop();
        topk.push({dist, id});
    }
}

struct Node {
    int start = 0;
    int end = -1;
    int depth = 0;
    int center_query_id = -1;
    float epsilon = 0.0f;
    Node *left = nullptr;
    Node *right = nullptr;
    bool leaf() const { return left == nullptr && right == nullptr; }
};

static Node *new_node(int start, int end, int depth) {
    Node *node = new Node();
    node->start = start;
    node->end = end;
    node->depth = depth;
    return node;
}

static void delete_node(Node *node) {
    if (!node) return;
    delete_node(node->left);
    delete_node(node->right);
    delete node;
}

static int crack(vector<int> &ids, vector<float> &distances, int start, int end, float epsilon) {
    int i = start, j = end;
    while (true) {
        while (i <= end && distances[i] <= epsilon) i++;
        while (j >= start && distances[j] > epsilon) j--;
        if (i >= j) break;
        swap(ids[i], ids[j]);
        swap(distances[i], distances[j]);
    }
    return j;
}

struct Task {
    float lb = 0.0f;
    int order = 0;
    Node *node = nullptr;
};

struct TaskGreater {
    bool operator()(const Task &left, const Task &right) const {
        if (left.lb != right.lb)
            return left.lb > right.lb;
        return left.order > right.order;
    }
};

static void query_knn(Node *root, vector<int> &ids, vector<float> &distances,
                      int query_id, int k, int leaf_threshold, int max_depth,
                      priority_queue<Hit, vector<Hit>, HitWorse> &topk) {
    priority_queue<Task, vector<Task>, TaskGreater> queue;
    int order = 0;
    queue.push({0.0f, order++, root});
    const vector<float> &query = queries[query_id];
    const float *query_ptr = query.data();
    while (!queue.empty()) {
        Task task = queue.top();
        queue.pop();
        float radius = kth_radius(topk, k);
        if (radius < FLT_MAX && task.lb > radius)
            continue;
        Node *node = task.node;
        if (node->leaf()) {
            for (int pos = node->start; pos <= node->end; pos++) {
                int id = ids[pos];
                bool already_seeded = (seed_stamp[id] == seed_token);
                float dist = already_seeded ? seed_dist[id] : l2_point_query_sq(id, query_ptr);
                distances[pos] = dist;
                if (!already_seeded)
                    add_hit(topk, k, id, dist);
            }
            int len = node->end - node->start + 1;
            if (len > leaf_threshold && node->depth < max_depth) {
                int a = node->start + (int)(((long long)len * 1103515245LL + 12345LL) % len);
                int b = node->start + (int)(((long long)len * 2654435761LL + 1013904223LL) % len);
                int c = node->start + (int)(((long long)len * 2246822519LL + 3266489917LL) % len);
                float x = distances[a], y = distances[b], z = distances[c];
                float epsilon = max(min(x, y), min(max(x, y), z));
                int mid = crack(ids, distances, node->start, node->end, epsilon);
                if (mid >= node->start && mid + 1 <= node->end) {
                    node->center_query_id = query_id;
                    node->epsilon = sqrt(epsilon);
                    node->left = new_node(node->start, mid, node->depth + 1);
                    node->right = new_node(mid + 1, node->end, node->depth + 1);
                }
            }
        } else {
            float dist = sqrt(l2_vec_vec_sq(query, queries[node->center_query_id]));
            float left_lb = max(0.0f, dist - node->epsilon);
            float right_lb = max(0.0f, node->epsilon - dist);
            left_lb *= left_lb;
            right_lb *= right_lb;
            radius = kth_radius(topk, k);
            if (radius == FLT_MAX || left_lb <= radius)
                queue.push({left_lb, order++, node->left});
            if (radius == FLT_MAX || right_lb <= radius)
                queue.push({right_lb, order++, node->right});
        }
    }
}

int main(int argc, char **argv) {
    int k = 10;
    int leaf_threshold = 128;
    int max_depth = 32;
    int seed_cache_size = 0;
    int opt = 0;
    while ((opt = getopt(argc, argv, "k:t:h:s:")) != -1) {
        switch (opt) {
            case 'k': k = atoi(optarg); break;
            case 't': leaf_threshold = atoi(optarg); break;
            case 'h': max_depth = atoi(optarg); break;
            case 's': seed_cache_size = atoi(optarg); break;
            default: break;
        }
    }
    if (argc - optind < 2 || k <= 0) {
        cerr << "Usage: " << argv[0] << " [-k K] [-t leaf_threshold] [-s seed_cache_size] data.txt queries.txt\n";
        return 1;
    }
    load_data(argv[optind]);
    load_queries(argv[optind + 1]);
    vector<int> ids(num_points);
    iota(ids.begin(), ids.end(), 0);
    vector<float> distances(num_points, 0.0f);
    Node *root = new_node(0, num_points - 1, 0);
    vector<int> seed_cache;

    cout << "Results\tQueryTime\tDC\tKthDist\n";
    for (int q = 0; q < num_queries; q++) {
        dc_count = 0;
        seed_token++;
        if (seed_token == INT_MAX) {
            fill(seed_stamp.begin(), seed_stamp.end(), 0);
            seed_token = 1;
        }
        priority_queue<Hit, vector<Hit>, HitWorse> topk;
        auto start = chrono::steady_clock::now();
        if (seed_cache_size > 0) {
            const float *query_ptr = queries[q].data();
            int used = 0;
            for (int id : seed_cache) {
                if (used >= seed_cache_size)
                    break;
                if (id < 0 || id >= num_points || seed_stamp[id] == seed_token)
                    continue;
                float dist = l2_point_query_sq(id, query_ptr);
                seed_stamp[id] = seed_token;
                seed_dist[id] = dist;
                add_hit(topk, k, id, dist);
                used++;
            }
        }
        query_knn(root, ids, distances, q, k, leaf_threshold, max_depth, topk);
        auto end = chrono::steady_clock::now();
        double elapsed = chrono::duration<double>(end - start).count();
        cout << topk.size() << "\t" << elapsed << "\t" << dc_count << "\t"
             << (topk.empty() ? 0.0f : sqrt(topk.top().dist)) << "\n";
        seed_cache.clear();
        priority_queue<Hit, vector<Hit>, HitWorse> copy = topk;
        vector<Hit> hits;
        while (!copy.empty()) {
            hits.push_back(copy.top());
            copy.pop();
        }
        sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) {
            if (a.dist != b.dist)
                return a.dist < b.dist;
            return a.id < b.id;
        });
        for (const Hit &hit : hits) {
            if ((int)seed_cache.size() >= seed_cache_size)
                break;
            seed_cache.push_back(hit.id);
        }
    }
    delete_node(root);
    return 0;
}
