void updateRegionStats(Region *region, long long checked, long long fp, long long answers) {
    region->visits++;
    bool had_ema = region->ema_ready;
    double old_checked_ema = region->checked_ema;
    region->last_checked = checked;
    if (!had_ema) {
        region->adaptive_eviction_mode = 0;
    } else if ((double)checked < old_checked_ema) {
        region->adaptive_eviction_mode = 0;
    } else if ((double)checked > max(1.0, old_checked_ema) * RECAST_EMERGENCY_CHECKED_SPIKE) {
        region->adaptive_eviction_mode = 2;
    } else {
        region->adaptive_eviction_mode = 1;
    }
    if (!region->ema_ready) {
        region->checked_ema = (double)checked;
        region->ema_ready = true;
    } else {
        region->checked_ema = EMA_ALPHA * (double)checked + (1.0 - EMA_ALPHA) * region->checked_ema;
    }
}

double medianCheckedDistance(const vector<pair<int, float>> &checked_dists) {
    if (checked_dists.empty())
        return 0.0;
    vector<float> distances;
    distances.reserve(checked_dists.size());
    for (const auto &entry : checked_dists)
        distances.push_back(entry.second);
    nth_element(distances.begin(), distances.begin() + distances.size() / 2, distances.end());
    return distances[distances.size() / 2];
}

double pivotKeepScore(Region *region, long long checked, long long fp,
                      bool decreasing, bool sudden, bool explore) {
    double recent_fp = (double)fp;
    double future_visits = min(16.0, max(2.0, (double)region->visits));
    double future_cost = future_visits;
    double state_bonus = 0.0;
    if (decreasing) state_bonus += 0.25 * recent_fp;
    if (sudden) state_bonus += 0.50 * recent_fp;
    if (explore) state_bonus += 0.10 * recent_fp;
    double coverage_bonus = min((double)checked, recent_fp);
    return 0.50 * recent_fp + 0.25 * coverage_bonus + state_bonus - future_cost;
}

double splitNetScore(Region *region, float radius,
                     const vector<pair<int, float>> &checked_dists,
                     long long checked, long long fp) {
    if (!region || checked_dists.empty())
        return -DBL_MAX;
    double region_size = max(1, (int)region->objects.size());
    double median_dist = medianCheckedDistance(checked_dists);
    if (median_dist <= radius * SPLIT_RADIUS_MARGIN)
        return -DBL_MAX / 2.0;

    double future_visits = min(32.0, max(4.0, (double)region->visits));
    double expected_saved_per_visit = 0.5 * (double)checked;
    double split_extra_est = max(0.0, region_size - (double)checked);
    double child_cold_start = min(region_size, (double)LEAF_THRESHOLD * 4.0) * SPLIT_COLD_WEIGHT;
    double fp_bonus = (double)fp * 0.10;
    return future_visits * expected_saved_per_visit + fp_bonus
           - SPLIT_EXTRA_WEIGHT * split_extra_est - child_cold_start;
}

double recastLazySplitScore(Region *region, long long checked, long long fp) {
    if (!region || checked <= 0)
        return -DBL_MAX;
    double region_size = max(1, (int)region->objects.size());
    double fp_ratio = (double)fp / (double)max(1LL, checked);
    double assigned = (double)checked;
    double residual = max(0.0, region_size - assigned);
    double future_visits = min(64.0, max(4.0, (double)region->visits));




    double expected_saved_per_visit = 0.25 * assigned * fp_ratio;
    double residual_drag = 0.10 * residual;
    double routing_and_metadata = future_visits + 2.0;
    return future_visits * expected_saved_per_visit
           - residual_drag
           - routing_and_metadata;
}

bool shouldTimeAwareFullSplit(Region *region,
                              const vector<pair<int, float>> &checked_dists,
                              long long checked, long long fp,
                              long long local_pivot_pruned,
                              double old_checked_ema, bool had_ema) {
    if (!timeAwareSplitEnabled() || !region || !region->isLeaf() || POLICY_MODE != 3)
        return false;
    int n = (int)region->objects.size();
    int min_region_size = max(32 * LEAF_THRESHOLD, 4096);
    if (n < min_region_size || region->depth >= MAX_DEPTH)
        return false;
    if (region->visits < 8)
        return false;
    if ((int)checked_dists.size() < max(2 * LEAF_THRESHOLD, 512))
        return false;

    double checked_ratio = (double)checked / (double)max(1, n);
    double fp_ratio = (double)fp / (double)max(1LL, checked);
    double prune_ratio = (double)local_pivot_pruned /
                         (double)max(1LL, checked + local_pivot_pruned);
    bool high_scan = checked_ratio >= 0.05 || checked >= 2048;
    bool mostly_fp = fp_ratio >= 0.30;
    bool checked_spike = had_ema && checked >= old_checked_ema * 20.00;
    bool pivot_not_solving = prune_ratio < 0.50 || checked >= 4096;
    return mostly_fp &&
        high_scan && checked_spike && pivot_not_solving;
}

