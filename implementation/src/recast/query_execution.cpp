void queryRegion(Region *region, const vector<float> &query, float radius,
                 int query_id, QueryStats &stats) {
    if (!region)
        return;
    if (RECAST_DIAGNOSTICS >= 2) g_recast_count_visited_region++;
    if (POLICY_MODE == 3)
        updateShadowSplit(region, query, radius, query_id, stats);

    if (!region->isLeaf()) {
        chrono::steady_clock::time_point _ts_route;
        if (RECAST_DIAGNOSTICS >= 2) _ts_route = chrono::steady_clock::now();
        float qdist = distCompute(query, region->split_center);



        long long _route_split_visits_before = region->split_visits;
        long long _route_M_left = regionObjectCount(region->left);
        long long _route_M_right = regionObjectCount(region->right);
        long long _route_M_residual = (region->residual ? regionObjectCount(region->residual) : 0);
        if (POLICY_MODE >= 2) {
            region->split_visits++;
            region->split_routing_dc++;
            region->last_routing_query_id = query_id;
            region->last_routing_qdist = qdist;
        }
        bool _route_visit_left = false, _route_visit_right = false, _route_visit_residual = false;
        if (!childCanBeSkipped(qdist, region->left_min, region->left_max, radius)) {
            if (RECAST_DIAGNOSTICS >= 2) g_recast_time_routing_ns += chrono::duration_cast<chrono::nanoseconds>(chrono::steady_clock::now() - _ts_route).count();
            _route_visit_left = true;
            queryRegion(region->left, query, radius, query_id, stats);
            if (RECAST_DIAGNOSTICS >= 2) _ts_route = chrono::steady_clock::now();
        } else if (POLICY_MODE >= 2) {
            long long skipped = regionObjectCount(region->left);
            region->split_skipped_objects += skipped;
        }
        if (!childCanBeSkipped(qdist, region->right_min, region->right_max, radius)) {
            if (RECAST_DIAGNOSTICS >= 2) g_recast_time_routing_ns += chrono::duration_cast<chrono::nanoseconds>(chrono::steady_clock::now() - _ts_route).count();
            _route_visit_right = true;
            queryRegion(region->right, query, radius, query_id, stats);
            if (RECAST_DIAGNOSTICS >= 2) _ts_route = chrono::steady_clock::now();
        } else if (POLICY_MODE >= 2) {
            long long skipped = regionObjectCount(region->right);
            region->split_skipped_objects += skipped;
        }
        if (RECAST_RESIDUAL_CHILD && region->residual) {
            if (RECAST_DIAGNOSTICS >= 2) {
                g_recast_count_residual_region++;
                g_recast_time_routing_ns += chrono::duration_cast<chrono::nanoseconds>(chrono::steady_clock::now() - _ts_route).count();
            }
            _route_visit_residual = true;
            queryRegion(region->residual, query, radius, query_id, stats);
            if (RECAST_DIAGNOSTICS >= 2) _ts_route = chrono::steady_clock::now();
        }
        if (RECAST_DIAGNOSTICS >= 2) g_recast_time_routing_ns += chrono::duration_cast<chrono::nanoseconds>(chrono::steady_clock::now() - _ts_route).count();
        if (RECAST_DIAGNOSTICS >= 3 && g_routing_trace_fp) {
            fprintf(g_routing_trace_fp,
                    "%d\t%d\t%lld\t%d\t%d\t%d\t%lld\t%lld\t%lld\n",
                    query_id, region->id, _route_split_visits_before,
                    (int)_route_visit_left, (int)_route_visit_right, (int)_route_visit_residual,
                    _route_M_left, _route_M_right, _route_M_residual);
        }
        if (POLICY_MODE < 2 || region->objects.empty()) {
            if (POLICY_MODE >= 2)
                mergeBadSplit(region, stats);
            return;
        }
    }

    bool processing_residual_bucket = !region->isLeaf() || region->is_residual_region;
    leaf_points += region->objects.size();
    chrono::steady_clock::time_point _ts_active;
    if (RECAST_DIAGNOSTICS >= 2) _ts_active = chrono::steady_clock::now();
    updatePivotPools(region);
    vector<int> active_pivots;
    active_pivots.reserve(region->pivots.size());
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
        active_limit = active_pivots.size();
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
        && active_limit > ADAPTIVE_BUDGET_K) {
        active_limit = ADAPTIVE_BUDGET_K;
        active_pivots.resize(active_limit);
    }
    if (RECAST_DIAGNOSTICS >= 2) {
        g_recast_time_active_select_ns += chrono::duration_cast<chrono::nanoseconds>(chrono::steady_clock::now() - _ts_active).count();
        g_recast_count_active_pivot_total += active_limit;
    }

    long long local_checked = 0;
    long long local_fp = 0;
    long long local_answers = 0;
    long long local_pivot_pruned = 0;

    int rt_seed_triggered = 0;
    long long rt_inside_band = -1;
    long long rt_pivot_filter_ns = 0;
    long long rt_survivor_ns = 0;

    int object_count = region->objects.size();
