void usage(const char *prog) {
    cerr << "Usage: " << prog
         << " [-- optional: -t leaf_threshold -p pivot_budget -Q active_pivots]"
         << " data.txt queries.txt\n";
    cerr << "Default configuration is the final RECAST release setting: "
         << "leaf=128, K=32, active=32, residual-child on, "
         << "single-tier signal-adaptive pivot pool, "
         << "signal+shadow lazy split admission, "
         << "seed inside-band optimization on.\n";
}

int recast::run_range_cli(int argc, char **argv) {
    int opt;
    while ((opt = getopt(argc, argv, "t:p:Q:G:e:R:s:z:r:K:C:U:P:E:A:W:O:B:X:Y:Z:N:S:T:D:V:J:M:F:L:I:H:q:w:x:y:g:u:m:j:")) != -1) {
        switch (opt) {
            case 't': LEAF_THRESHOLD = atoi(optarg); break;
            case 'p': REGION_PIVOT_MAX = atoi(optarg); break;
            case 'Q': QUERY_ACTIVE_PIVOTS = atoi(optarg); break;
            case 'G': POLICY_MODE = atoi(optarg); break;
            case 'e': SPLIT_EXTRA_WEIGHT = atof(optarg); break;
            case 'R': SPLIT_RADIUS_MARGIN = atof(optarg); break;
            case 's': SPLIT_MIN_SCORE = atof(optarg); break;
            case 'z': TRACE_PATH = optarg; break;
            case 'r': srand(atoi(optarg)); break;
            case 'K': RECAST_KEEP_PARENT_EVIDENCE = atoi(optarg) != 0; break;
            case 'C': RECAST_SHADOW_FROM_PIVOTS = atoi(optarg) != 0; break;
            case 'U': RECAST_WORKLOAD_THRESHOLD = atoi(optarg) != 0; break;
            case 'P': RECAST_RESIDUAL_PROMOTION = atoi(optarg) != 0; break;
            case 'E': RECAST_EAGER_SHADOW_SPLIT = atoi(optarg) != 0; break;
            case 'A': RECAST_RESIDUAL_CHILD = atoi(optarg) != 0; break;
            case 'W': TIME_AWARE_MODE = atoi(optarg); break;
            case 'O': LIGHTWEIGHT_SPLIT_CHILDREN = atoi(optarg); break;
            case 'B': LIGHTWEIGHT_CHILD_PIVOT_CAP = atoi(optarg); break;
#ifdef RA_COLUMN_ORB_EXECUTOR
            case 'X': g_use_column_orb = atoi(optarg) != 0 ? 1 : 0; break;
            case 'Y': g_column_m_max = atoi(optarg); break;
#endif
            case 'Z': PIVOT_EARLY_EXIT_ABS = atoi(optarg); break;
            case 'N': PIVOT_EARLY_EXIT_RATIO = atof(optarg); break;
            case 'S': SEED_PIVOT_ENABLED = atoi(optarg) != 0 ? 1 : 0; break;
            case 'T': RECAST_DIAGNOSTICS = atoi(optarg); break;
            case 'D': ADAPTIVE_BUDGET_K = atoi(optarg); break;
            case 'V': PIVOT_COVERAGE_CAP = atoi(optarg); break;
            case 'J': PRACTICAL_SPLIT_M_THRESHOLD = atoi(optarg); break;
            case 'M': PRACTICAL_SPLIT_MIN_VISITS = atoi(optarg); break;
            case 'F': TRACE_REGION_PATH = optarg; break;
            case 'L': DYNAMIC_ALIVE_SOFT_CAP = atoi(optarg); break;
            case 'I': DYNAMIC_ZERO_KILL_PATIENCE = atoi(optarg); break;
            case 'H': TRACE_PIVOT_PATH = optarg; break;
            case 'q': ACTIVE_SCHEDULER_MIN = atoi(optarg); break;
            case 'w': ACTIVE_SCHEDULER_WARMUP = atoi(optarg); break;
            case 'x': TRACE_ROUTING_PATH = optarg; break;
            case 'y': TRACE_SPLIT_PATH = optarg; break;
            case 'g': SPLIT_RESIDUAL_GUARD = atof(optarg); break;
            case 'u': PIVOT_POOL_POLICY = atoi(optarg); break;
            case 'm': FORCE_PIVOT_ADMISSION = atoi(optarg) != 0; break;
            case 'j': SPLIT_POLICY_MODE = atoi(optarg); break;
            default:
                usage(argv[0]);
                return 1;
        }
    }
    if (argc - optind < 2) {
        usage(argv[0]);
        return 1;
    }

    loadData(argv[optind]);
#ifdef QFD
    initQFDMatrix(dimension, 54321);
    cout << "QFD matrix initialized (" << dimension << "x" << dimension << ")" << endl;
#endif
    loadQueries(argv[optind + 1]);

    if (RECAST_DIAGNOSTICS >= 3 && !TRACE_REGION_PATH.empty()) {
        g_region_trace_fp = fopen(TRACE_REGION_PATH.c_str(), "w");
        if (g_region_trace_fp) {
            fprintf(g_region_trace_fp,
                    "query_id\tregion_id\tdepth\tM\tvisits_before\tactive_K"
                    "\tseed_triggered\tinside_band\tsurvivor\tfp\tans"
                    "\tlocal_pivot_pruned\tpivot_filter_ns\tsurvivor_ns"
                    "\tphysical_distance_pairs\tlogical_view_pairs\n");
        } else {
            cerr << "[warn] failed to open region trace file: " << TRACE_REGION_PATH << endl;
        }
    }
    if (RECAST_DIAGNOSTICS >= 3 && !TRACE_PIVOT_PATH.empty()) {
        g_pivot_trace_fp = fopen(TRACE_PIVOT_PATH.c_str(), "w");
        if (g_pivot_trace_fp) {
            fprintf(g_pivot_trace_fp,
                    "query_id\tregion_id\tactive_idx\tpivot_id\tbirth_query\tpool\tpivot_M"
                    "\thas_alive\tpruned_now\tscan_entries\tns\n");
        } else {
            cerr << "[warn] failed to open pivot trace file: " << TRACE_PIVOT_PATH << endl;
        }
    }
    if (RECAST_DIAGNOSTICS >= 3 && !TRACE_ROUTING_PATH.empty()) {
        g_routing_trace_fp = fopen(TRACE_ROUTING_PATH.c_str(), "w");
        if (g_routing_trace_fp) {
            fprintf(g_routing_trace_fp,
                    "query_id\tparent_region_id\tsplit_visits_before"
                    "\tvisit_left\tvisit_right\tvisit_residual"
                    "\tM_left\tM_right\tM_residual\n");
        } else {
            cerr << "[warn] failed to open routing trace file: " << TRACE_ROUTING_PATH << endl;
        }
    }
    if (RECAST_DIAGNOSTICS >= 3 && !TRACE_SPLIT_PATH.empty()) {
        g_split_trace_fp = fopen(TRACE_SPLIT_PATH.c_str(), "w");
        if (g_split_trace_fp) {
            fprintf(g_split_trace_fp,
                    "query_id\tregion_id\tdepth\tn\tassigned"
                    "\test_residual_share\tforced\treject_reason\tcommitted\n");
        } else {
            cerr << "[warn] failed to open split trace file: " << TRACE_SPLIT_PATH << endl;
        }
    }

    vector<int> root_objects(numPoints);
    for (int i = 0; i < numPoints; i++)
        root_objects[i] = i;
    Region *root = newRegion(root_objects, 0);
    g_root_region = root;

    cout << "Results\tQueryTime\tDC\tFiltered\tLeafPts\tBSnarrow" << endl;
    for (int q = 0; q < numQueries; q++) {
        dc_count = 0;
        filter_count = 0;
        leaf_points = 0;
        QueryStats stats;

        auto start = chrono::steady_clock::now();
        queryRegion(root, queries[q], radii[q], q, stats);
        auto end = chrono::steady_clock::now();
        double elapsed = chrono::duration<double>(end - start).count();
        double query_ms = elapsed * 1000.0;

        cout << stats.result << "\t"
             << elapsed << "\t"
             << dc_count << "\t"
             << filter_count << "\t"
             << leaf_points << "\t"
             << 0 << endl;
    }

    deleteRegion(root);
#ifdef RA_COLUMN_ORB_EXECUTOR
    if (RECAST_DIAGNOSTICS) {
        long long leaf = g_recast_diag_leaf_invocations;
        cerr << "[recast diag]"
             << " leaf=" << leaf
             << " seed_triggered=" << g_recast_diag_seed_triggered
             << " active_empty=" << g_recast_diag_active_empty
             << " eligible_at_0=" << g_recast_diag_eligible_at_0
             << " eligible_elsewhere=" << g_recast_diag_eligible_elsewhere
             << " no_eligible=" << g_recast_diag_no_eligible;
        if (g_recast_diag_M_count > 0)
            cerr << " avg_M=" << (g_recast_diag_M_total / g_recast_diag_M_count);
        if (g_recast_diag_inside_band_seed_count > 0)
            cerr << " avg_inside_band=" << (g_recast_diag_inside_band_total / g_recast_diag_inside_band_seed_count);
        if (leaf > 0) {
            cerr << " seed_pct=" << (100.0 * (double)g_recast_diag_seed_triggered / (double)leaf);
        }
        if (DYNAMIC_ZERO_KILL_PATIENCE > 0 && DYNAMIC_ALIVE_SOFT_CAP > 0) {
            cerr << " dyn_early_break=" << g_dyn_early_break_hits
                 << " (cap=" << DYNAMIC_ALIVE_SOFT_CAP
                 << " patience=" << DYNAMIC_ZERO_KILL_PATIENCE << ")";
        }
        if (ACTIVE_SCHEDULER_MIN > 0) {
            cerr << " scheduler_active_regions=" << g_scheduler_active_regions
                 << " scheduler_breaks=" << g_scheduler_break_hits
                 << " skipped_pivots=" << g_scheduler_pivots_skipped
                 << " (min_active=" << ACTIVE_SCHEDULER_MIN
                 << " warmup=" << ACTIVE_SCHEDULER_WARMUP
                 << " patience=" << ACTIVE_SCHEDULER_PATIENCE << ")";
        }
        if (SPLIT_RESIDUAL_GUARD > 0.0) {
            cerr << " split_guard_rejects=" << g_split_guard_rejects
                 << " (threshold=" << SPLIT_RESIDUAL_GUARD << ")";
        }
        cerr << endl;
    }
    if (RECAST_DIAGNOSTICS >= 2) {
        long long total_ns = g_recast_time_routing_ns + g_recast_time_active_select_ns + g_recast_time_seed_init_ns
                           + g_recast_time_pivot_filter_ns + g_recast_time_survivor_enum_ns + g_recast_time_growth_ns;
        cerr << "[recast time_ms]"
             << " routing=" << (g_recast_time_routing_ns / 1e6)
             << " active_select=" << (g_recast_time_active_select_ns / 1e6)
             << " seed_init=" << (g_recast_time_seed_init_ns / 1e6)
             << " pivot_filter=" << (g_recast_time_pivot_filter_ns / 1e6)
             << " survivor_enum=" << (g_recast_time_survivor_enum_ns / 1e6)
             << " growth=" << (g_recast_time_growth_ns / 1e6)
             << " sum=" << (total_ns / 1e6)
             << endl;
        cerr << "[recast counters]"
             << " visited_region=" << g_recast_count_visited_region
             << " residual_region=" << g_recast_count_residual_region
             << " growth_trigger=" << g_recast_count_growth_trigger
             << " active_pivot_sum=" << g_recast_count_active_pivot_total
             << " pivot_entries_scanned=" << g_recast_count_pivot_entries_scanned
             << " objects_marked_dead=" << g_recast_count_objects_marked_dead
             << " survivor_total=" << g_recast_count_survivor_total
             << endl;
    }
#endif
    if (g_region_trace_fp) {
        fclose(g_region_trace_fp);
        g_region_trace_fp = nullptr;
    }
    if (g_pivot_trace_fp) {
        fclose(g_pivot_trace_fp);
        g_pivot_trace_fp = nullptr;
    }
    if (g_routing_trace_fp) {
        fclose(g_routing_trace_fp);
        g_routing_trace_fp = nullptr;
    }
    if (g_split_trace_fp) {
        fclose(g_split_trace_fp);
        g_split_trace_fp = nullptr;
    }
    return 0;
}