bool shadowExpired(Region *region) {
    if (!region || !region->shadow.active)
        return false;
    const ShadowSplit &shadow = region->shadow;
    long long assigned = shadow.left_count + shadow.right_count;
    double routing_selectivity = (double)shadow.visited_sample /
        (double)max(1LL, shadow.visits * assigned);
    if (shadow.visits >= RECAST_SHADOW_TTL &&
        shadow.net <= RECAST_SHADOW_COMMIT_MARGIN)
        return true;
    if (shadow.visits >= RECAST_SHADOW_MIN_VISITS &&
        routing_selectivity >= 0.95 &&
        shadow.net <= 0.0)
        return true;
    return false;
}

void rejectShadowSplit(Region *region, QueryStats &stats, bool replaced) {
    if (!region || !region->shadow.active)
        return;
    region->shadow.active = false;
    if (replaced) {
    } else {
    }
}

double shadowSeedScore(const ShadowSplit &shadow) {
    return max(0.0, shadow.net) + 0.1 * (double)shadow.skipped_sample;
}

vector<int> checkedIdsFromDists(const vector<pair<int, float>> &checked_dists) {
    vector<int> ids;
    ids.reserve(checked_dists.size());
    for (const auto &entry : checked_dists)
        ids.push_back(entry.first);
    return ids;
}

void rememberRecentObservation(Region *region, const vector<float> &query,
                               float radius,
                               const vector<pair<int, float>> &checked_dists) {
    if (!region || POLICY_MODE != 3 || checked_dists.empty())
        return;
    RecentObservation observation;
    observation.query = query;
    observation.radius = radius;
    int stride = 1;
    if ((int)checked_dists.size() > RECAST_RECENT_MAX_IDS)
        stride = (int)ceil((double)checked_dists.size() / (double)RECAST_RECENT_MAX_IDS);
    observation.checked_ids.reserve(min((int)checked_dists.size(), RECAST_RECENT_MAX_IDS));
    for (int i = 0; i < (int)checked_dists.size(); i += stride)
        observation.checked_ids.push_back(checked_dists[i].first);
    region->recent_observations.push_back(std::move(observation));
    while ((int)region->recent_observations.size() > RECAST_RECENT_REPLAY)
        region->recent_observations.erase(region->recent_observations.begin());
}

float medianThreshold(const vector<pair<int, float>> &covered) {
    vector<float> &values = threshold_values_workspace;
    values.clear();
    if (values.capacity() < covered.size())
        values.reserve(covered.size());
    for (const auto &entry : covered)
        values.push_back(entry.second);
    nth_element(values.begin(), values.begin() + values.size() / 2, values.end());
    return values[values.size() / 2];
}

bool shadowIntervalsForThreshold(const vector<pair<int, float>> &covered,
                                 float threshold,
                                 float &left_min, float &left_max,
                                 float &right_min, float &right_max,
                                 int &left_count, int &right_count) {
    left_min = FLT_MAX;
    left_max = 0.0f;
    right_min = FLT_MAX;
    right_max = 0.0f;
    left_count = 0;
    right_count = 0;
    for (const auto &entry : covered) {
        float dist = entry.second;
        if (dist <= threshold) {
            left_count++;
            if (dist < left_min) left_min = dist;
            if (dist > left_max) left_max = dist;
        } else {
            right_count++;
            if (dist < right_min) right_min = dist;
            if (dist > right_max) right_max = dist;
        }
    }
    return left_count > 0 && right_count > 0;
}

