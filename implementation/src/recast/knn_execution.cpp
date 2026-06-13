struct KnnHit {
    float dist = 0.0f;
    int id = -1;
};

struct KnnHitWorse {
    bool operator()(const KnnHit &left, const KnnHit &right) const {
        if (left.dist != right.dist)
            return left.dist < right.dist;
        return left.id < right.id;
    }
};

struct KnnRegionTask {
    float lb = 0.0f;
    int order = 0;
    Region *region = nullptr;
};

struct KnnRegionTaskGreater {
    bool operator()(const KnnRegionTask &left, const KnnRegionTask &right) const {
        if (left.lb != right.lb)
            return left.lb > right.lb;
        return left.order > right.order;
    }
};

static inline float currentKnnRadius(const priority_queue<KnnHit, vector<KnnHit>, KnnHitWorse> &topk,
                                     int k) {
    if ((int)topk.size() < k)
        return FLT_MAX;
    return topk.top().dist;
}

static inline void addKnnHit(priority_queue<KnnHit, vector<KnnHit>, KnnHitWorse> &topk,
                             int k, int id, float dist) {
    if (k <= 0)
        return;
    if ((int)topk.size() < k) {
        topk.push({dist, id});
        return;
    }
    const KnnHit &worst = topk.top();
    if (dist < worst.dist || (dist == worst.dist && id < worst.id)) {
        topk.pop();
        topk.push({dist, id});
    }
}

static inline int nextKnnSeenToken() {
    knn_seen_token++;
    if (knn_seen_token == INT_MAX) {
        fill(knn_seen_stamp.begin(), knn_seen_stamp.end(), 0);
        knn_seen_token = 1;
    }
    return knn_seen_token;
}

static inline bool addKnnHitOnce(priority_queue<KnnHit, vector<KnnHit>, KnnHitWorse> &topk,
                                 int k, int id, float dist, int seen_token) {
    if (knn_seen_stamp[id] == seen_token)
        return false;
    knn_seen_stamp[id] = seen_token;
    addKnnHit(topk, k, id, dist);
    return true;
}

static inline float intervalLowerBound(float qdist, float child_min, float child_max) {
    if (qdist < child_min)
        return child_min - qdist;
    if (qdist > child_max)
        return qdist - child_max;
    return 0.0f;
}

static inline float distComputePointPtrSquaredL2(int point_id, const float *__restrict__ right) {
    dc_count++;
    const float *left = &points_flat[(long long)point_id * dimension];
    float total = 0.0f;
    for (int i = 0; i < dimension; i++) {
        float diff = left[i] - right[i];
        total += diff * diff;
    }
    return total;
}

static inline float distComputeSquaredL2(const vector<float> &left, const vector<float> &right) {
    dc_count++;
    float total = 0.0f;
    for (int i = 0; i < dimension; i++) {
        float diff = left[i] - right[i];
        total += diff * diff;
    }
    return total;
}