#ifdef RA_COLUMN_ORB_EXECUTOR
    RA_COL_CHECK(region);
#endif
    vector<pair<int, float>> &checked_dists = checked_dists_workspace;
    checked_dists.clear();

    bool check_shadow_counterfactual =
        POLICY_MODE == 3 &&
        region->shadow.active &&
        region->shadow.eval_query_id == query_id &&
        (!region->shadow.eval_visit_left || !region->shadow.eval_visit_right);

#ifdef RA_COLUMN_ORB_EXECUTOR




    if (g_use_column_orb && region->col_M > 0 && region->col_M <= g_column_m_max) {






        int M = region->col_M;
        int W = (M + 63) >> 6;
        vector<uint64_t> &alive = region->alive_mask_ws;
        alive.assign(W, 0ULL);
        for (int w = 0; w < W; w++) alive[w] = ~0ULL;
        if (M & 63) alive[W - 1] = (1ULL << (M & 63)) - 1ULL;
        long long alive_count = M;

        for (int idx = 0; idx < active_limit && alive_count > 0; idx++) {
            int p = active_pivots[idx];
            PivotEvidence &pivot = region->pivots[p];



            bool has_alive = (pivot.memory_pairs > 0 && alive_count == M);
            if (!has_alive) {
                const uint64_t *vm = pivot.col_valid_mask.data();
                int VW = (int)pivot.col_valid_mask.size();
                int scan_W = W < VW ? W : VW;
                for (int w = 0; w < scan_W; w++) {
                    if (alive[w] & vm[w]) { has_alive = true; break; }
                }
            }
            if (!has_alive)
                continue;


            float qdist = distCompute(query, pivot.center);
            pivot.query_dist_cost++;
            pivot.last_used_region_visit = region->visits;
            float low = qdist - radius;
            float high = qdist + radius;
            if (low <= pivot.min_dist && high >= pivot.max_dist)
                continue;




            const float * __restrict__ col = pivot.col_dist.data();
            const uint64_t *vm = pivot.col_valid_mask.data();
            int VW = (int)pivot.col_valid_mask.size();
            long long pruned_now = 0;
            for (int w = 0; w < W; w++) {
                uint64_t alive_w = alive[w];
                if (!alive_w) continue;
                uint64_t valid_w = (w < VW) ? vm[w] : 0ULL;
                if (!(alive_w & valid_w)) continue;

                uint64_t outside = 0ULL;
                int base = w << 6;
                int limit = (M - base < 64) ? (M - base) : 64;
#if defined(__AVX__)
                {
                    const __m256 qv = _mm256_set1_ps(qdist);
                    const __m256 rv = _mm256_set1_ps(radius);
                    const __m256 sign_mask = _mm256_set1_ps(-0.0f);
                    const float * __restrict__ pc = col + base;
                    int b = 0;
                    for (; b + 8 <= limit; b += 8) {
                        __m256 dv = _mm256_loadu_ps(pc + b);
                        __m256 diff = _mm256_sub_ps(dv, qv);
                        diff = _mm256_andnot_ps(sign_mask, diff);
                        __m256 cmp = _mm256_cmp_ps(diff, rv, _CMP_GT_OQ);
                        uint32_t bits8 = (uint32_t)_mm256_movemask_ps(cmp) & 0xFFu;
                        outside |= ((uint64_t)bits8) << b;
                    }
                    for (; b < limit; b++) {
                        float d = pc[b];
                        float diff = d - qdist;
                        if (diff < 0.0f) diff = -diff;
                        outside |= (uint64_t)(diff > radius) << b;
                    }
                }
#else
                for (int b = 0; b < limit; b++) {
                    float d = col[base + b];
                    float diff = d - qdist;
                    if (diff < 0.0f) diff = -diff;
                    outside |= (uint64_t)(diff > radius) << b;
                }
#endif
                outside &= valid_w;
                uint64_t kill = outside & alive_w;
                if (kill) {
                    alive[w] = alive_w & ~kill;
                    pruned_now += __builtin_popcountll(kill);
                }
            }
            alive_count -= pruned_now;
            if (pruned_now > 0) {
                filter_count += pruned_now;
                pivot.pruned += pruned_now;
                local_pivot_pruned += pruned_now;
            }
        }



        if (checked_dists.capacity() < (size_t)min<long long>((long long)object_count, max(1024LL, alive_count)))
            checked_dists.reserve((size_t)min<long long>((long long)object_count, max(1024LL, alive_count)));
        const float *query_ptr = query.data();
        for (int w = 0; w < W; w++) {
            uint64_t bits = alive[w];
            while (bits) {
                int b = __builtin_ctzll(bits);
                bits &= bits - 1;
                int i = (w << 6) + b;
                if (i >= M) break;
                int id = region->objects[i];
                if (check_shadow_counterfactual) {
                    if (shadow_side_stamp[id] == region->shadow.side_token) {
                        unsigned char side = shadow_side_value[id];
                        bool shadow_would_skip =
                            (side == 0 && !region->shadow.eval_visit_left) ||
                            (side == 1 && !region->shadow.eval_visit_right);
                        if (shadow_would_skip) {
                            region->shadow.counterfactual_saved_true_checks++;
                            region->shadow.net += 1.0;
                        }
                    }
                }
                float dist = distComputePointPtr(id, query_ptr);
                checked_dists.push_back({id, dist});
                local_checked++;
                if (dist <= radius) {
                    stats.result++;
                    local_answers++;
                } else {
                    local_fp++;
                    if ((RECAST_RESIDUAL_PROMOTION || timeAwareResidualEnabled()) && processing_residual_bucket)
                        region->residual_fp_counts[id]++;
                }
            }
        }
    } else