long long thresholdReplayScore(const unordered_map<int, float> &dist_by_id,
                               float threshold,
                               float left_min, float left_max,
                               float right_min, float right_max,
                               const vector<int> &checked_ids,
                               float qdist, float radius) {
    bool visit_left = intervalIntersects(qdist, left_min, left_max, radius);
    bool visit_right = intervalIntersects(qdist, right_min, right_max, radius);
    long long score = 0;
    for (int id : checked_ids) {
        auto it = dist_by_id.find(id);
        if (it == dist_by_id.end())
            continue;
        bool left = it->second <= threshold;
        if ((left && !visit_left) || (!left && !visit_right))
            score++;
    }
    return score;
}

float chooseShadowThreshold(Region *region, const vector<float> &center,
                            const vector<pair<int, float>> &covered,
                            const vector<float> &query, float radius,
                            const vector<pair<int, float>> &checked_dists,
                            float current_qdist, QueryStats &stats) {
    if (!RECAST_WORKLOAD_THRESHOLD || covered.empty())
        return medianThreshold(covered);

    vector<float> values;
    values.reserve(covered.size());
    unordered_map<int, float> dist_by_id;
    dist_by_id.reserve(covered.size() * 2 + 1);
    for (const auto &entry : covered) {
        dist_by_id[entry.first] = entry.second;
        values.push_back(entry.second);
    }
    sort(values.begin(), values.end());
    vector<double> quantiles = {0.10, 0.25, 0.40, 0.50, 0.60, 0.75, 0.90};
    vector<int> current_ids = checkedIdsFromDists(checked_dists);

    struct ReplayEval {
        vector<int> ids;
        float qdist;
        float radius;
    };
    vector<ReplayEval> replays;
    replays.push_back({current_ids, current_qdist, radius});
    if (region) {
        for (auto it = region->recent_observations.rbegin();
             it != region->recent_observations.rend() &&
             (int)replays.size() <= RECAST_RECENT_REPLAY; ++it) {
            float qdist = distCompute(it->query, center);
            replays.push_back({it->checked_ids, qdist, it->radius});
        }
    }

    float best_threshold = values[values.size() / 2];
    long long best_score = -1;
    int best_balance = -1;
    for (double quantile : quantiles) {
        int idx = min((int)values.size() - 1,
                      max(0, (int)floor(quantile * (double)(values.size() - 1))));
        float threshold = values[idx];
        float left_min, left_max, right_min, right_max;
        int left_count, right_count;
        if (!shadowIntervalsForThreshold(covered, threshold,
                                         left_min, left_max, right_min, right_max,
                                         left_count, right_count))
            continue;
        long long score = 0;
        for (const ReplayEval &replay : replays)
            score += thresholdReplayScore(dist_by_id, threshold,
                                          left_min, left_max, right_min, right_max,
                                          replay.ids, replay.qdist, replay.radius);
        int balance = min(left_count, right_count);
        if (score > best_score || (score == best_score && balance > best_balance)) {
            best_score = score;
            best_balance = balance;
            best_threshold = threshold;
        }
    }
    return best_threshold;
}

bool buildShadowSplit(ShadowSplit &shadow, const vector<float> &center,
                      const vector<pair<int, float>> &covered,
                      float threshold, int query_id, int birth_region_visit,
                      bool from_pivot) {
    shadow = ShadowSplit();
    shadow.active = true;
    shadow.center = center;
    shadow.birth_query = query_id;
    shadow.birth_region_visit = birth_region_visit;
    shadow.threshold = threshold;
    shadow.from_pivot = from_pivot;
    shadow.assigned.reserve(covered.size());
    shadow.side_token = nextShadowSideToken();
    for (const auto &entry : covered) {
        int id = entry.first;
        float dist = entry.second;
        shadow.assigned.push_back({id, dist});
        if (dist <= shadow.threshold) {
            shadow_side_stamp[id] = shadow.side_token;
            shadow_side_value[id] = 0;
            shadow.left_count++;
            if (dist < shadow.left_min) shadow.left_min = dist;
            if (dist > shadow.left_max) shadow.left_max = dist;
        } else {
            shadow_side_stamp[id] = shadow.side_token;
            shadow_side_value[id] = 1;
            shadow.right_count++;
            if (dist < shadow.right_min) shadow.right_min = dist;
            if (dist > shadow.right_max) shadow.right_max = dist;
        }
    }
    return shadow.left_count > 0 && shadow.right_count > 0;
}