static void selectActivePivotsForKnn(Region *region, vector<int> &active_pivots) {
    active_pivots.clear();
    if (!region)
        return;
    updatePivotPools(region);
    int active_limit = 0;
    if (POLICY_MODE >= 2) {
        vector<int> stable;
        vector<int> nursery;
        stable.reserve(region->pivots.size());
        nursery.reserve(region->pivots.size());
        for (int p = 0; p < (int)region->pivots.size(); p++) {
            if ((PIVOT_POOL_POLICY == 0 || PIVOT_POOL_POLICY == 5) && region->pivots[p].pool == 1)
                nursery.push_back(p);
            else
                stable.push_back(p);
        }
        auto by_score = [&](int left, int right) {
            double left_score = pivotNet(region->pivots[left]);
            double right_score = pivotNet(region->pivots[right]);
            if (left_score != right_score)
                return left_score > right_score;
            if (region->pivots[left].memory_pairs != region->pivots[right].memory_pairs)
                return region->pivots[left].memory_pairs > region->pivots[right].memory_pairs;
            return left < right;
        };
        sort(stable.begin(), stable.end(), by_score);
        sort(nursery.begin(), nursery.end(), by_score);
        int rapid_limit = min(QUERY_ACTIVE_PIVOTS, RECAST_REGION_ACTIVE);
        int nursery_budget = min(RECAST_NURSERY_EXPLORE, rapid_limit);
        int stable_budget = max(0, rapid_limit - nursery_budget);
        for (int p : stable) {
            if ((int)active_pivots.size() >= stable_budget)
                break;
            if (pivotNet(region->pivots[p]) > 0.0)
                active_pivots.push_back(p);
        }
        for (int p : nursery) {
            if ((int)active_pivots.size() >= rapid_limit)
                break;
            active_pivots.push_back(p);
        }
        for (int p : stable) {
            if ((int)active_pivots.size() >= rapid_limit)
                break;
            if (find(active_pivots.begin(), active_pivots.end(), p) == active_pivots.end())
                active_pivots.push_back(p);
        }
        active_limit = (int)active_pivots.size();
    } else {
        active_pivots.resize(region->pivots.size());
        iota(active_pivots.begin(), active_pivots.end(), 0);
        active_limit = min((int)region->pivots.size(), QUERY_ACTIVE_PIVOTS);
        if (POLICY_MODE == 1 && active_limit < (int)active_pivots.size()) {
            stable_sort(active_pivots.begin(), active_pivots.end(), [&](int left, int right) {
                double left_score = pivotRoi(region->pivots[left]);
                double right_score = pivotRoi(region->pivots[right]);
                if (left_score != right_score)
                    return left_score > right_score;
                return region->pivots[left].memory_pairs > region->pivots[right].memory_pairs;
            });
        }
    }
    active_pivots.resize(active_limit);
    if (ADAPTIVE_BUDGET_K > 0
        && (int)region->objects.size() > ADAPTIVE_BUDGET_THRESHOLD_M
        && (int)active_pivots.size() > ADAPTIVE_BUDGET_K) {
        active_pivots.resize(ADAPTIVE_BUDGET_K);
    }
}

