static inline float distRaw(const float *__restrict__ left, const float *__restrict__ right) {
#ifdef QFD
    float result = 0.0f;
    for (int i = 0; i < dimension; i++) {
        float ai = 0.0f;
        for (int j = 0; j < dimension; j++)
            ai += qfd_matrix_flat[(long long)i * dimension + j] * (left[j] - right[j]);
        result += (left[i] - right[i]) * ai;
    }
    return sqrt(result > 0.0f ? result : 0.0f);
#else
    float total = 0.0f;
    for (int i = 0; i < dimension; i++) {
        float diff = left[i] - right[i];
        total += diff * diff;
    }
    return sqrt(total);
#endif
}

static inline float distFunc(const vector<float> &left, const vector<float> &right) {
    return distRaw(left.data(), right.data());
}

static inline float distCompute(const vector<float> &left, const vector<float> &right) {
    dc_count++;
    return distRaw(left.data(), right.data());
}

static inline float distComputePointVector(int point_id, const vector<float> &right) {
    dc_count++;
    const float *left = &points_flat[(long long)point_id * dimension];
    return distRaw(left, right.data());
}

static inline float distComputePointPtr(int point_id, const float *__restrict__ right) {
    dc_count++;
    const float *left = &points_flat[(long long)point_id * dimension];
    return distRaw(left, right);
}

static inline void retainEvidencePair(int, int) {}
static inline void releaseEvidencePair(int, int) {}