bool createShadowSplitFromColumn(Region *region, const vector<float> &center,
                                 const vector<pair<int, float>> &covered,
                                 const vector<float> &query, float radius,
                                 const vector<pair<int, float>> &checked_dists,
                                 long long checked, long long fp,
                                 bool force_replace, bool from_pivot,
                                 float current_qdist,
                                 int query_id, QueryStats &stats) {
    if (!region || POLICY_MODE != 3 || covered.empty())
        return false;
    int min_assigned = max(2 * LEAF_THRESHOLD, RECAST_MIN_COVERAGE * 2);
    if ((int)covered.size() < min_assigned)
        return false;
    if (region->shadow.active) {
        if (shadowExpired(region)) {
            rejectShadowSplit(region, stats, false);
        } else {
            if (!force_replace && region->shadow.visits < RECAST_SHADOW_MIN_VISITS)
                return false;
            double fp_rate = (double)fp / (double)max(1LL, checked);
            double new_score = (double)covered.size() * fp_rate;
            double old_score = max(1.0, shadowSeedScore(region->shadow));
            if (!force_replace && new_score <= old_score * RECAST_SHADOW_REPLACE_MARGIN)
                return false;
            rejectShadowSplit(region, stats, true);
        }
    }

    ShadowSplit shadow;
    float threshold = chooseShadowThreshold(region, center, covered,
                                            query, radius, checked_dists,
                                            current_qdist, stats);
    if (!buildShadowSplit(shadow, center, covered, threshold,
                          query_id, region->visits, from_pivot))
        return false;
    region->shadow = std::move(shadow);
    if (from_pivot) {
    }
    return true;
}

bool createShadowSplit(Region *region, const vector<float> &query, float radius,
                       const vector<pair<int, float>> &checked_dists,
                       long long checked, long long fp,
                       bool force_replace,
                       int query_id, QueryStats &stats) {
    return createShadowSplitFromColumn(region, query, checked_dists,
                                       query, radius, checked_dists,
                                       checked, fp, force_replace, false,
                                       0.0f, query_id, stats);
}

void createPivotShadowSplits(Region *region, const vector<float> &query, float radius,
                             const vector<pair<int, float>> &checked_dists,
                             long long checked, long long fp,
                             bool force_replace, int query_id, QueryStats &stats) {
    if (!RECAST_SHADOW_FROM_PIVOTS || !region || POLICY_MODE != 3 || checked_dists.empty())
        return;
    vector<int> order(region->pivots.size());
    iota(order.begin(), order.end(), 0);
    sort(order.begin(), order.end(), [&](int a, int b) {
        const PivotEvidence &left = region->pivots[a];
        const PivotEvidence &right = region->pivots[b];
        double left_score = (double)(left.pruned + 1) * log(2.0 + (double)left.memory_pairs) /
                            (double)(left.query_dist_cost + 1);
        double right_score = (double)(right.pruned + 1) * log(2.0 + (double)right.memory_pairs) /
                             (double)(right.query_dist_cost + 1);
        return left_score > right_score;
    });
    int tried = 0;
    for (int idx : order) {
        if (tried >= RECAST_SHADOW_PIVOT_CANDIDATES)
            break;
        PivotEvidence &pivot = region->pivots[idx];
        if (pivot.birth_query == query_id)
            continue;
        ensurePivotLookup(pivot);
        vector<pair<int, float>> covered;
        covered.reserve(region->objects.size());
        for (int id : region->objects) {
            auto it = pivot.dist_by_id.find(id);
            if (it != pivot.dist_by_id.end())
                covered.push_back({id, it->second});
        }
        int min_assigned = max(2 * LEAF_THRESHOLD, RECAST_MIN_COVERAGE * 2);
        if ((int)covered.size() < min_assigned)
            continue;
        tried++;
        float qdist = distCompute(query, pivot.center);
        bool created = createShadowSplitFromColumn(region, pivot.center, covered,
                                                   query, radius, checked_dists,
                                                   checked, fp, force_replace,
                                                   true, qdist, pivot.birth_query, stats);
        if (created)
            return;
    }
}

