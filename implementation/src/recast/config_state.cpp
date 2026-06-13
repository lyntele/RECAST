

int dimension = 0;
int numPoints = 0;
int numQueries = 0;
vector<vector<float>> points;
vector<vector<float>> queries;
vector<float> radii;
vector<float> points_flat;

#ifdef QFD


static vector<float> qfd_matrix_flat;

static void initQFDMatrix(int d, int seed) {
    srand(seed + 12345);
    vector<float> random_matrix((long long)d * d, 0.0f);
    for (int i = 0; i < d; i++) {
        for (int j = 0; j < d; j++)
            random_matrix[(long long)i * d + j] = ((float)rand() / RAND_MAX - 0.5f) * 2.0f;
    }
    qfd_matrix_flat.assign((long long)d * d, 0.0f);
    for (int i = 0; i < d; i++) {
        for (int j = 0; j < d; j++) {
            float sum = 0.0f;
            for (int k = 0; k < d; k++)
                sum += random_matrix[(long long)k * d + i] * random_matrix[(long long)k * d + j];
            qfd_matrix_flat[(long long)i * d + j] = sum;
        }
    }
}
#endif

long long dc_count = 0;
long long filter_count = 0;
long long leaf_points = 0;

int LEAF_THRESHOLD = 128;
int REGION_PIVOT_MAX = 32;
int QUERY_ACTIVE_PIVOTS = 32;
int POLICY_MODE = 3;
int MIN_SPLIT_VISITS = 4;
int MIN_SPLIT_PIVOTS = 2;
int EXPLORATION_PIVOTS = 2;
int MAX_DEPTH = 32;
double EMA_ALPHA = 0.25;
double CHECKED_HIGH_RATIO = 0.20;
double FP_HIGH_RATIO = 0.80;
double DROP_RATIO = 0.80;
double FLAT_RATIO = 0.95;
double PIVOT_FP_RATIO = 0.50;
int MIN_PIVOT_CHECKED = 64;
double PIVOT_MIN_SCORE = 8.0;
double SPLIT_MIN_SCORE = 0.0;
double SPLIT_RADIUS_MARGIN = 1.20;
double SPLIT_EXTRA_WEIGHT = 1.0;
double SPLIT_COLD_WEIGHT = 0.25;
int RECAST_STABLE_MAX = 24;
int RECAST_NURSERY_MAX = 8;
int RECAST_REGION_ACTIVE = 32;
int RECAST_NURSERY_EXPLORE = 8;
int RECAST_MIN_COVERAGE = 64;
int RECAST_MIN_FALSE_POSITIVE = 32;
int RECAST_NURSERY_TTL = 64;
int RECAST_MIN_PROBE_USES = 4;
int RECAST_SPLIT_PROBATION = 64;
double RECAST_PROMOTE_MARGIN = 16.0;
bool FORCE_PIVOT_ADMISSION = false;
int RECAST_SHADOW_MIN_VISITS = 4;
int RECAST_SHADOW_EMERGENCY_VISITS = 2;
double RECAST_SHADOW_COMMIT_MARGIN = 16.0;
double RECAST_SHADOW_MAX_SELECTIVITY = 0.98;
int RECAST_SHADOW_MIN_SAVED = 64;
int RECAST_SHADOW_TTL = 32;
double RECAST_SHADOW_REPLACE_MARGIN = 1.5;
double RECAST_PIVOT_GOOD_PRUNE_RATE = 0.15;
double RECAST_PIVOT_COLLAPSE_RATE = 0.02;
double RECAST_EMERGENCY_CHECKED_SPIKE = 4.0;
bool RECAST_KEEP_PARENT_EVIDENCE = true;
bool RECAST_SHADOW_FROM_PIVOTS = false;
bool RECAST_WORKLOAD_THRESHOLD = false;

bool RECAST_RESIDUAL_PROMOTION = false;
bool RECAST_EAGER_SHADOW_SPLIT = false;
bool RECAST_RESIDUAL_CHILD = true;
int TIME_AWARE_MODE = 0;

int PIVOT_EARLY_EXIT_ABS = 0;
double PIVOT_EARLY_EXIT_RATIO = 0.0;


int DYNAMIC_ALIVE_SOFT_CAP = 0;
int DYNAMIC_ZERO_KILL_PATIENCE = 0;
long long g_dyn_early_break_hits = 0;


