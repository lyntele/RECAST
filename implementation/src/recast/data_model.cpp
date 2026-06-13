struct PivotEvidence {
    vector<float> center;
    unordered_map<int, float> dist_by_id;
    vector<pair<float, int>> dist_sorted;
    float min_dist = FLT_MAX;
    float max_dist = 0.0f;
    int birth_query = -1;
    int birth_region_visit = 0;
    int pool = 0;


    long long uses = 0;
    int last_used_region_visit = 0;
    long long query_dist_cost = 0;
    long long pruned = 0;
    long long memory_pairs = 0;
    bool lookup_built = false;
    bool sorted_built = false;






    double roi_pruned_ema = 0.0;
    double roi_cost_ema = 0.0;
    long long roi_samples = 0;
#ifdef RA_COLUMN_ORB_EXECUTOR



    vector<float> col_dist;
    vector<uint64_t> col_valid_mask;
    int col_region_M = 0;
#endif
};

struct ShadowSplit {
    bool active = false;
    vector<float> center;
    vector<pair<int, float>> assigned;
    int birth_query = -1;
    int birth_region_visit = 0;
    float threshold = 0.0f;
    float left_min = FLT_MAX;
    float left_max = 0.0f;
    float right_min = FLT_MAX;
    float right_max = 0.0f;
    int left_count = 0;
    int right_count = 0;
    long long visits = 0;
    long long routing_dc = 0;
    long long skipped_sample = 0;
    long long visited_sample = 0;
    long long counterfactual_saved_true_checks = 0;
    int eval_query_id = -1;
    bool eval_visit_left = true;
    bool eval_visit_right = true;
    int side_token = 0;
    double net = 0.0;
    bool from_pivot = false;
};

struct RecentObservation {
    vector<float> query;
    float radius = 0.0f;
    vector<int> checked_ids;
};

struct Region {
    int id = 0;
    int depth = 0;
    vector<int> objects;
    long long subtree_size = 0;
    vector<PivotEvidence> pivots;

    Region *left = nullptr;
    Region *right = nullptr;
    Region *residual = nullptr;
    bool is_residual_region = false;
    vector<float> split_center;
    int split_center_query_id = -1;
    float split_threshold = 0.0f;
    float left_min = FLT_MAX;
    float left_max = 0.0f;
    float right_min = FLT_MAX;
    float right_max = 0.0f;
    int split_birth_visit = 0;
    long long split_visits = 0;
    long long split_routing_dc = 0;
    int last_routing_query_id = -1;
    float last_routing_qdist = 0.0f;
    long long split_skipped_objects = 0;
    long long split_residual_fill_extra_dc = 0;

    int visits = 0;
    int exploration_installs = 0;
    bool lightweight_runtime = false;
    int lightweight_growth_installs = 0;
    bool ema_ready = false;
    double checked_ema = 0.0;
    long long last_checked = 0;
    int adaptive_eviction_mode = 0;

    vector<RecentObservation> recent_observations;
    unordered_map<int, int> residual_fp_counts;
    vector<int> knn_seed_ids;

    bool isLeaf() const {
        return left == nullptr && right == nullptr;
    }

    ShadowSplit shadow;
#ifdef RA_COLUMN_ORB_EXECUTOR



    int col_M = 0;
    int col_version = 0;
    vector<uint64_t> alive_mask_ws;



    long long objects_version = 0;
    long long local_pos_token = 0;
    long long local_pos_built_for_version = -1;
#endif
};