void updateShadowSplit(Region *region, const vector<float> &query, float radius,
                       int query_id, QueryStats &stats) {
    if (!region || POLICY_MODE != 3 || !region->shadow.active)
        return;
    ShadowSplit &shadow = region->shadow;
    float qdist = distCompute(query, shadow.center);
    shadow.visits++;
    shadow.routing_dc++;
    bool visit_left = intervalIntersects(qdist, shadow.left_min, shadow.left_max, radius);
    bool visit_right = intervalIntersects(qdist, shadow.right_min, shadow.right_max, radius);
    shadow.eval_query_id = query_id;
    shadow.eval_visit_left = visit_left;
    shadow.eval_visit_right = visit_right;
    long long visited = 0;
    if (visit_left)
        visited += shadow.left_count;
    if (visit_right)
        visited += shadow.right_count;
    long long assigned = shadow.left_count + shadow.right_count;
    long long skipped = max(0LL, assigned - visited);
    shadow.visited_sample += visited;
    shadow.skipped_sample += skipped;
    shadow.net -= 1.0;
}

bool pivotTableSufficient(long long checked, long long fp, long long local_pivot_pruned) {
    if (checked <= 0)
        return true;
    double fp_rate = (double)fp / (double)max(1LL, checked);
    double prune_rate = (double)local_pivot_pruned / (double)max(1LL, checked + local_pivot_pruned);
    return fp_rate < 0.30 || prune_rate >= RECAST_PIVOT_GOOD_PRUNE_RATE;
}

bool pivotTableSufficientRecent(Region *region, long long checked, long long fp,
                                long long local_pivot_pruned,
                                double old_checked_ema, bool had_ema) {
    if (!region || checked <= 0)
        return true;
    double fp_rate = (double)fp / (double)max(1LL, checked);
    double prune_rate = (double)local_pivot_pruned / (double)max(1LL, checked + local_pivot_pruned);
    bool checked_decreasing = had_ema && (double)checked < old_checked_ema * DROP_RATIO;
    if (fp_rate < 0.30)
        return true;
    if (prune_rate >= RECAST_PIVOT_GOOD_PRUNE_RATE && checked_decreasing)
        return true;
    return false;
}

bool commitShadowSplit(Region *region, const vector<float> &query,
                       long long checked, long long fp, long long local_pivot_pruned,
                       double old_checked_ema, bool had_ema,
                       int query_id, QueryStats &stats) {
    if (!region || POLICY_MODE != 3 || !region->isLeaf() || !region->shadow.active)
        return false;
    ShadowSplit &shadow = region->shadow;
    long long assigned = shadow.left_count + shadow.right_count;
    if (assigned <= 0 || shadow.visits <= 0)
        return false;
    double routing_selectivity = (double)shadow.visited_sample /
        (double)max(1LL, shadow.visits * assigned);
    double fp_rate = (double)fp / (double)max(1LL, checked);
    double prune_rate = (double)local_pivot_pruned / (double)max(1LL, checked + local_pivot_pruned);
    double saved_rate = (double)shadow.counterfactual_saved_true_checks /
                        (double)max(1LL, checked);
    bool pivot_not_sufficient = !pivotTableSufficientRecent(region, checked, fp,
                                                            local_pivot_pruned,
                                                            old_checked_ema, had_ema);
    bool normal = shadow.visits >= RECAST_SHADOW_MIN_VISITS &&
                  shadow.counterfactual_saved_true_checks >= RECAST_SHADOW_MIN_SAVED &&
                  shadow.net > RECAST_SHADOW_COMMIT_MARGIN &&
                  routing_selectivity < RECAST_SHADOW_MAX_SELECTIVITY &&
                  (region->depth > 0 || pivot_not_sufficient);
    bool emergency = shadow.visits >= RECAST_SHADOW_EMERGENCY_VISITS &&
                     had_ema &&
                     checked > max(1.0, old_checked_ema) * RECAST_EMERGENCY_CHECKED_SPIKE &&
                     fp_rate > 0.30 &&
                     shadow.net > RECAST_SHADOW_COMMIT_MARGIN &&
                     saved_rate >= 0.10;
    if (!normal && !emergency) {
        if (shadowExpired(region))
            rejectShadowSplit(region, stats, false);
        return false;
    }

    vector<pair<int, float>> assigned_dists = shadow.assigned;
    vector<float> center = shadow.center;
    float threshold = shadow.threshold;
    bool committed_from_pivot = shadow.from_pivot;
    region->shadow.active = false;
    int evidence_query_id = shadow.birth_query >= 0 ? shadow.birth_query : query_id;
    bool split_ok = RECAST_EAGER_SHADOW_SPLIT
        ? splitRegion(region, center, assigned_dists, evidence_query_id, stats)
        : lazySplitRegion(region, center, assigned_dists, evidence_query_id, stats, true, threshold);
    if (split_ok) {
        if (committed_from_pivot) {
        }
        return true;
    }
    return false;
}

