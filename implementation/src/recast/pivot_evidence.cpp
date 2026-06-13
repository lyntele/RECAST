void removeAssignedDistancesFromParent(Region *region, const vector<int> &assigned_ids) {
    if (!region || assigned_ids.empty())
        return;
    for (auto &pivot : region->pivots) {
        ensurePivotLookup(pivot);
        bool changed = false;
        for (int id : assigned_ids) {
            auto it = pivot.dist_by_id.find(id);
            if (it != pivot.dist_by_id.end()) {
                pivot.dist_by_id.erase(it);
                pivot.memory_pairs--;
                releaseEvidencePair(pivot.birth_query, id);
                changed = true;
            }
        }
        if (changed) {
            pivot.dist_sorted.clear();
            pivot.dist_sorted.reserve(pivot.dist_by_id.size());
            pivot.min_dist = FLT_MAX;
            pivot.max_dist = 0.0f;
            for (const auto &entry : pivot.dist_by_id) {
                pivot.dist_sorted.push_back({entry.second, entry.first});
                if (entry.second < pivot.min_dist) pivot.min_dist = entry.second;
                if (entry.second > pivot.max_dist) pivot.max_dist = entry.second;
            }
            pivot.sorted_built = false;
            pivot.lookup_built = true;
        }
    }
}

void clearPivots(Region *region) {
    if (!region)
        return;
    for (const auto &pivot : region->pivots) {
        for (const auto &entry : pivot.dist_sorted)
            releaseEvidencePair(pivot.birth_query, entry.second);
    }
    region->pivots.clear();
}

void installPivot(Region *region, const vector<float> &query,
                  const vector<pair<int, float>> &covered, int query_id,
                  int pool);

void inheritStablePivots(Region *parent, Region *child, const vector<int> &object_ids) {
    if (!parent || !child || POLICY_MODE != 3 || object_ids.empty())
        return;
    int token = nextObjectMembershipToken();
    for (int id : object_ids)
        object_membership_stamp[id] = token;
    for (auto &pivot : parent->pivots) {
        if ((PIVOT_POOL_POLICY == 0 || PIVOT_POOL_POLICY == 5) && pivot.pool != 0)
            continue;
        vector<pair<int, float>> covered;
        covered.reserve(min<long long>((long long)object_ids.size(), pivot.memory_pairs));
        for (const auto &entry : pivot.dist_sorted) {
            int id = entry.second;
            if (object_membership_stamp[id] == token)
                covered.push_back({id, entry.first});
        }
        if ((int)covered.size() >= RECAST_MIN_COVERAGE)
            installPivot(child, pivot.center, covered, pivot.birth_query, 0);
    }
}

void inheritPivotsForObjects(Region *parent, Region *child, const vector<int> &object_ids) {
    if (!parent || !child || POLICY_MODE != 3 || object_ids.empty())
        return;
    int token = nextObjectMembershipToken();
    for (int id : object_ids)
        object_membership_stamp[id] = token;
    for (auto &pivot : parent->pivots) {
        vector<pair<int, float>> covered;
        covered.reserve(min<long long>((long long)object_ids.size(), pivot.memory_pairs));
        for (const auto &entry : pivot.dist_sorted) {
            int id = entry.second;
            if (object_membership_stamp[id] == token)
                covered.push_back({id, entry.first});
        }
        if ((int)covered.size() >= RECAST_MIN_COVERAGE)
            installPivot(child, pivot.center, covered, pivot.birth_query, pivot.pool);
    }
}

