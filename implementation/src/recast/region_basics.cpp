int next_region_id = 0;
Region *g_root_region = nullptr;

struct QueryStats {
    int result = 0;
    long long checked = 0;
};

Region* newRegion(vector<int> objs, int depth) {
    Region *region = new Region();
    region->id = next_region_id++;
    region->depth = depth;
    region->objects.swap(objs);
    region->subtree_size = (long long)region->objects.size();
#ifdef RA_COLUMN_ORB_EXECUTOR
    region->col_M = (int)region->objects.size();
#endif
    return region;
}

void deleteRegion(Region *region) {
    if (!region) return;
    deleteRegion(region->left);
    deleteRegion(region->right);
    deleteRegion(region->residual);
    for (const auto &pivot : region->pivots) {
        for (const auto &entry : pivot.dist_sorted)
            releaseEvidencePair(pivot.birth_query, entry.second);
    }
    delete region;
}

void loadData(const char *path) {
    ifstream in(path);
    if (!in) {
        cerr << "cannot open data file: " << path << endl;
        exit(1);
    }
    int ignored = 0;
    in >> dimension >> numPoints >> ignored;
    points.assign(numPoints, vector<float>(dimension));
    points_flat.assign((long long)numPoints * dimension, 0.0f);
    candidate_alive_stamp.assign(numPoints, 0);
    knn_seen_stamp.assign(numPoints, 0);
    shadow_side_stamp.assign(numPoints, 0);
    shadow_side_value.assign(numPoints, 0);
    object_membership_stamp.assign(numPoints, 0);
#ifdef RA_COLUMN_ORB_EXECUTOR
    object_local_pos.assign(numPoints, 0);
    object_local_pos_stamp.assign(numPoints, 0);
#endif
    for (int i = 0; i < numPoints; i++) {
        for (int d = 0; d < dimension; d++) {
            in >> points[i][d];
            points_flat[(long long)i * dimension + d] = points[i][d];
        }
    }
}

int nextCandidateAliveToken() {
    candidate_alive_token++;
    if (candidate_alive_token == INT_MAX) {
        fill(candidate_alive_stamp.begin(), candidate_alive_stamp.end(), 0);
        candidate_alive_token = 1;
    }
    return candidate_alive_token;
}

int nextShadowSideToken() {
    shadow_side_token++;
    if (shadow_side_token == INT_MAX) {
        fill(shadow_side_stamp.begin(), shadow_side_stamp.end(), 0);
        shadow_side_token = 1;
    }
    return shadow_side_token;
}

int nextObjectMembershipToken() {
    object_membership_token++;
    if (object_membership_token == INT_MAX) {
        fill(object_membership_stamp.begin(), object_membership_stamp.end(), 0);
        object_membership_token = 1;
    }
    return object_membership_token;
}

void loadQueries(const char *path) {
    ifstream in(path);
    if (!in) {
        cerr << "cannot open query file: " << path << endl;
        exit(1);
    }
    in >> numQueries;
    queries.assign(numQueries, vector<float>(dimension));
    radii.assign(numQueries, 0.0f);
    for (int i = 0; i < numQueries; i++) {
        in >> radii[i];
        for (int d = 0; d < dimension; d++)
            in >> queries[i][d];
    }
}

bool childCanBeSkipped(float qdist, float child_min, float child_max, float radius) {
    if (child_max < FLT_MAX && qdist - child_max > radius)
        return true;
    if (child_min - qdist > radius)
        return true;
    return false;
}

bool intervalIntersects(float qdist, float child_min, float child_max, float radius) {
    return !childCanBeSkipped(qdist, child_min, child_max, radius);
}

long long regionObjectCount(Region *region) {
    if (!region)
        return 0;
    return region->subtree_size;
}

int countStablePivots(Region *region) {
    int count = 0;
    if (!region)
        return count;
    for (const auto &pivot : region->pivots)
        if (pivot.pool == 0)
            count++;
    return count;
}

long long countEvidencePairs(Region *region) {
    if (!region)
        return 0;
    long long total = 0;
    for (const auto &pivot : region->pivots)
        total += pivot.memory_pairs;
    total += countEvidencePairs(region->left);
    total += countEvidencePairs(region->right);
    total += countEvidencePairs(region->residual);
    return total;
}

