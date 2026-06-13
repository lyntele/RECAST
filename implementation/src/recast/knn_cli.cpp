void usage_knn(const char *prog) {
    cerr << "Usage: " << prog
         << " [optional: -k neighbors -l lite_dispatcher -c seed_cache -d direct_crack -t leaf_threshold -p pivot_budget -Q active_pivots]"
         << " data.txt queries.txt\n";
    cerr << "The query file uses the range-query format; the radius column is ignored.\n";
}

int recast::run_knn_cli(int argc, char **argv) {
    int k = 10;
    int opt;
    while ((opt = getopt(argc, argv, "k:l:c:d:t:p:Q:G:r:K:A:S:D:g:u:m:j:")) != -1) {
        switch (opt) {
            case 'k': k = atoi(optarg); break;
            case 'l': KNN_LITE_DISPATCHER = atoi(optarg) != 0; break;
            case 'c': KNN_SEED_CACHE_SIZE = atoi(optarg); break;
            case 'd': KNN_DIRECT_CRACK = atoi(optarg) != 0; break;
            case 't': LEAF_THRESHOLD = atoi(optarg); break;
            case 'p': REGION_PIVOT_MAX = atoi(optarg); break;
            case 'Q': QUERY_ACTIVE_PIVOTS = atoi(optarg); break;
            case 'G': POLICY_MODE = atoi(optarg); break;
            case 'r': srand(atoi(optarg)); break;
            case 'K': RECAST_KEEP_PARENT_EVIDENCE = atoi(optarg) != 0; break;
            case 'A': RECAST_RESIDUAL_CHILD = atoi(optarg) != 0; break;
            case 'S': SEED_PIVOT_ENABLED = atoi(optarg) != 0 ? 1 : 0; break;
            case 'D': ADAPTIVE_BUDGET_K = atoi(optarg); break;
            case 'g': SPLIT_RESIDUAL_GUARD = atof(optarg); break;
            case 'u': PIVOT_POOL_POLICY = atoi(optarg); break;
            case 'm': FORCE_PIVOT_ADMISSION = atoi(optarg) != 0; break;
            case 'j': SPLIT_POLICY_MODE = atoi(optarg); break;
            default:
                usage_knn(argv[0]);
                return 1;
        }
    }
    if (argc - optind < 2 || k <= 0) {
        usage_knn(argv[0]);
        return 1;
    }

    loadData(argv[optind]);
    loadQueries(argv[optind + 1]);

    vector<int> root_objects;
    Region *root = nullptr;
    vector<int> lite_ids;
    vector<float> lite_dists;
    KnnLiteNode *lite_root = nullptr;
    if (KNN_LITE_DISPATCHER) {
        lite_ids.resize(numPoints);
        iota(lite_ids.begin(), lite_ids.end(), 0);
        lite_dists.assign(numPoints, 0.0f);
        lite_root = newKnnLiteNode(0, numPoints - 1, 0);
    } else {
        root_objects.resize(numPoints);
        for (int i = 0; i < numPoints; i++)
            root_objects[i] = i;
        root = newRegion(root_objects, 0);
        g_root_region = root;
    }

    cout << "Results\tQueryTime\tDC\tKthDist" << endl;
    for (int q = 0; q < numQueries; q++) {
        dc_count = 0;
        filter_count = 0;
        leaf_points = 0;
        QueryStats stats;
        priority_queue<KnnHit, vector<KnnHit>, KnnHitWorse> topk;

        auto start = chrono::steady_clock::now();
        if (KNN_LITE_DISPATCHER) {
            knnLiteQuery(lite_root, lite_ids, lite_dists, queries[q], k, topk);
            stats.result = (int)topk.size();
        } else {
            knnQuery(root, queries[q], k, q, stats, topk);
        }
        auto end = chrono::steady_clock::now();
        double elapsed = chrono::duration<double>(end - start).count();
        float kth = currentKnnRadius(topk, k);
        if (kth == FLT_MAX)
            kth = 0.0f;
        else if (KNN_LITE_DISPATCHER)
            kth = sqrt(kth);

        cout << stats.result << "\t"
             << elapsed << "\t"
             << dc_count << "\t"
             << kth << endl;
    }

    deleteRegion(root);
    deleteKnnLiteNode(lite_root);
    return 0;
}