static bool knnDirectSplitRegion(Region *region, const vector<float> &query,
                                 const vector<pair<int, float>> &checked_dists,
                                 int query_id) {
    if (!region || !region->isLeaf())
        return false;
    int n = (int)region->objects.size();
    if (n <= LEAF_THRESHOLD || region->depth >= MAX_DEPTH)
        return false;
    if ((int)checked_dists.size() < max(2 * LEAF_THRESHOLD, RECAST_MIN_COVERAGE * 2))
        return false;

    vector<pair<float, int>> dist_ids;
    dist_ids.reserve(checked_dists.size());
    for (const auto &entry : checked_dists)
        dist_ids.push_back({entry.second, entry.first});
    float threshold = 0.0f;
    if (KNN_SAMPLE_SPLIT && dist_ids.size() >= 3) {
        size_t n0 = dist_ids.size();
        size_t a = ((long long)query_id * 1103515245LL + 12345LL) % n0;
        size_t b = ((long long)query_id * 2654435761LL + 1013904223LL) % n0;
        size_t c = ((long long)query_id * 2246822519LL + 3266489917LL) % n0;
        float x = dist_ids[a].first, y = dist_ids[b].first, z = dist_ids[c].first;
        threshold = max(min(x, y), min(max(x, y), z));
    } else {
        nth_element(dist_ids.begin(), dist_ids.begin() + dist_ids.size() / 2, dist_ids.end());
        threshold = dist_ids[dist_ids.size() / 2].first;
    }

    vector<int> left_objs;
    vector<int> right_objs;
    vector<pair<int, float>> left_covered;
    vector<pair<int, float>> right_covered;
    vector<int> assigned_ids;
    left_objs.reserve(dist_ids.size() / 2 + 1);
    right_objs.reserve(dist_ids.size() / 2 + 1);
    left_covered.reserve(dist_ids.size() / 2 + 1);
    right_covered.reserve(dist_ids.size() / 2 + 1);
    assigned_ids.reserve(dist_ids.size());

    float left_min = FLT_MAX, left_max = 0.0f;
    float right_min = FLT_MAX, right_max = 0.0f;
    for (const auto &entry : dist_ids) {
        float dist = entry.first;
        int id = entry.second;
        assigned_ids.push_back(id);
        if (dist <= threshold) {
            left_objs.push_back(id);
            left_covered.push_back({id, dist});
            if (dist < left_min) left_min = dist;
            if (dist > left_max) left_max = dist;
        } else {
            right_objs.push_back(id);
            right_covered.push_back({id, dist});
            if (dist < right_min) right_min = dist;
            if (dist > right_max) right_max = dist;
        }
    }
    if (left_objs.empty() || right_objs.empty())
        return false;

    vector<int> residual;
    residual.reserve(max(0, n - (int)assigned_ids.size()));
    int assigned_token = nextObjectMembershipToken();
    for (int id : assigned_ids)
        object_membership_stamp[id] = assigned_token;
    for (int id : region->objects) {
        if (object_membership_stamp[id] != assigned_token)
            residual.push_back(id);
    }

    Region *left = newRegion(left_objs, region->depth + 1);
    Region *right = newRegion(right_objs, region->depth + 1);
    if (REGION_PIVOT_MAX > 0) {
        inheritStablePivots(region, left, left_objs);
        inheritStablePivots(region, right, right_objs);
    }
    region->split_center = query;
    region->split_center_query_id = query_id;
    region->split_threshold = threshold;
    region->left_min = left_min;
    region->left_max = left_max;
    region->right_min = right_min;
    region->right_max = right_max;
    region->left = left;
    region->right = right;
    if (RECAST_RESIDUAL_CHILD && !residual.empty()) {
        Region *residual_child = newRegion(residual, region->depth + 1);
        residual_child->is_residual_region = true;
        if (REGION_PIVOT_MAX > 0)
            inheritPivotsForObjects(region, residual_child, residual);
        region->residual = residual_child;
        region->objects.clear();
    } else {
        region->objects.swap(residual);
    }
#ifdef RA_COLUMN_ORB_EXECUTOR
    region->col_M = (int)region->objects.size();
    region->col_version++;
    region->objects_version++;
    if (g_use_column_orb)
        rebuildAllPivotColumnsForRegion(region);
#endif
    region->split_birth_visit = region->visits;
    region->split_visits = 0;
    region->split_routing_dc = 0;
    region->split_skipped_objects = 0;
    region->split_residual_fill_extra_dc = 0;
    if (REGION_PIVOT_MAX > 0) {
        installPivot(left, query, left_covered, query_id, 1);
        installPivot(right, query, right_covered, query_id, 1);
    }
    return true;
}

