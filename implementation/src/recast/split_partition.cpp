double pivotRoi(const PivotEvidence &pivot) {
    return (double)(pivot.pruned + 1) / (double)(pivot.query_dist_cost + 1);
}

bool splitRegion(Region *region, const vector<float> &query,
                 const vector<pair<int, float>> &checked_dists,
                 int query_id, QueryStats &stats) {
    if (!region || !region->isLeaf())
        return false;
    int n = (int)region->objects.size();
    if (n <= LEAF_THRESHOLD * 2 || region->depth >= MAX_DEPTH)
        return false;

    unordered_map<int, float> known;
    known.reserve(checked_dists.size() * 2 + 1);
    for (const auto &entry : checked_dists)
        known[entry.first] = entry.second;

    vector<pair<float, int>> dist_ids;
    dist_ids.reserve(n);
    long long split_extra_before = dc_count;
    for (int id : region->objects) {
        auto it = known.find(id);
        float dist = 0.0f;
        if (it != known.end()) {
            dist = it->second;
        } else {
            dist = distComputePointVector(id, query);
        }
        dist_ids.push_back({dist, id});
    }

    nth_element(dist_ids.begin(), dist_ids.begin() + n / 2, dist_ids.end());
    float threshold = dist_ids[n / 2].first;

    vector<int> left_objs;
    vector<int> right_objs;
    vector<pair<int, float>> left_covered;
    vector<pair<int, float>> right_covered;
    left_objs.reserve(n / 2 + 1);
    right_objs.reserve(n / 2 + 1);
    for (const auto &entry : dist_ids) {
        float dist = entry.first;
        int id = entry.second;
        if (dist <= threshold) {
            left_objs.push_back(id);
            left_covered.push_back({id, dist});
        } else {
            right_objs.push_back(id);
            right_covered.push_back({id, dist});
        }
    }
    if (left_objs.empty() || right_objs.empty())
        return false;

    Region *left = newRegion(left_objs, region->depth + 1);
    Region *right = newRegion(right_objs, region->depth + 1);
    if (LIGHTWEIGHT_SPLIT_CHILDREN) {
        left->lightweight_runtime = true;
        right->lightweight_runtime = true;
    }
    for (const auto &entry : left_covered) {
        if (entry.second < region->left_min) region->left_min = entry.second;
        if (entry.second > region->left_max) region->left_max = entry.second;
    }
    for (const auto &entry : right_covered) {
        if (entry.second < region->right_min) region->right_min = entry.second;
        if (entry.second > region->right_max) region->right_max = entry.second;
    }

    region->split_center = query;
    region->split_center_query_id = query_id;
    region->split_threshold = threshold;
    region->left = left;
    region->right = right;
    region->objects.clear();
    clearPivots(region);
#ifdef RA_COLUMN_ORB_EXECUTOR



    region->col_M = 0;
    region->col_version++;
    region->objects_version++;
#endif




    installPivot(left, query, left_covered, query_id);
    installPivot(right, query, right_covered, query_id);
    return true;
}

