int findWeakPivot(Region *region, bool nursery_only) {
    int best = -1;
    double best_score = DBL_MAX;
    for (int i = 0; i < (int)region->pivots.size(); i++) {
        const PivotEvidence &pivot = region->pivots[i];
        if (nursery_only && pivot.pool != 1)
            continue;
        double score = pivotDensity(pivot);
        if (pivot.pool == 1)
            score -= 1.0;
        if (score < best_score) {
            best_score = score;
            best = i;
        }
    }
    return best;
}

int findSingleTierEvictionPivot(Region *region) {
    if (!region || region->pivots.empty())
        return -1;
    if (PIVOT_POOL_POLICY == 2) {
        int best = 0;
        for (int i = 1; i < (int)region->pivots.size(); i++) {
            const PivotEvidence &left = region->pivots[i];
            const PivotEvidence &right = region->pivots[best];
            if (left.birth_query < right.birth_query ||
                (left.birth_query == right.birth_query &&
                 left.birth_region_visit < right.birth_region_visit))
                best = i;
        }
        return best;
    }
    if (PIVOT_POOL_POLICY == 3) {
        int best = 0;
        for (int i = 1; i < (int)region->pivots.size(); i++) {
            const PivotEvidence &left = region->pivots[i];
            const PivotEvidence &right = region->pivots[best];
            if (left.last_used_region_visit < right.last_used_region_visit ||
                (left.last_used_region_visit == right.last_used_region_visit &&
                 left.birth_region_visit < right.birth_region_visit))
                best = i;
        }
        return best;
    }
    return findWeakPivot(region, false);
}

int findAdaptiveEvictionPivot(Region *region, bool nursery_only) {
    if (!region || region->pivots.empty())
        return -1;
    int mode = region->adaptive_eviction_mode;
    int best = -1;
    if (mode == 2) {


        for (int i = 0; i < (int)region->pivots.size(); i++) {
            const PivotEvidence &pivot = region->pivots[i];
            if (nursery_only && pivot.pool != 1)
                continue;
            if (best < 0 ||
                pivot.birth_query < region->pivots[best].birth_query ||
                (pivot.birth_query == region->pivots[best].birth_query &&
                 pivot.birth_region_visit < region->pivots[best].birth_region_visit))
                best = i;
        }
        return best;
    }
    if (mode == 0) {


        double best_net = DBL_MAX;
        for (int i = 0; i < (int)region->pivots.size(); i++) {
            const PivotEvidence &pivot = region->pivots[i];
            if (nursery_only && pivot.pool != 1)
                continue;
            double net = pivotNet(pivot);
            if (net <= 0.0 && net < best_net) {
                best_net = net;
                best = i;
            }
        }
        return best;
    }

    return findWeakPivot(region, nursery_only);
}

void enforceRegionPivotBudget(Region *region) {
    if (!region || POLICY_MODE < 2)
        return;
    if (PIVOT_POOL_POLICY != 0 && PIVOT_POOL_POLICY != 5) {
        while ((int)region->pivots.size() > REGION_PIVOT_MAX) {
            int idx = PIVOT_POOL_POLICY == 6
                ? findAdaptiveEvictionPivot(region, false)
                : findSingleTierEvictionPivot(region);
            if (idx < 0)
                break;
            erasePivot(region, idx);
        }
        return;
    }
    int nursery_count = 0;
    for (const auto &pivot : region->pivots)
        if (pivot.pool == 1)
            nursery_count++;
    while (nursery_count > RECAST_NURSERY_MAX) {
        int idx = PIVOT_POOL_POLICY == 5
            ? findAdaptiveEvictionPivot(region, true)
            : findWeakPivot(region, true);
        if (idx < 0)
            break;
        erasePivot(region, idx);
        nursery_count--;
    }
    while ((int)region->pivots.size() > REGION_PIVOT_MAX) {
        int idx = PIVOT_POOL_POLICY == 5
            ? findAdaptiveEvictionPivot(region, false)
            : findWeakPivot(region, false);
        if (idx < 0)
            break;
        erasePivot(region, idx);
    }
}

void updatePivotPools(Region *region) {
    if (!region || POLICY_MODE < 2)
        return;
    if (PIVOT_POOL_POLICY != 0 && PIVOT_POOL_POLICY != 5) {
        enforceRegionPivotBudget(region);
        return;
    }
    for (int i = 0; i < (int)region->pivots.size();) {
        PivotEvidence &pivot = region->pivots[i];
        double net = pivotNet(pivot);
        if (pivot.pool == 1 &&
            pivot.query_dist_cost >= RECAST_MIN_PROBE_USES &&
            net >= RECAST_PROMOTE_MARGIN) {
            pivot.pool = 0;
            i++;
            continue;
        }
        if (pivot.pool == 1 &&
            region->visits - pivot.birth_region_visit >= RECAST_NURSERY_TTL &&
            (net <= 0.0 || (PIVOT_POOL_POLICY == 5 && region->adaptive_eviction_mode == 2))) {
            erasePivot(region, i);
            continue;
        }
        i++;
    }
    enforceRegionPivotBudget(region);
}