double pivotNet(const PivotEvidence &pivot) {
    return (double)pivot.pruned - (double)pivot.query_dist_cost;
}

double pivotDensity(const PivotEvidence &pivot) {
    return pivotNet(pivot) / (double)max(1LL, pivot.memory_pairs);
}

void ensurePivotLookup(PivotEvidence &pivot) {
    if (pivot.lookup_built)
        return;
    pivot.dist_by_id.clear();
    pivot.dist_by_id.reserve(pivot.dist_sorted.size() * 2 + 1);
    for (const auto &entry : pivot.dist_sorted)
        pivot.dist_by_id[entry.second] = entry.first;
    pivot.lookup_built = true;
}

void ensurePivotSorted(PivotEvidence &pivot) {
    if (pivot.sorted_built)
        return;
    sort(pivot.dist_sorted.begin(), pivot.dist_sorted.end());
    pivot.sorted_built = true;
}

#ifdef RA_COLUMN_ORB_EXECUTOR











static int nextObjectLocalPosToken() {
    object_local_pos_token++;
    if (object_local_pos_token == INT_MAX) {
        fill(object_local_pos_stamp.begin(), object_local_pos_stamp.end(), 0);
        object_local_pos_token = 1;
    }
    return object_local_pos_token;
}

static inline int colWordCount(int M) { return (M + 63) >> 6; }




static int buildRegionLocalPos(Region *region) {
    int token = nextObjectLocalPosToken();
    int M = (int)region->objects.size();
    for (int i = 0; i < M; i++) {
        int id = region->objects[i];
        object_local_pos_stamp[id] = token;
        object_local_pos[id] = i;
    }
    return token;
}







static int ensureRegionLocalPos(Region *region) {
    if (region->local_pos_token != 0
        && region->local_pos_token == g_local_pos_current_token
        && region->local_pos_built_for_version == region->objects_version) {
        return (int)region->local_pos_token;
    }
    int token = nextObjectLocalPosToken();
    int M = (int)region->objects.size();
    for (int i = 0; i < M; i++) {
        int id = region->objects[i];
        object_local_pos_stamp[id] = token;
        object_local_pos[id] = i;
    }
    region->local_pos_token = token;
    region->local_pos_built_for_version = region->objects_version;
    g_local_pos_current_token = token;
    return token;
}




static void initPivotColumnFromCovered(PivotEvidence &pivot, Region *region,
                                       int region_local_token,
                                       const vector<pair<int, float>> &covered) {
    int M = region->col_M;
    int W = colWordCount(M);
    pivot.col_dist.assign(M, -1.0f);
    pivot.col_valid_mask.assign(W, 0ULL);
    pivot.col_region_M = M;
    for (const auto &entry : covered) {
        int id = entry.first;
        if (id < 0 || id >= numPoints) continue;
        if (object_local_pos_stamp[id] != region_local_token) continue;
        int pos = object_local_pos[id];
        if (pos < 0 || pos >= M) continue;
        pivot.col_dist[pos] = entry.second;
        pivot.col_valid_mask[pos >> 6] |= (1ULL << (pos & 63));
    }
}



static void rebuildPivotColumnFromDistById(PivotEvidence &pivot, Region *region,
                                           int region_local_token) {
    int M = region->col_M;
    int W = colWordCount(M);
    pivot.col_dist.assign(M, -1.0f);
    pivot.col_valid_mask.assign(W, 0ULL);
    pivot.col_region_M = M;
    ensurePivotLookup(pivot);
    for (const auto &entry : pivot.dist_by_id) {
        int id = entry.first;
        if (id < 0 || id >= numPoints) continue;
        if (object_local_pos_stamp[id] != region_local_token) continue;
        int pos = object_local_pos[id];
        if (pos < 0 || pos >= M) continue;
        pivot.col_dist[pos] = entry.second;
        pivot.col_valid_mask[pos >> 6] |= (1ULL << (pos & 63));
    }
}



static void rebuildAllPivotColumnsForRegion(Region *region) {
    int token = buildRegionLocalPos(region);
    for (auto &pivot : region->pivots)
        rebuildPivotColumnFromDistById(pivot, region, token);
}