static void processKnnBucket(Region *region, const vector<float> &query, int k,
                             int query_id,
                             int seen_token,
                             priority_queue<KnnHit, vector<KnnHit>, KnnHitWorse> &topk,
                             QueryStats &stats) {
    if (!region || region->objects.empty())
        return;
    leaf_points += region->objects.size();
    vector<int> active_pivots;
    selectActivePivotsForKnn(region, active_pivots);
    int active_limit = (int)active_pivots.size();

    long long local_checked = 0;
    long long local_fp = 0;
    long long local_answers = 0;
    long long local_pivot_pruned = 0;
    vector<pair<int, float>> &checked_dists = checked_dists_workspace;
    checked_dists.clear();

    int alive_token = nextCandidateAliveToken();
    auto candidateAlive = [&](int id) {
        return candidate_alive_stamp[id] != alive_token;
    };
    auto markCandidateDead = [&](int id) {
        candidate_alive_stamp[id] = alive_token;
    };

    long long alive_count = (long long)region->objects.size();
    if (KNN_SEED_CACHE_SIZE > 0 && !region->knn_seed_ids.empty()) {
        for (int id : region->knn_seed_ids) {
            if (!candidateAlive(id))
                continue;
            if (knn_seen_stamp[id] == seen_token) {
                markCandidateDead(id);
                alive_count--;
                continue;
            }
            float dist = distComputePointPtr(id, query.data());
            checked_dists.push_back({id, dist});
            addKnnHitOnce(topk, k, id, dist, seen_token);
            markCandidateDead(id);
            alive_count--;
            local_checked++;
        }
    }
    for (int id : region->objects) {
        if ((int)topk.size() >= k)
            break;
        if (!candidateAlive(id))
            continue;
        if (knn_seen_stamp[id] == seen_token) {
            markCandidateDead(id);
            alive_count--;
            continue;
        }
        float dist = distComputePointPtr(id, query.data());
        checked_dists.push_back({id, dist});
        addKnnHitOnce(topk, k, id, dist, seen_token);
        markCandidateDead(id);
        alive_count--;
        local_checked++;
    }
    float radius = currentKnnRadius(topk, k);
    if (radius < FLT_MAX) {
        for (int idx = 0; idx < active_limit && alive_count > 0; idx++) {
            int p = active_pivots[idx];
            PivotEvidence &pivot = region->pivots[p];
            long long pivot_M_snap = pivot.memory_pairs;
            bool has_alive_covered = pivot_M_snap > 0 && alive_count == (long long)region->objects.size();
            if (!has_alive_covered) {
                for (const auto &entry : pivot.dist_sorted) {
                    if (candidateAlive(entry.second)) {
                        has_alive_covered = true;
                        break;
                    }
                }
            }
            if (!has_alive_covered)
                continue;
            float qdist = distCompute(query, pivot.center);
            pivot.query_dist_cost++;
            pivot.last_used_region_visit = region->visits;
            float low = qdist - radius;
            float high = qdist + radius;
            if (low <= pivot.min_dist && high >= pivot.max_dist)
                continue;
            ensurePivotSorted(pivot);
            auto low_end = lower_bound(pivot.dist_sorted.begin(), pivot.dist_sorted.end(),
                                       make_pair(low, INT_MIN));
            auto high_begin = upper_bound(pivot.dist_sorted.begin(), pivot.dist_sorted.end(),
                                          make_pair(high, INT_MAX));
            long long pruned_now = 0;
            auto prune_range = [&](vector<pair<float, int>>::const_iterator begin,
                                   vector<pair<float, int>>::const_iterator end) {
                for (auto it = begin; it != end; ++it) {
                    int id = it->second;
                    if (!candidateAlive(id))
                        continue;
                    float diff = it->first - qdist;
                    if (diff < 0.0f) diff = -diff;
                    if (diff <= radius)
                        continue;
                    markCandidateDead(id);
                    pruned_now++;
                }
            };
            prune_range(pivot.dist_sorted.begin(), low_end);
            prune_range(high_begin, pivot.dist_sorted.end());
            alive_count -= pruned_now;
            if (pruned_now > 0) {
                filter_count += pruned_now;
                pivot.pruned += pruned_now;
                local_pivot_pruned += pruned_now;
            }
        }
    }

    const float *query_ptr = query.data();
    for (int id : region->objects) {
        if (!candidateAlive(id))
            continue;
        if (knn_seen_stamp[id] == seen_token)
            continue;
        float dist = distComputePointPtr(id, query_ptr);
        checked_dists.push_back({id, dist});
        addKnnHitOnce(topk, k, id, dist, seen_token);
        local_checked++;
    }

    radius = currentKnnRadius(topk, k);
    if (radius < FLT_MAX) {
        for (const auto &entry : checked_dists) {
            if (entry.second <= radius)
                local_answers++;
            else
                local_fp++;
        }
        stats.checked += local_checked;
        bool had_ema = region->ema_ready;
        double old_checked_ema = region->checked_ema;
        updateRegionStats(region, local_checked, local_fp, local_answers);
        if (KNN_SEED_CACHE_SIZE > 0 && !checked_dists.empty() && region->isLeaf()) {
            vector<pair<float, int>> nearest;
            nearest.reserve(checked_dists.size());
            for (const auto &entry : checked_dists)
                nearest.push_back({entry.second, entry.first});
            int take = min(KNN_SEED_CACHE_SIZE, (int)nearest.size());
            nth_element(nearest.begin(), nearest.begin() + take - 1, nearest.end());
            sort(nearest.begin(), nearest.begin() + take);
            region->knn_seed_ids.clear();
            region->knn_seed_ids.reserve(take);
            for (int i = 0; i < take; i++)
                region->knn_seed_ids.push_back(nearest[i].second);
        }
        if (KNN_DIRECT_CRACK
            && region->isLeaf()
            && (int)region->objects.size() > LEAF_THRESHOLD
            && (int)checked_dists.size() >= max(2 * LEAF_THRESHOLD, RECAST_MIN_COVERAGE * 2)
            && region->depth < MAX_DEPTH
            && knnDirectSplitRegion(region, query, checked_dists, query_id)) {
            return;
        }
        maybeGrowRegion(region, query, radius, checked_dists, local_checked, local_fp,
                        local_answers, local_pivot_pruned, old_checked_ema, had_ema,
                        query_id, stats);
        if (POLICY_MODE == 3 && region->isLeaf() &&
            (RECAST_SHADOW_FROM_PIVOTS || RECAST_WORKLOAD_THRESHOLD))
            rememberRecentObservation(region, query, radius, checked_dists);
        if (POLICY_MODE >= 2)
            mergeBadSplit(region, stats);
    } else {
        stats.checked += local_checked;
        bool had_ema = region->ema_ready;
        double old_checked_ema = region->checked_ema;
        updateRegionStats(region, local_checked, local_checked, 0);
        maybeGrowRegion(region, query, FLT_MAX, checked_dists, local_checked, local_checked,
                        0, local_pivot_pruned, old_checked_ema, had_ema,
                        query_id, stats);
    }
}

