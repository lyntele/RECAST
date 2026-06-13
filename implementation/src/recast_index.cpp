#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cfloat>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <numeric>
#include <queue>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <functional>
#include <unistd.h>
#include "recast/recast_index.h"
#if defined(__AVX__)
#include <immintrin.h>
#endif

using namespace std;





#include "recast/config_state.cpp"
#include "recast/distance_io.cpp"
#include "recast/data_model.cpp"
#include "recast/region_basics.cpp"
#include "recast/pivot_pool.cpp"
#include "recast/pivot_evidence.cpp"
#include "recast/split_partition.cpp"
#include "recast/shadow_growth.cpp"
#include "recast/query_execution.cpp"
#include "recast/knn_execution.cpp"
#include "recast/cli.cpp"
#include "recast/knn_cli.cpp"