static void appendObjectSlotForAllPivotColumns(Region *region) {
    int new_M = region->col_M;
    int new_W = colWordCount(new_M);
    for (auto &pivot : region->pivots) {

        if (pivot.col_region_M + 1 != new_M)
            continue;
        pivot.col_dist.push_back(-1.0f);
        if ((int)pivot.col_valid_mask.size() < new_W)
            pivot.col_valid_mask.resize(new_W, 0ULL);
        pivot.col_region_M = new_M;
    }
}

#ifdef RA_COLUMN_ORB_DEBUG
static inline void checkColInvariant(Region *region) {
    if (!region) return;
    if (region->col_M != (int)region->objects.size()) {
        cerr << "[col-orb] col_M mismatch: region->col_M=" << region->col_M
             << " objects.size=" << region->objects.size()
             << " region_id=" << region->id << endl;
        abort();
    }




    if (!g_use_column_orb) return;
    for (const auto &pivot : region->pivots) {
        if (pivot.col_region_M != region->col_M) {
            cerr << "[col-orb] pivot col_region_M=" << pivot.col_region_M
                 << " region col_M=" << region->col_M
                 << " region_id=" << region->id << endl;
            abort();
        }
        if ((int)pivot.col_dist.size() != region->col_M) {
            cerr << "[col-orb] pivot col_dist.size=" << pivot.col_dist.size()
                 << " region col_M=" << region->col_M
                 << " region_id=" << region->id << endl;
            abort();
        }
        if ((int)pivot.col_valid_mask.size() != colWordCount(region->col_M)) {
            cerr << "[col-orb] pivot col_valid_mask.size=" << pivot.col_valid_mask.size()
                 << " expected=" << colWordCount(region->col_M)
                 << " region_id=" << region->id << endl;
            abort();
        }
    }
}
#define RA_COL_CHECK(region) checkColInvariant(region)