static void knnQuery(Region *root, const vector<float> &query, int k, int query_id,
                     QueryStats &stats,
                     priority_queue<KnnHit, vector<KnnHit>, KnnHitWorse> &topk) {
    int seen_token = nextKnnSeenToken();
    priority_queue<KnnRegionTask, vector<KnnRegionTask>, KnnRegionTaskGreater> queue;
    int order = 0;
    queue.push({0.0f, order++, root});
    while (!queue.empty()) {
        KnnRegionTask task = queue.top();
        queue.pop();
        float radius = currentKnnRadius(topk, k);
        if (radius < FLT_MAX && task.lb > radius)
            continue;
        Region *region = task.region;
        if (!region)
            continue;
        if (POLICY_MODE == 3 && radius < FLT_MAX)
            updateShadowSplit(region, query, radius, query_id, stats);
        if (KNN_SEED_CACHE_SIZE > 0 && !region->knn_seed_ids.empty()) {
            for (int id : region->knn_seed_ids) {
                if (knn_seen_stamp[id] == seen_token)
                    continue;
                float dist = distComputePointPtr(id, query.data());
                addKnnHitOnce(topk, k, id, dist, seen_token);
            }
            radius = currentKnnRadius(topk, k);
            if (radius < FLT_MAX && task.lb > radius)
                continue;
        }
        if (!region->isLeaf()) {
            float qdist = distCompute(query, region->split_center);
            if (POLICY_MODE >= 2) {
                region->split_visits++;
                region->split_routing_dc++;
                region->last_routing_query_id = query_id;
                region->last_routing_qdist = qdist;
            }
            radius = currentKnnRadius(topk, k);
            if (region->left) {
                float lb = intervalLowerBound(qdist, region->left_min, region->left_max);
                if (radius == FLT_MAX || lb <= radius)
                    queue.push({lb, order++, region->left});
                else if (POLICY_MODE >= 2)
                    region->split_skipped_objects += regionObjectCount(region->left);
            }
            if (region->right) {
                float lb = intervalLowerBound(qdist, region->right_min, region->right_max);
                if (radius == FLT_MAX || lb <= radius)
                    queue.push({lb, order++, region->right});
                else if (POLICY_MODE >= 2)
                    region->split_skipped_objects += regionObjectCount(region->right);
            }
            if (RECAST_RESIDUAL_CHILD && region->residual)
                queue.push({0.0f, order++, region->residual});
            if (POLICY_MODE < 2 || region->objects.empty())
                continue;
        }
        processKnnBucket(region, query, k, query_id, seen_token, topk, stats);
    }
    stats.result = (int)topk.size();
}