int ACTIVE_SCHEDULER_MIN = 0;
int ACTIVE_SCHEDULER_WARMUP = 16;
int ACTIVE_SCHEDULER_PATIENCE = 2;
double ACTIVE_SCHEDULER_EMA_ALPHA = 0.5;
long long g_scheduler_break_hits = 0;
long long g_scheduler_pivots_skipped = 0;
long long g_scheduler_active_regions = 0;


double SPLIT_RESIDUAL_GUARD = 0.0;
long long g_split_guard_rejects = 0;


int PIVOT_POOL_POLICY = 6;
int SPLIT_POLICY_MODE = 0;

int SEED_PIVOT_ENABLED = 1;


int ADAPTIVE_BUDGET_K = 0;
int ADAPTIVE_BUDGET_THRESHOLD_M = 4096;
int PIVOT_COVERAGE_CAP = 0;
int PIVOT_COVERAGE_MIN_TO_CAP = 20000;


int PRACTICAL_SPLIT_M_THRESHOLD = 0;
int PRACTICAL_SPLIT_MIN_VISITS = 16;
int LIGHTWEIGHT_SPLIT_CHILDREN = 0;
int LIGHTWEIGHT_CHILD_PIVOT_CAP = 0;






int KNN_SEED_CACHE_SIZE = 0;
int KNN_DIRECT_CRACK = 1;
int KNN_SAMPLE_SPLIT = 1;
int KNN_LITE_DISPATCHER = 0;


int RECAST_DIAGNOSTICS = 0;
long long g_recast_diag_leaf_invocations = 0;
long long g_recast_diag_seed_triggered = 0;
long long g_recast_diag_active_empty = 0;
long long g_recast_diag_eligible_at_0 = 0;
long long g_recast_diag_eligible_elsewhere = 0;
long long g_recast_diag_no_eligible = 0;
long long g_recast_diag_inside_band_total = 0;
long long g_recast_diag_inside_band_seed_count = 0;
long long g_recast_diag_M_total = 0;
long long g_recast_diag_M_count = 0;

long long g_recast_time_routing_ns = 0;
long long g_recast_time_active_select_ns = 0;
long long g_recast_time_seed_init_ns = 0;
long long g_recast_time_pivot_filter_ns = 0;
long long g_recast_time_survivor_enum_ns = 0;
long long g_recast_time_growth_ns = 0;
long long g_recast_count_active_pivot_total = 0;
long long g_recast_count_pivot_entries_scanned = 0;
long long g_recast_count_objects_marked_dead = 0;
long long g_recast_count_survivor_total = 0;
long long g_recast_count_visited_region = 0;
long long g_recast_count_residual_region = 0;
long long g_recast_count_growth_trigger = 0;
int RECAST_SHADOW_PIVOT_CANDIDATES = 4;
int RECAST_RECENT_REPLAY = 8;
int RECAST_RECENT_MAX_IDS = 4096;
int RECAST_PROMOTION_MAX_PER_QUERY = 8;
int RECAST_PROMOTION_MIN_FP_COUNT = 4;
double RECAST_PROMOTION_START_MARGIN = 128.0;
long long GLOBAL_MEMORY_BUDGET_PAIRS = 3200000;
string TRACE_PATH;
string TRACE_REGION_PATH;
FILE *g_region_trace_fp = nullptr;
string TRACE_PIVOT_PATH;
FILE *g_pivot_trace_fp = nullptr;
string TRACE_ROUTING_PATH;
FILE *g_routing_trace_fp = nullptr;
string TRACE_SPLIT_PATH;
FILE *g_split_trace_fp = nullptr;
vector<int> candidate_alive_stamp;
int candidate_alive_token = 1;
vector<int> knn_seen_stamp;
int knn_seen_token = 1;
vector<int> shadow_side_stamp;
vector<unsigned char> shadow_side_value;
int shadow_side_token = 1;
vector<int> object_membership_stamp;
int object_membership_token = 1;
vector<pair<int, float>> checked_dists_workspace;
vector<float> threshold_values_workspace;

static inline bool timeAwareSplitEnabled() { return (TIME_AWARE_MODE & 1) != 0; }
static inline bool timeAwareResidualEnabled() { return (TIME_AWARE_MODE & 2) != 0; }

#ifdef RA_COLUMN_ORB_EXECUTOR






long long g_local_pos_current_token = 0;




















int g_use_column_orb = 0;
int g_column_m_max = 2048;



vector<int> object_local_pos;
vector<int> object_local_pos_stamp;
int object_local_pos_token = 1;
#endif