static void dualTrackVerifyColumnPath(
    Region *region,
    const vector<float> &query,
    float radius,
    const vector<int> &active_pivots,
    int active_limit,
    const vector<pair<long long, long long>> &snapshot,
    long long reference_filter_count_before,
    long long reference_final_alive_count,
    const vector<pair<int, float>> &reference_checked_dists,
    int query_id)
{
    int M = region->col_M;
    if (M != (int)region->objects.size()) {
        cerr << "[consistency] region M=" << M << " != objects.size=" << region->objects.size()
             << " query=" << query_id << " region_id=" << region->id << endl;
        abort();
    }
    int W = colWordCount(M);
    vector<uint64_t> shadow_alive(W, 0ULL);
    if (W > 0) {
        for (int w = 0; w < W; w++) shadow_alive[w] = ~0ULL;
        if (M & 63) shadow_alive[W - 1] = (1ULL << (M & 63)) - 1ULL;
    }
    long long shadow_alive_count = M;
    long long shadow_filter_total = 0;

    for (int k = 0; k < active_limit && shadow_alive_count > 0; k++) {
        int p = active_pivots[k];
        const PivotEvidence &pivot = region->pivots[p];
        long long reference_qdist_delta = pivot.query_dist_cost - snapshot[k].first;
        long long reference_pruned_delta = pivot.pruned - snapshot[k].second;

        bool has_alive = (pivot.memory_pairs > 0 && shadow_alive_count == M);
        if (!has_alive) {
            const uint64_t *vm = pivot.col_valid_mask.data();
            int VW = (int)pivot.col_valid_mask.size();
            int scan_W = W < VW ? W : VW;
            for (int w = 0; w < scan_W; w++) {
                if (shadow_alive[w] & vm[w]) { has_alive = true; break; }
            }
        }
        bool reference_triggered = (reference_qdist_delta > 0);
        if (reference_triggered != has_alive) {
            cerr << "[consistency] qdist trigger mismatch at active_idx=" << k
                 << " p=" << p << " reference=" << reference_triggered << " shadow=" << has_alive
                 << " M=" << M << " shadow_alive=" << shadow_alive_count
                 << " mem_pairs=" << pivot.memory_pairs << " query=" << query_id
                 << " region_id=" << region->id << endl;
            abort();
        }
        if (!has_alive) {
            if (reference_pruned_delta != 0) {
                cerr << "[consistency] non-zero pruned without trigger at active_idx=" << k
                     << " reference_pruned=" << reference_pruned_delta << endl;
                abort();
            }
            continue;
        }

        float qdist = distRaw(query.data(), pivot.center.data());
        float low = qdist - radius;
        float high = qdist + radius;
        long long shadow_pruned = 0;
        if (!(low <= pivot.min_dist && high >= pivot.max_dist)) {
            const float *col = pivot.col_dist.data();
            const uint64_t *vm = pivot.col_valid_mask.data();
            for (int w = 0; w < W; w++) {
                uint64_t alive_w = shadow_alive[w];
                if (!alive_w) continue;
                uint64_t valid_w = (w < (int)pivot.col_valid_mask.size()) ? vm[w] : 0ULL;
                if (!(alive_w & valid_w)) continue;

                uint64_t outside = 0ULL;
                int base = w << 6;
                int limit = (M - base < 64) ? (M - base) : 64;




                for (int b = 0; b < limit; b++) {
                    float d = col[base + b];
                    float diff = d - qdist;
                    if (diff < 0.0f) diff = -diff;
                    outside |= (uint64_t)(diff > radius) << b;
                }
                outside &= valid_w;
                uint64_t kill = outside & alive_w;
                if (kill) {
                    shadow_alive[w] = alive_w & ~kill;
                    shadow_pruned += __builtin_popcountll(kill);
                }
            }
        }
        shadow_alive_count -= shadow_pruned;
        shadow_filter_total += shadow_pruned;
        if (shadow_pruned != reference_pruned_delta) {
            cerr << "[consistency] pruned mismatch at active_idx=" << k
                 << " p=" << p << " reference=" << reference_pruned_delta
                 << " shadow=" << shadow_pruned
                 << " M=" << M << " qdist=" << qdist << " radius=" << radius
                 << " min=" << pivot.min_dist << " max=" << pivot.max_dist
                 << " query=" << query_id << " region_id=" << region->id << endl;
            abort();
        }
    }

    if (shadow_alive_count != reference_final_alive_count) {
        cerr << "[consistency] final alive_count mismatch: reference=" << reference_final_alive_count
             << " shadow=" << shadow_alive_count << " M=" << M
             << " query=" << query_id << " region_id=" << region->id << endl;
        abort();
    }
    if ((long long)reference_checked_dists.size() != reference_final_alive_count) {


        cerr << "[consistency] reference checked_dists.size=" << reference_checked_dists.size()
             << " != alive_count=" << reference_final_alive_count << endl;
        abort();
    }

    int reference_idx = 0;
    for (int w = 0; w < W; w++) {
        uint64_t bits = shadow_alive[w];
        while (bits) {
            int b = __builtin_ctzll(bits);
            bits &= bits - 1;
            int i = (w << 6) + b;
            if (i >= M) break;
            int shadow_id = region->objects[i];
            if (reference_idx >= (int)reference_checked_dists.size()
                || reference_checked_dists[reference_idx].first != shadow_id) {
                cerr << "[consistency] survivor[" << reference_idx << "] mismatch: reference_id="
                     << (reference_idx < (int)reference_checked_dists.size()
                         ? reference_checked_dists[reference_idx].first : -1)
                     << " shadow_id=" << shadow_id
                     << " query=" << query_id << " region_id=" << region->id << endl;
                abort();
            }
            reference_idx++;
        }
    }
    if (reference_idx != (int)reference_checked_dists.size()) {
        cerr << "[consistency] survivor count short: reference=" << reference_checked_dists.size()
             << " shadow_emitted=" << reference_idx << endl;
        abort();
    }
    (void)reference_filter_count_before;
    (void)shadow_filter_total;
}
#else
#define RA_COL_CHECK(region) ((void)0)
#endif

#endif

void erasePivot(Region *region, int index) {
    if (!region || index < 0 || index >= (int)region->pivots.size())
        return;
    for (const auto &entry : region->pivots[index].dist_sorted)
        releaseEvidencePair(region->pivots[index].birth_query, entry.second);
    region->pivots.erase(region->pivots.begin() + index);
}