bool lazySplitRegion(Region *region, const vector<float> &query,
                     const vector<pair<int, float>> &checked_dists,
                     int query_id, QueryStats &stats,
                     bool use_forced_threshold = false,
                     float forced_threshold = 0.0f) {
    if (!region || !region->isLeaf()) {
        return false;
    }


    int _split_trace_n = (int)region->objects.size();
    int _split_trace_depth = region->depth;
    int _split_trace_region_id = region->id;
    auto emit_split_trace = [&](int reject_reason, int committed) {
        if (RECAST_DIAGNOSTICS < 3 || !g_split_trace_fp) return;
        int assigned = (int)checked_dists.size();
        double est = _split_trace_n > 0 ?
            (double)(_split_trace_n - assigned) / (double)_split_trace_n : 0.0;
        fprintf(g_split_trace_fp,
                "%d\t%d\t%d\t%d\t%d\t%.4f\t%d\t%d\t%d\n",
                query_id, _split_trace_region_id, _split_trace_depth,
                _split_trace_n, assigned, est,
                use_forced_threshold ? 1 : 0, reject_reason, committed);
    };
    int n = _split_trace_n;
    int min_region_size = max(4 * LEAF_THRESHOLD, 512);
    int min_assigned = max(2 * LEAF_THRESHOLD, RECAST_MIN_COVERAGE * 2);
    if (n < min_region_size) {
        emit_split_trace(1, 0);
        return false;
    }
    if ((int)checked_dists.size() < min_assigned) {
        emit_split_trace(2, 0);
        return false;
    }
    if (region->depth >= MAX_DEPTH) {
        emit_split_trace(3, 0);
        return false;
    }



    if (SPLIT_RESIDUAL_GUARD > 0.0) {
        double est = (double)(n - (int)checked_dists.size()) / (double)max(1, n);
        if (est > SPLIT_RESIDUAL_GUARD) {
            g_split_guard_rejects++;
            emit_split_trace(6, 0);
            return false;
        }
    }

    vector<pair<float, int>> dist_ids;
    dist_ids.reserve(checked_dists.size());
    for (const auto &entry : checked_dists)
        dist_ids.push_back({entry.second, entry.first});
    nth_element(dist_ids.begin(), dist_ids.begin() + dist_ids.size() / 2, dist_ids.end());
    float threshold = use_forced_threshold ? forced_threshold : dist_ids[dist_ids.size() / 2].first;

    sort(dist_ids.begin(), dist_ids.end());
    vector<int> left_objs;
    vector<int> right_objs;
    vector<pair<int, float>> left_covered;
    vector<pair<int, float>> right_covered;
    vector<int> assigned_ids;
    assigned_ids.reserve(dist_ids.size());
    for (const auto &entry : dist_ids) {
        float dist = entry.first;
        int id = entry.second;
        assigned_ids.push_back(id);
        if (dist <= threshold) {
            left_objs.push_back(id);
            left_covered.push_back({id, dist});
        } else {
            right_objs.push_back(id);
            right_covered.push_back({id, dist});
        }
    }
    if (left_objs.empty() || right_objs.empty()) {
        emit_split_trace(4, 0);
        return false;
    }

    vector<int> residual;
    residual.reserve(region->objects.size() - assigned_ids.size());
    int assigned_token = nextObjectMembershipToken();
    for (int id : assigned_ids)
        object_membership_stamp[id] = assigned_token;
    for (int id : region->objects) {
        if (object_membership_stamp[id] != assigned_token)
            residual.push_back(id);
    }

    Region *left = newRegion(left_objs, region->depth + 1);
    Region *right = newRegion(right_objs, region->depth + 1);
    if (LIGHTWEIGHT_SPLIT_CHILDREN) {
        left->lightweight_runtime = true;
        right->lightweight_runtime = true;
    }
    region->left_min = FLT_MAX;
    region->left_max = 0.0f;
    region->right_min = FLT_MAX;
    region->right_max = 0.0f;
    for (const auto &entry : left_covered) {
        if (entry.second < region->left_min) region->left_min = entry.second;
        if (entry.second > region->left_max) region->left_max = entry.second;
    }
    for (const auto &entry : right_covered) {
        if (entry.second < region->right_min) region->right_min = entry.second;
        if (entry.second > region->right_max) region->right_max = entry.second;
    }

    if (LIGHTWEIGHT_SPLIT_CHILDREN != 1) {
        inheritStablePivots(region, left, left_objs);
        inheritStablePivots(region, right, right_objs);
    }
    if (!RECAST_KEEP_PARENT_EVIDENCE)
        removeAssignedDistancesFromParent(region, assigned_ids);
    region->split_center = query;
    region->split_center_query_id = query_id;
    region->split_threshold = threshold;
    region->left = left;
    region->right = right;
    if (RECAST_RESIDUAL_CHILD && !residual.empty()) {
        Region *residual_child = newRegion(residual, region->depth + 1);
        residual_child->is_residual_region = true;
        if (LIGHTWEIGHT_SPLIT_CHILDREN == 1)
            residual_child->lightweight_runtime = true;
        else
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

    installPivot(left, query, left_covered, query_id, 1);
    installPivot(right, query, right_covered, query_id, 1);
    emit_split_trace(0, 1);
    return true;
}

void collectSubtreeObjects(Region *region, vector<int> &out) {
    if (!region)
        return;
    out.insert(out.end(), region->objects.begin(), region->objects.end());
    collectSubtreeObjects(region->left, out);
    collectSubtreeObjects(region->right, out);
    collectSubtreeObjects(region->residual, out);
}

bool mergeBadSplit(Region *region, QueryStats &stats) {
    if (!region || region->isLeaf() || POLICY_MODE < 2)
        return false;
    if (region->split_visits < RECAST_SPLIT_PROBATION)
        return false;
    double net = (double)region->split_skipped_objects
                 - (double)region->split_routing_dc
                 - (double)region->split_residual_fill_extra_dc
                 - 1.0;
    if (net >= -64.0)
        return false;

    collectSubtreeObjects(region->left, region->objects);
    collectSubtreeObjects(region->right, region->objects);
    collectSubtreeObjects(region->residual, region->objects);
    deleteRegion(region->left);
    deleteRegion(region->right);
    deleteRegion(region->residual);
    region->left = nullptr;
    region->right = nullptr;
    region->residual = nullptr;
    region->split_center.clear();
    region->split_center_query_id = -1;
    region->split_threshold = 0.0f;
    region->last_routing_query_id = -1;
    region->last_routing_qdist = 0.0f;
    region->left_min = FLT_MAX;
    region->left_max = 0.0f;
    region->right_min = FLT_MAX;
    region->right_max = 0.0f;
    region->residual_fp_counts.clear();
#ifdef RA_COLUMN_ORB_EXECUTOR

    region->col_M = (int)region->objects.size();
    region->col_version++;
    region->objects_version++;
    if (g_use_column_orb)
        rebuildAllPivotColumnsForRegion(region);
#endif
    return true;
}

void promoteResidualObjects(Region *region, const vector<float> &query, float radius,
                            int query_id, QueryStats &stats) {
    bool time_residual = timeAwareResidualEnabled();
    if ((!RECAST_RESIDUAL_PROMOTION && !time_residual) || !region || region->isLeaf() ||
        !region->left || !region->right || region->objects.empty() ||
        region->split_center.empty() || region->split_center_query_id < 0)
        return;

    double split_net = (double)region->split_skipped_objects
                       - (double)region->split_routing_dc
                       - (double)region->split_residual_fill_extra_dc;
    if (!time_residual && split_net <= RECAST_PROMOTION_START_MARGIN)
        return;
    if (time_residual && region->split_visits < 4)
        return;

    float qdist = 0.0f;
    if (region->last_routing_query_id == query_id) {
        qdist = region->last_routing_qdist;
    } else {
        qdist = distCompute(query, region->split_center);
        region->split_residual_fill_extra_dc++;
    }
    bool left_skipped_now = childCanBeSkipped(qdist, region->left_min, region->left_max, radius);
    bool right_skipped_now = childCanBeSkipped(qdist, region->right_min, region->right_max, radius);
    if (!time_residual && !left_skipped_now && !right_skipped_now)
        return;

    vector<pair<int, int>> candidates;
    candidates.reserve(region->residual_fp_counts.size());
    for (const auto &entry : region->residual_fp_counts) {
        int min_count = time_residual ? max(2, RECAST_PROMOTION_MIN_FP_COUNT / 2)
                                      : RECAST_PROMOTION_MIN_FP_COUNT;
        if (entry.second >= min_count)
            candidates.push_back({entry.second, entry.first});
    }
    if (candidates.empty())
        return;
    sort(candidates.begin(), candidates.end(), greater<pair<int, int>>());

    int budget = time_residual ? max(32, RECAST_PROMOTION_MAX_PER_QUERY)
                               : RECAST_PROMOTION_MAX_PER_QUERY;
    if (stats.checked > 0)
        budget = min(budget, max(1, (int)floor((double)stats.checked * (time_residual ? 0.10 : 0.05))));
    if (time_residual)
        budget = min(budget, 256);
    if (budget <= 0)
        return;

    unordered_map<int, char> promoted;
    promoted.reserve(budget * 2 + 1);
    int promoted_count = 0;
    int attempted_count = 0;
    for (const auto &candidate : candidates) {
        if (attempted_count >= budget)
            break;
        int id = candidate.second;
        if (promoted.find(id) != promoted.end())
            continue;
        auto exists = find(region->objects.begin(), region->objects.end(), id);
        if (exists == region->objects.end())
            continue;

        float dist = distComputePointVector(id, region->split_center);
        attempted_count++;
        region->split_residual_fill_extra_dc++;

        bool goes_left = dist <= region->split_threshold;
        if (!time_residual &&
            ((goes_left && !left_skipped_now) || (!goes_left && !right_skipped_now)))
            continue;
        float new_min = goes_left ? min(region->left_min, dist) : min(region->right_min, dist);
        float new_max = goes_left ? max(region->left_max, dist) : max(region->right_max, dist);
        if (!time_residual && !childCanBeSkipped(qdist, new_min, new_max, radius))
            continue;

        Region *child = nullptr;
        if (goes_left) {
            child = region->left;
            region->left_min = min(region->left_min, dist);
            region->left_max = max(region->left_max, dist);
        } else {
            child = region->right;
            region->right_min = min(region->right_min, dist);
            region->right_max = max(region->right_max, dist);
        }
        if (child) {
            child->objects.push_back(id);
            child->subtree_size++;
#ifdef RA_COLUMN_ORB_EXECUTOR
            child->col_M = (int)child->objects.size();
            child->objects_version++;
            if (g_use_column_orb)
                appendObjectSlotForAllPivotColumns(child);
#endif
            addDistanceToExistingPivot(child, region->split_center_query_id, id, dist);
        }
        promoted[id] = 1;
        promoted_count++;
        region->residual_fp_counts.erase(id);
    }

    if (promoted_count == 0)
        return;

    vector<int> residual;
    residual.reserve(region->objects.size() - promoted_count);
    for (int id : region->objects) {
        if (promoted.find(id) == promoted.end())
            residual.push_back(id);
    }
    region->objects.swap(residual);
#ifdef RA_COLUMN_ORB_EXECUTOR
    region->col_M = (int)region->objects.size();
    region->objects_version++;





    if (g_use_column_orb)
        rebuildAllPivotColumnsForRegion(region);
#endif
}