void installPivot(Region *region, const vector<float> &query,
                  const vector<pair<int, float>> &covered, int query_id,
                  int pool = 0) {
    if (!region)
        return;
    if (POLICY_MODE < 2 && !region->isLeaf())
        return;
    if ((int)region->pivots.size() >= REGION_PIVOT_MAX) {
        if (PIVOT_POOL_POLICY == 1)
            return;
        if (POLICY_MODE < 2)
            return;
        enforceRegionPivotBudget(region);
        if ((int)region->pivots.size() >= REGION_PIVOT_MAX) {
            int idx = -1;
            if (PIVOT_POOL_POLICY == 0)
                idx = findWeakPivot(region, false);
            else if (PIVOT_POOL_POLICY == 5 || PIVOT_POOL_POLICY == 6)
                idx = findAdaptiveEvictionPivot(region, false);
            else
                idx = findSingleTierEvictionPivot(region);
            if (idx >= 0)
                erasePivot(region, idx);
        }
    }
    if ((int)region->pivots.size() >= REGION_PIVOT_MAX)
        return;
    if (covered.empty())
        return;







    const vector<pair<int, float>> *covered_use_ptr = &covered;
    vector<pair<int, float>> capped_storage;
    if (PIVOT_COVERAGE_CAP > 0
        && (int)covered.size() > PIVOT_COVERAGE_MIN_TO_CAP
        && (int)covered.size() > PIVOT_COVERAGE_CAP) {
        capped_storage = covered;
        sort(capped_storage.begin(), capped_storage.end(),
             [](const pair<int, float> &a, const pair<int, float> &b) {
                 return a.second < b.second;
             });
        int N = (int)capped_storage.size();
        int K = PIVOT_COVERAGE_CAP;
        vector<pair<int, float>> sampled;
        sampled.reserve(K);

        for (int i = 0; i < K; i++) {
            long long pos = (long long)i * (long long)N / (long long)K;
            if (pos < 0) pos = 0;
            if (pos >= N) pos = N - 1;
            sampled.push_back(capped_storage[(size_t)pos]);
        }
        capped_storage.swap(sampled);
        covered_use_ptr = &capped_storage;
    }
    const vector<pair<int, float>> &covered_use = *covered_use_ptr;

    PivotEvidence pivot;
    pivot.center = query;
    pivot.birth_query = query_id;
    pivot.birth_region_visit = region->visits;
    pivot.pool = (PIVOT_POOL_POLICY == 0 || PIVOT_POOL_POLICY == 5) ? pool : 0;
    pivot.last_used_region_visit = region->visits;
    pivot.dist_sorted.reserve(covered_use.size());
    for (const auto &entry : covered_use) {
        pivot.dist_sorted.push_back({entry.second, entry.first});
        if (entry.second < pivot.min_dist) pivot.min_dist = entry.second;
        if (entry.second > pivot.max_dist) pivot.max_dist = entry.second;
        pivot.memory_pairs++;
        retainEvidencePair(query_id, entry.first);
    }
    if (pivot.memory_pairs <= 0)
        return;
    pivot.lookup_built = false;
    pivot.sorted_built = false;
    region->pivots.push_back(std::move(pivot));
#ifdef RA_COLUMN_ORB_EXECUTOR
    if (g_use_column_orb) {
        int token = buildRegionLocalPos(region);
        initPivotColumnFromCovered(region->pivots.back(), region, token, covered_use);
    }
#endif
    enforceRegionPivotBudget(region);
}

bool addDistanceToExistingPivot(Region *region, int query_id, int object_id, float distance) {
    if (!region)
        return false;
    for (auto &pivot : region->pivots) {
        if (pivot.birth_query != query_id)
            continue;
        ensurePivotLookup(pivot);
        if (pivot.dist_by_id.find(object_id) != pivot.dist_by_id.end())
            return false;
        pivot.dist_by_id[object_id] = distance;
        pivot.dist_sorted.push_back({distance, object_id});
        pivot.sorted_built = false;
        if (distance < pivot.min_dist) pivot.min_dist = distance;
        if (distance > pivot.max_dist) pivot.max_dist = distance;
        pivot.memory_pairs++;
        retainEvidencePair(query_id, object_id);
#ifdef RA_COLUMN_ORB_EXECUTOR
        if (g_use_column_orb
            && pivot.col_region_M == region->col_M
            && !region->objects.empty()
            && region->objects.back() == object_id) {
            int pos = region->col_M - 1;
            if (pos >= 0 && pos < (int)pivot.col_dist.size()) {
                pivot.col_dist[pos] = distance;
                pivot.col_valid_mask[pos >> 6] |= (1ULL << (pos & 63));
            }
        }
#endif
        return true;
    }
    return false;
}