struct KnnLiteNode {
    int start = 0;
    int end = -1;
    int depth = 0;
    vector<float> center;
    float epsilon = 0.0f;
    KnnLiteNode *left = nullptr;
    KnnLiteNode *right = nullptr;
    bool isLeaf() const { return left == nullptr && right == nullptr; }
};

static KnnLiteNode *newKnnLiteNode(int start, int end, int depth) {
    KnnLiteNode *node = new KnnLiteNode();
    node->start = start;
    node->end = end;
    node->depth = depth;
    return node;
}

static void deleteKnnLiteNode(KnnLiteNode *node) {
    if (!node) return;
    deleteKnnLiteNode(node->left);
    deleteKnnLiteNode(node->right);
    delete node;
}

struct KnnLiteTask {
    float lb = 0.0f;
    int order = 0;
    KnnLiteNode *node = nullptr;
};

struct KnnLiteTaskGreater {
    bool operator()(const KnnLiteTask &left, const KnnLiteTask &right) const {
        if (left.lb != right.lb)
            return left.lb > right.lb;
        return left.order > right.order;
    }
};

static int crackLiteRange(vector<int> &ids, vector<float> &dists,
                          int start, int end, float epsilon) {
    int i = start, j = end;
    while (true) {
        while (i <= end && dists[i] <= epsilon) i++;
        while (j >= start && dists[j] > epsilon) j--;
        if (i >= j) break;
        swap(ids[i], ids[j]);
        swap(dists[i], dists[j]);
    }
    return j;
}

static void knnLiteQuery(KnnLiteNode *root, vector<int> &ids, vector<float> &dists,
                         const vector<float> &query, int k,
                         priority_queue<KnnHit, vector<KnnHit>, KnnHitWorse> &topk) {
    priority_queue<KnnLiteTask, vector<KnnLiteTask>, KnnLiteTaskGreater> queue;
    int order = 0;
    queue.push({0.0f, order++, root});
    const float *query_ptr = query.data();
    while (!queue.empty()) {
        KnnLiteTask task = queue.top();
        queue.pop();
        float radius = currentKnnRadius(topk, k);
        if (radius < FLT_MAX && task.lb > radius)
            continue;
        KnnLiteNode *node = task.node;
        if (!node) continue;
        if (node->isLeaf()) {
            for (int pos = node->start; pos <= node->end; pos++) {
                int id = ids[pos];
                float dist = distComputePointPtrSquaredL2(id, query_ptr);
                dists[pos] = dist;
                addKnnHit(topk, k, id, dist);
            }
            int len = node->end - node->start + 1;
            if (len > LEAF_THRESHOLD && node->depth < MAX_DEPTH) {
                int a = node->start + (int)(((long long)len * 1103515245LL + 12345LL) % len);
                int b = node->start + (int)(((long long)len * 2654435761LL + 1013904223LL) % len);
                int c = node->start + (int)(((long long)len * 2246822519LL + 3266489917LL) % len);
                float x = dists[a], y = dists[b], z = dists[c];
                float epsilon = max(min(x, y), min(max(x, y), z));
                int crack = crackLiteRange(ids, dists, node->start, node->end, epsilon);
                if (crack >= node->start && crack + 1 <= node->end) {
                    node->center = query;
                    node->epsilon = sqrt(epsilon);
                    node->left = newKnnLiteNode(node->start, crack, node->depth + 1);
                    node->right = newKnnLiteNode(crack + 1, node->end, node->depth + 1);
                }
            }
        } else {
            float dist = sqrt(distComputeSquaredL2(query, node->center));
            float left_lb = max(0.0f, dist - node->epsilon);
            float right_lb = max(0.0f, node->epsilon - dist);
            left_lb *= left_lb;
            right_lb *= right_lb;
            radius = currentKnnRadius(topk, k);
            if (radius == FLT_MAX || left_lb <= radius)
                queue.push({left_lb, order++, node->left});
            if (radius == FLT_MAX || right_lb <= radius)
                queue.push({right_lb, order++, node->right});
        }
    }
}