void maybeGrowRegion(Region *region, const vector<float> &query, float radius,
                     const vector<pair<int, float>> &checked_dists,
                     long long checked, long long fp, long long answers,
                     long long local_pivot_pruned,
                     double old_checked_ema, bool had_ema,
                     int query_id, QueryStats &stats) {
    if (!region || checked <= 0)
        return;
    if (POLICY_MODE < 2 && !region->isLeaf())
        return;
    double region_size = max(1, (int)region->objects.size());
    double checked_ratio = (double)checked / region_size;
    double fp_ratio = (double)fp / (double)max(1LL, checked);
    bool high = checked_ratio >= CHECKED_HIGH_RATIO && fp_ratio >= FP_HIGH_RATIO;
    bool usefulPivotEvidence = checked >= MIN_PIVOT_CHECKED &&
                               fp_ratio >= PIVOT_FP_RATIO;

    bool decreasing = had_ema && (double)checked < old_checked_ema * DROP_RATIO;
    bool flat = had_ema && (double)checked > old_checked_ema * FLAT_RATIO;
    bool sudden = had_ema &&
                  old_checked_ema / region_size < CHECKED_HIGH_RATIO * 0.5 &&
                  checked_ratio >= CHECKED_HIGH_RATIO;






    if (LIGHTWEIGHT_SPLIT_CHILDREN && region->lightweight_runtime) {
        if (PRACTICAL_SPLIT_M_THRESHOLD > 0
            && region->isLeaf()
            && (int)region->objects.size() > PRACTICAL_SPLIT_M_THRESHOLD
            && region->visits >= PRACTICAL_SPLIT_MIN_VISITS
            && (int)checked_dists.size() >= max(2 * LEAF_THRESHOLD, RECAST_MIN_COVERAGE * 2)
            && region->depth < MAX_DEPTH) {
            lazySplitRegion(region, query, checked_dists, query_id, stats);
        }
        if (LIGHTWEIGHT_CHILD_PIVOT_CAP > 0
            && region->lightweight_growth_installs < LIGHTWEIGHT_CHILD_PIVOT_CAP
            && checked >= RECAST_MIN_COVERAGE
            && fp >= RECAST_MIN_FALSE_POSITIVE
            && region->visits >= 2) {
            installPivot(region, query, checked_dists, query_id, 1);
            region->lightweight_growth_installs++;
            updatePivotPools(region);
        }
        return;
    }

    bool canKeep = (int)region->pivots.size() < REGION_PIVOT_MAX || POLICY_MODE >= 2;
    bool explore = region->exploration_installs < EXPLORATION_PIVOTS;
    bool splitCandidate = high && flat && !sudden &&
                          region->isLeaf() &&
                          region->visits >= MIN_SPLIT_VISITS &&
                          (int)region->pivots.size() >= MIN_SPLIT_PIVOTS;
    if (POLICY_MODE >= 2) {
        int min_region_size = max(4 * LEAF_THRESHOLD, 512);
        bool checked_spike = had_ema &&
                             (double)checked > max(1.0, old_checked_ema) * 2.0;
        bool pivot_collapse = (double)local_pivot_pruned <
                              (double)checked * RECAST_PIVOT_COLLAPSE_RATE;
        bool pivot_full = (int)region->pivots.size() >= REGION_PIVOT_MAX;
        splitCandidate = high && flat && !sudden &&
                         region->isLeaf() &&
                         (int)region->objects.size() >= min_region_size &&
                         (int)checked_dists.size() >= max(2 * LEAF_THRESHOLD, RECAST_MIN_COVERAGE * 2) &&
                         region->visits >= MIN_SPLIT_VISITS &&
                         (int)region->pivots.size() >= MIN_SPLIT_PIVOTS;
        if (POLICY_MODE == 3) {
            splitCandidate = region->isLeaf() &&
                             region->visits >= 2 &&
                             (int)region->objects.size() >= min_region_size &&
                             (int)checked_dists.size() >= max(2 * LEAF_THRESHOLD, RECAST_MIN_COVERAGE * 2) &&
                             fp >= RECAST_MIN_FALSE_POSITIVE &&
                             (checked_ratio >= CHECKED_HIGH_RATIO ||
                              checked_spike ||
                              pivot_collapse ||
                              pivot_full);
        }
    }
    bool shouldSplit = splitCandidate;

    if (splitCandidate && POLICY_MODE == 1) {
        double score = splitNetScore(region, radius, checked_dists, checked, fp);
        shouldSplit = score > SPLIT_MIN_SCORE;
        if (!shouldSplit) {
        }
    }

    if (splitCandidate && POLICY_MODE == 3) {
    } else if (shouldSplit && POLICY_MODE == 2) {
        double score = recastLazySplitScore(region, checked, fp);
        if (score > SPLIT_MIN_SCORE &&
            lazySplitRegion(region, query, checked_dists, query_id, stats))
            return;
    } else if (shouldSplit && splitRegion(region, query, checked_dists, query_id, stats)) {
        return;
    }






    if (PRACTICAL_SPLIT_M_THRESHOLD > 0
        && region->isLeaf()
        && (int)region->objects.size() > PRACTICAL_SPLIT_M_THRESHOLD
        && region->visits >= PRACTICAL_SPLIT_MIN_VISITS
        && (int)checked_dists.size() >= max(2 * LEAF_THRESHOLD, RECAST_MIN_COVERAGE * 2)
        && region->depth < MAX_DEPTH) {
        if (lazySplitRegion(region, query, checked_dists, query_id, stats))
            return;
    }

    if (shouldTimeAwareFullSplit(region, checked_dists, checked, fp,
                                 local_pivot_pruned, old_checked_ema, had_ema) &&
        splitRegion(region, query, checked_dists, query_id, stats))
        return;

    bool pivotCandidate = usefulPivotEvidence &&
                          (!had_ema || decreasing || sudden || (high && explore));
    if (POLICY_MODE >= 2) {
        pivotCandidate = checked >= RECAST_MIN_COVERAGE &&
                         fp >= RECAST_MIN_FALSE_POSITIVE &&
                         region->visits >= 2;
        if (FORCE_PIVOT_ADMISSION) {
            pivotCandidate = !checked_dists.empty() &&
                             (int)region->pivots.size() < REGION_PIVOT_MAX;
        }
    }
    bool shouldKeep = pivotCandidate;

    if (pivotCandidate && POLICY_MODE == 1) {
        double score = pivotKeepScore(region, checked, fp, decreasing, sudden, explore);
        shouldKeep = score > PIVOT_MIN_SCORE;
        if (!shouldKeep) {
        }
    }

    if (canKeep && shouldKeep) {
        installPivot(region, query, checked_dists, query_id, POLICY_MODE >= 2 ? 1 : 0);
        region->exploration_installs++;
    }
    if (POLICY_MODE >= 2)
        updatePivotPools(region);
    if (POLICY_MODE == 3 && SPLIT_POLICY_MODE != 0) {
        bool basicSplitReady = region->isLeaf() &&
                               region->visits >= 2 &&
                               (int)region->objects.size() >= max(4 * LEAF_THRESHOLD, 512) &&
                               (int)checked_dists.size() >= max(2 * LEAF_THRESHOLD, RECAST_MIN_COVERAGE * 2) &&
                               region->depth < MAX_DEPTH;
        bool directSplit = SPLIT_POLICY_MODE == 1
            ? basicSplitReady
            : (basicSplitReady && splitCandidate);
        if (directSplit)
            lazySplitRegion(region, query, checked_dists, query_id, stats);
        return;
    }
    if (POLICY_MODE == 3) {
        bool checked_spike = had_ema &&
                             (double)checked > max(1.0, old_checked_ema) * 2.0;
        bool committed = commitShadowSplit(region, query, checked, fp, local_pivot_pruned,
                                           old_checked_ema, had_ema, query_id, stats);
        bool query_shadow_created = false;
        if (!committed && splitCandidate &&
            (!RECAST_SHADOW_FROM_PIVOTS || !region->shadow.active)) {
            query_shadow_created = createShadowSplit(region, query, radius, checked_dists,
                                                     checked, fp, checked_spike,
                                                     query_id, stats);
        }
        if (!committed && splitCandidate && RECAST_SHADOW_FROM_PIVOTS && !query_shadow_created)
            createPivotShadowSplits(region, query, radius, checked_dists, checked, fp,
                                    checked_spike, query_id, stats);
    }
}