#endif
    {































        int alive_token = nextCandidateAliveToken();

















#ifdef RA_COLUMN_ORB_EXECUTOR
        bool use_seed_mode = false;
        int seed_active_idx = -1;
        int seed_local_pos_token = 0;
        int seed_M = object_count;
        int seed_W = (seed_M + 63) >> 6;
        uint64_t * __restrict__ alive_ptr = nullptr;
        if (RECAST_DIAGNOSTICS) {
            g_recast_diag_leaf_invocations++;
            g_recast_diag_M_total += seed_M;
            g_recast_diag_M_count++;
            if (active_limit == 0) {
                g_recast_diag_active_empty++;
            } else if (region->pivots[active_pivots[0]].memory_pairs == (long long)seed_M) {
                g_recast_diag_eligible_at_0++;
            } else {
                bool found_elsewhere = false;
                for (int i = 1; i < active_limit; i++) {
                    if (region->pivots[active_pivots[i]].memory_pairs == (long long)seed_M) {
                        found_elsewhere = true; break;
                    }
                }
                if (found_elsewhere) g_recast_diag_eligible_elsewhere++;
                else g_recast_diag_no_eligible++;
            }
        }
        if (SEED_PIVOT_ENABLED && active_limit > 0 && seed_M > 0) {





            if (region->pivots[active_pivots[0]].memory_pairs == (long long)seed_M) {
                seed_active_idx = 0;
            }
            if (seed_active_idx >= 0) {
                use_seed_mode = true;
                seed_local_pos_token = ensureRegionLocalPos(region);
                vector<uint64_t> &alive_vec = region->alive_mask_ws;
                if ((int)alive_vec.size() < seed_W) alive_vec.resize(seed_W);
                alive_ptr = alive_vec.data();
                if (seed_W > 0) memset(alive_ptr, 0, (size_t)seed_W * sizeof(uint64_t));
            }
        }
        auto candidateAlive = [&](int id) -> bool {
            if (use_seed_mode) {
                if (object_local_pos_stamp[id] != seed_local_pos_token) return false;
                int pos = object_local_pos[id];
                return (alive_ptr[pos >> 6] >> (pos & 63)) & 1ULL;
            }
            return candidate_alive_stamp[id] != alive_token;
        };
        auto markCandidateDead = [&](int id) {
            if (use_seed_mode) {
                if (object_local_pos_stamp[id] == seed_local_pos_token) {
                    int pos = object_local_pos[id];
                    alive_ptr[pos >> 6] &= ~(1ULL << (pos & 63));
                }
                return;
            }
            candidate_alive_stamp[id] = alive_token;
        };
#else
        auto candidateAlive = [&](int id) {
            return candidate_alive_stamp[id] != alive_token;
        };
        auto markCandidateDead = [&](int id) {
            candidate_alive_stamp[id] = alive_token;
        };
#endif
        long long alive_count = object_count;

#ifdef RA_COLUMN_ORB_DEBUG


        vector<pair<long long, long long>> dt_pivot_snapshot;
        dt_pivot_snapshot.reserve(active_limit);
        for (int dt_k = 0; dt_k < active_limit; dt_k++) {
            const PivotEvidence &dt_pv = region->pivots[active_pivots[dt_k]];
            dt_pivot_snapshot.push_back({dt_pv.query_dist_cost, dt_pv.pruned});
        }
        long long dt_filter_count_before = filter_count;
#endif





        long long early_exit_floor = 0;
        if (PIVOT_EARLY_EXIT_ABS > 0 || PIVOT_EARLY_EXIT_RATIO > 0.0) {
            long long abs_floor = (long long)PIVOT_EARLY_EXIT_ABS;
            long long ratio_floor = (long long)(PIVOT_EARLY_EXIT_RATIO * (double)object_count);
            early_exit_floor = max(abs_floor, ratio_floor);
        }

#ifdef RA_COLUMN_ORB_EXECUTOR
        chrono::steady_clock::time_point _ts_seed;
        if (RECAST_DIAGNOSTICS >= 2) _ts_seed = chrono::steady_clock::now();
        if (use_seed_mode) {

            int p = active_pivots[seed_active_idx];
            PivotEvidence &pivot = region->pivots[p];
            float qdist = distCompute(query, pivot.center);
            pivot.query_dist_cost++;
            pivot.last_used_region_visit = region->visits;
            float low = qdist - radius;
            float high = qdist + radius;
            if (low <= pivot.min_dist && high >= pivot.max_dist) {

                for (int w = 0; w < seed_W; w++) alive_ptr[w] = ~0ULL;
                if (seed_M & 63) alive_ptr[seed_W - 1] = (1ULL << (seed_M & 63)) - 1ULL;

                if (RECAST_DIAGNOSTICS >= 3) rt_inside_band = seed_M;
            } else {
                ensurePivotSorted(pivot);
                auto inside_start = lower_bound(pivot.dist_sorted.begin(),
                                                pivot.dist_sorted.end(),
                                                make_pair(low, INT_MIN));
                auto inside_end = upper_bound(pivot.dist_sorted.begin(),
                                              pivot.dist_sorted.end(),
                                              make_pair(high, INT_MAX));
                long long inside_count = 0;
                for (auto it = inside_start; it != inside_end; ++it) {
                    int id = it->second;
                    if (object_local_pos_stamp[id] != seed_local_pos_token) continue;
                    int pos = object_local_pos[id];
                    alive_ptr[pos >> 6] |= (1ULL << (pos & 63));
                    inside_count++;
                }
                long long seed_pruned = (long long)seed_M - inside_count;
                if (seed_pruned > 0) {
                    filter_count += seed_pruned;
                    pivot.pruned += seed_pruned;
                    local_pivot_pruned += seed_pruned;
                }
                alive_count = inside_count;
                if (RECAST_DIAGNOSTICS) {
                    g_recast_diag_inside_band_total += inside_count;
                    g_recast_diag_inside_band_seed_count++;
                }
                if (RECAST_DIAGNOSTICS >= 3) rt_inside_band = inside_count;
            }
            if (RECAST_DIAGNOSTICS) g_recast_diag_seed_triggered++;
            if (RECAST_DIAGNOSTICS >= 3) rt_seed_triggered = 1;
        }
        if (RECAST_DIAGNOSTICS >= 2)
            g_recast_time_seed_init_ns += chrono::duration_cast<chrono::nanoseconds>(chrono::steady_clock::now() - _ts_seed).count();
#endif

        chrono::steady_clock::time_point _ts_pivot;
        if (RECAST_DIAGNOSTICS >= 2) _ts_pivot = chrono::steady_clock::now();




        const bool emit_pivot_trace = (RECAST_DIAGNOSTICS >= 3) && (g_pivot_trace_fp != nullptr);



        const bool scheduler_active = (ACTIVE_SCHEDULER_MIN > 0)
                                   && ((int)region->visits >= ACTIVE_SCHEDULER_WARMUP)
                                   && (active_limit > ACTIVE_SCHEDULER_MIN);
        if (scheduler_active && RECAST_DIAGNOSTICS) g_scheduler_active_regions++;



        if (scheduler_active) {
            int begin_reorder = ACTIVE_SCHEDULER_MIN;
            sort(active_pivots.begin() + begin_reorder,
                 active_pivots.begin() + active_limit,
                 [&](int a, int b) {
                     const PivotEvidence &pa = region->pivots[a];
                     const PivotEvidence &pb = region->pivots[b];
                     double ra = pa.roi_pruned_ema / max(1.0, pa.roi_cost_ema);
                     double rb = pb.roi_pruned_ema / max(1.0, pb.roi_cost_ema);
                     return ra > rb;
                 });
        }


        long long low_gain_threshold = max((long long)4,
                                           (long long)(object_count / 1000));
        int consec_low_gain = 0;
        int consec_zero_kill = 0;
        for (int idx = 0; idx < active_limit && alive_count > 0; idx++) {
#ifdef RA_COLUMN_ORB_EXECUTOR
            if (use_seed_mode && idx == seed_active_idx) continue;
#endif
            if (early_exit_floor > 0 && alive_count <= early_exit_floor)
                break;




            if (DYNAMIC_ZERO_KILL_PATIENCE > 0
                && DYNAMIC_ALIVE_SOFT_CAP > 0
                && consec_zero_kill >= DYNAMIC_ZERO_KILL_PATIENCE
                && alive_count <= DYNAMIC_ALIVE_SOFT_CAP) {
                if (RECAST_DIAGNOSTICS) g_dyn_early_break_hits++;
                break;
            }



            if (scheduler_active
                && idx >= ACTIVE_SCHEDULER_MIN
                && consec_low_gain >= ACTIVE_SCHEDULER_PATIENCE) {
                if (RECAST_DIAGNOSTICS) {
                    g_scheduler_break_hits++;
                    g_scheduler_pivots_skipped += (active_limit - idx);
                }
                break;
            }
            chrono::steady_clock::time_point _ts_one;
            if (emit_pivot_trace) _ts_one = chrono::steady_clock::now();
            int p = active_pivots[idx];
            PivotEvidence &pivot = region->pivots[p];
            long long pivot_M_snap = pivot.memory_pairs;
            bool has_alive_covered = pivot_M_snap > 0 && alive_count == object_count;
            if (!has_alive_covered) {
                for (const auto &entry : pivot.dist_sorted) {
                    int id = entry.second;
                    if (candidateAlive(id)) {
                        has_alive_covered = true;
                        break;
                    }
                }
            }
            if (!has_alive_covered) {



                if (emit_pivot_trace) {
                    long long _one_ns = chrono::duration_cast<chrono::nanoseconds>(
                        chrono::steady_clock::now() - _ts_one).count();
                    fprintf(g_pivot_trace_fp,
                            "%d\t%d\t%d\t%d\t%d\t%d\t%lld\t0\t0\t0\t%lld\n",
                            query_id, region->id, idx, p, pivot.birth_query, pivot.pool,
                            pivot_M_snap, _one_ns);
                }
                continue;
            }
            float qdist = distCompute(query, pivot.center);
            pivot.query_dist_cost++;
            pivot.last_used_region_visit = region->visits;
            float low = qdist - radius;
            float high = qdist + radius;
            float boundary_eps = 1e-5f * max(1.0f, max(fabs(low), fabs(high)));
            if (low <= pivot.min_dist && high >= pivot.max_dist) {



                consec_zero_kill++;


                if (scheduler_active || ACTIVE_SCHEDULER_MIN > 0) {
                    double a = ACTIVE_SCHEDULER_EMA_ALPHA;
                    pivot.roi_pruned_ema = (1.0 - a) * pivot.roi_pruned_ema;
                    pivot.roi_cost_ema = a * 1.0 + (1.0 - a) * pivot.roi_cost_ema;
                    pivot.roi_samples++;
                }
                if (scheduler_active && idx >= ACTIVE_SCHEDULER_MIN) {

                    consec_low_gain++;
                }
                if (emit_pivot_trace) {
                    long long _one_ns = chrono::duration_cast<chrono::nanoseconds>(
                        chrono::steady_clock::now() - _ts_one).count();
                    fprintf(g_pivot_trace_fp,
                            "%d\t%d\t%d\t%d\t%d\t%d\t%lld\t1\t0\t0\t%lld\n",
                            query_id, region->id, idx, p, pivot.birth_query, pivot.pool,
                            pivot_M_snap, _one_ns);
                }
                continue;
            }

            ensurePivotSorted(pivot);
            float low_scan = low + boundary_eps;
            float high_scan = high - boundary_eps;
            auto low_begin = pivot.dist_sorted.begin();
            auto low_end = lower_bound(pivot.dist_sorted.begin(), pivot.dist_sorted.end(),
                                       make_pair(low_scan, INT_MIN));
            auto high_begin = upper_bound(pivot.dist_sorted.begin(), pivot.dist_sorted.end(),
                                          make_pair(high_scan, INT_MAX));
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
            prune_range(low_begin, low_end);
            prune_range(high_begin, pivot.dist_sorted.end());
            alive_count -= pruned_now;
            long long scan_entries = (long long)(low_end - low_begin)
                                   + (long long)(pivot.dist_sorted.end() - high_begin);
            if (pruned_now > 0) {
                filter_count += pruned_now;
                pivot.pruned += pruned_now;
                local_pivot_pruned += pruned_now;
                consec_zero_kill = 0;
            } else {
                consec_zero_kill++;
            }



            if (scheduler_active || ACTIVE_SCHEDULER_MIN > 0) {
                double a = ACTIVE_SCHEDULER_EMA_ALPHA;
                double cost_proxy = 1.0 + (double)scan_entries + (double)pruned_now;
                pivot.roi_pruned_ema = a * (double)pruned_now + (1.0 - a) * pivot.roi_pruned_ema;
                pivot.roi_cost_ema = a * cost_proxy + (1.0 - a) * pivot.roi_cost_ema;
                pivot.roi_samples++;
            }
            if (scheduler_active && idx >= ACTIVE_SCHEDULER_MIN) {
                if (pruned_now < low_gain_threshold) consec_low_gain++;
                else consec_low_gain = 0;
            }
            if (emit_pivot_trace) {
                long long _one_ns = chrono::duration_cast<chrono::nanoseconds>(
                    chrono::steady_clock::now() - _ts_one).count();
                fprintf(g_pivot_trace_fp,
                        "%d\t%d\t%d\t%d\t%d\t%d\t%lld\t1\t%lld\t%lld\t%lld\n",
                        query_id, region->id, idx, p, pivot.birth_query, pivot.pool,
                        pivot_M_snap, pruned_now, scan_entries, _one_ns);
            }
            if (RECAST_DIAGNOSTICS >= 2) {
                g_recast_count_pivot_entries_scanned += (long long)(low_end - low_begin) + (long long)(pivot.dist_sorted.end() - high_begin);
                g_recast_count_objects_marked_dead += pruned_now;
            }
        }
        if (RECAST_DIAGNOSTICS >= 2) {
            long long _pf_ns = chrono::duration_cast<chrono::nanoseconds>(chrono::steady_clock::now() - _ts_pivot).count();
            g_recast_time_pivot_filter_ns += _pf_ns;
            if (RECAST_DIAGNOSTICS >= 3) rt_pivot_filter_ns = _pf_ns;
            g_recast_count_survivor_total += alive_count;
        }

        if (checked_dists.capacity() < (size_t)min<long long>((long long)object_count, max(1024LL, alive_count)))
            checked_dists.reserve((size_t)min<long long>((long long)object_count, max(1024LL, alive_count)));
        const float *query_ptr = query.data();
        chrono::steady_clock::time_point _ts_survivor;
        if (RECAST_DIAGNOSTICS >= 2) _ts_survivor = chrono::steady_clock::now();
#ifdef RA_COLUMN_ORB_EXECUTOR
        if (use_seed_mode) {

            for (int w = 0; w < seed_W; w++) {
                uint64_t bits = alive_ptr[w];
                while (bits) {
                    int b = __builtin_ctzll(bits);
                    bits &= bits - 1;
                    int i = (w << 6) + b;
                    if (i >= seed_M) break;
                    int id = region->objects[i];
                    if (check_shadow_counterfactual) {
                        if (shadow_side_stamp[id] == region->shadow.side_token) {
                            unsigned char side = shadow_side_value[id];
                            bool shadow_would_skip =
                                (side == 0 && !region->shadow.eval_visit_left) ||
                                (side == 1 && !region->shadow.eval_visit_right);
                            if (shadow_would_skip) {
                                region->shadow.counterfactual_saved_true_checks++;
                                region->shadow.net += 1.0;
                            }
                        }
                    }
                    float dist = distComputePointPtr(id, query_ptr);
                    checked_dists.push_back({id, dist});
                    local_checked++;
                    if (dist <= radius) {
                        stats.result++;
                        local_answers++;
                    } else {
                        local_fp++;
                        if ((RECAST_RESIDUAL_PROMOTION || timeAwareResidualEnabled()) && processing_residual_bucket)
                            region->residual_fp_counts[id]++;
                    }
                }
            }
        } else
#endif
        {
            for (int id : region->objects) {
                if (!candidateAlive(id))
                    continue;
                if (check_shadow_counterfactual) {
                    if (shadow_side_stamp[id] == region->shadow.side_token) {
                        unsigned char side = shadow_side_value[id];
                        bool shadow_would_skip =
                            (side == 0 && !region->shadow.eval_visit_left) ||
                            (side == 1 && !region->shadow.eval_visit_right);
                        if (shadow_would_skip) {
                            region->shadow.counterfactual_saved_true_checks++;
                            region->shadow.net += 1.0;
                        }
                    }
                }
                float dist = distComputePointPtr(id, query_ptr);
                checked_dists.push_back({id, dist});
                local_checked++;
                if (dist <= radius) {
                    stats.result++;
                    local_answers++;
                } else {
                    local_fp++;
                    if ((RECAST_RESIDUAL_PROMOTION || timeAwareResidualEnabled()) && processing_residual_bucket)
                        region->residual_fp_counts[id]++;
                }
            }
        }

#ifdef RA_COLUMN_ORB_DEBUG



        if (g_use_column_orb)
            dualTrackVerifyColumnPath(region, query, radius, active_pivots, active_limit,
                                      dt_pivot_snapshot, dt_filter_count_before,
                                      alive_count, checked_dists, query_id);
#endif
        if (RECAST_DIAGNOSTICS >= 2) {
            long long _sv_ns = chrono::duration_cast<chrono::nanoseconds>(chrono::steady_clock::now() - _ts_survivor).count();
            g_recast_time_survivor_enum_ns += _sv_ns;
            if (RECAST_DIAGNOSTICS >= 3) rt_survivor_ns = _sv_ns;
        }
    }







    if (RECAST_DIAGNOSTICS >= 3 && g_region_trace_fp) {
        long long evidence_pairs = countEvidencePairs(g_root_region);
        fprintf(g_region_trace_fp,
                "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\n",
                query_id, region->id, region->depth, object_count,
                region->visits, active_limit,
                rt_seed_triggered, rt_inside_band,
                local_checked, local_fp, local_answers,
                local_pivot_pruned,
                rt_pivot_filter_ns, rt_survivor_ns,
                evidence_pairs, evidence_pairs);
    }

    stats.checked += local_checked;
    if (processing_residual_bucket) {
    }

    chrono::steady_clock::time_point _ts_growth;
    if (RECAST_DIAGNOSTICS >= 2) _ts_growth = chrono::steady_clock::now();
    bool had_ema = region->ema_ready;
    double old_checked_ema = region->checked_ema;
    updateRegionStats(region, local_checked, local_fp, local_answers);
    maybeGrowRegion(region, query, radius, checked_dists, local_checked, local_fp,
                    local_answers, local_pivot_pruned, old_checked_ema, had_ema, query_id, stats);
    if ((RECAST_RESIDUAL_PROMOTION || timeAwareResidualEnabled()) && processing_residual_bucket)
        promoteResidualObjects(region, query, radius, query_id, stats);
    if (POLICY_MODE == 3 && region->isLeaf() &&
        (RECAST_SHADOW_FROM_PIVOTS || RECAST_WORKLOAD_THRESHOLD))
        rememberRecentObservation(region, query, radius, checked_dists);
    if (POLICY_MODE >= 2)
        mergeBadSplit(region, stats);
    if (RECAST_DIAGNOSTICS >= 2) {
        g_recast_time_growth_ns += chrono::duration_cast<chrono::nanoseconds>(chrono::steady_clock::now() - _ts_growth).count();
        g_recast_count_growth_trigger++;
    }
}
