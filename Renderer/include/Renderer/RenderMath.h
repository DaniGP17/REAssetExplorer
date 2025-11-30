#ifndef REASSETEXPLORER_RENDERMATH_H
#define REASSETEXPLORER_RENDERMATH_H
#include <cmath>

// Row-major, row-vector convention: v * M.
struct Mat4 {
    float m[16];
};

inline Mat4 Identity() {
    return Mat4{{ 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 }};
}

inline Mat4 Mul(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            for (int k = 0; k < 4; k++) {
                r.m[i * 4 + j] += a.m[i * 4 + k] * b.m[k * 4 + j];
            }
        }
    }
    return r;
}

inline Mat4 ComposeTRS(const float t[3], const float q[4], const float s[3]) {
    float x = q[0], y = q[1], z = q[2], w = q[3];
    float xx = 2 * x * x, yy = 2 * y * y, zz = 2 * z * z;
    float xy = 2 * x * y, xz = 2 * x * z, yz = 2 * y * z;
    float wx = 2 * w * x, wy = 2 * w * y, wz = 2 * w * z;
    return Mat4{{
        s[0] * (1 - yy - zz), s[0] * (xy + wz),     s[0] * (xz - wy),     0,
        s[1] * (xy - wz),     s[1] * (1 - xx - zz), s[1] * (yz + wx),     0,
        s[2] * (xz + wy),     s[2] * (yz - wx),     s[2] * (1 - xx - yy), 0,
        t[0],                 t[1],                 t[2],                 1
    }};
}

inline void ToFloat3x4(const Mat4& m, float out[12]) {
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 4; c++) {
            out[r * 4 + c] = m.m[c * 4 + r];
        }
    }
}

inline void TransformPoint(const Mat4& m, const float p[3], float out[3]) {
    for (int c = 0; c < 3; c++) {
        out[c] = p[0] * m.m[c] + p[1] * m.m[4 + c] + p[2] * m.m[8 + c] + m.m[12 + c];
    }
}

inline void TransformVector(const Mat4& m, const float v[3], float out[3]) {
    for (int c = 0; c < 3; c++) out[c] = v[0] * m.m[c] + v[1] * m.m[4 + c] + v[2] * m.m[8 + c];
}

inline Mat4 Translation(const float t[3]) {
    Mat4 m = Identity();
    m.m[12] = t[0];
    m.m[13] = t[1];
    m.m[14] = t[2];
    return m;
}

// Inverse of a matrix whose last column is (0, 0, 0, 1); identity when singular.
inline Mat4 InverseAffine(const Mat4& m) {
    const float* a = m.m;
    float c00 = a[5] * a[10] - a[6] * a[9], c01 = a[2] * a[9] - a[1] * a[10], c02 = a[1] * a[6] - a[2] * a[5];
    float c10 = a[6] * a[8] - a[4] * a[10], c11 = a[0] * a[10] - a[2] * a[8], c12 = a[2] * a[4] - a[0] * a[6];
    float c20 = a[4] * a[9] - a[5] * a[8], c21 = a[1] * a[8] - a[0] * a[9], c22 = a[0] * a[5] - a[1] * a[4];
    float det = a[0] * c00 + a[1] * c10 + a[2] * c20;
    if (std::fabs(det) < 1e-20f) return Identity();
    float inv = 1.0f / det;
    Mat4 r{{ c00 * inv, c01 * inv, c02 * inv, 0,
             c10 * inv, c11 * inv, c12 * inv, 0,
             c20 * inv, c21 * inv, c22 * inv, 0,
             0, 0, 0, 1 }};
    for (int c = 0; c < 3; c++) r.m[12 + c] = -(a[12] * r.m[c] + a[13] * r.m[4 + c] + a[14] * r.m[8 + c]);
    return r;
}

// The inverse of ComposeTRS; a mirrored matrix gets a negative x scale.
inline void DecomposeTRS(const Mat4& m, float t[3], float q[4], float s[3]) {
    float r[3][3];
    for (int i = 0; i < 3; i++) {
        t[i] = m.m[12 + i];
        s[i] = std::sqrt(m.m[i * 4] * m.m[i * 4] + m.m[i * 4 + 1] * m.m[i * 4 + 1] + m.m[i * 4 + 2] * m.m[i * 4 + 2]);
    }
    float det = m.m[0] * (m.m[5] * m.m[10] - m.m[6] * m.m[9]) - m.m[1] * (m.m[4] * m.m[10] - m.m[6] * m.m[8]) +
                m.m[2] * (m.m[4] * m.m[9] - m.m[5] * m.m[8]);
    if (det < 0) s[0] = -s[0];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) r[i][j] = std::fabs(s[i]) > 1e-12f ? m.m[i * 4 + j] / s[i] : (i == j ? 1.0f : 0.0f);
    }
    // r holds rows (row vectors); the column-vector rotation is its transpose.
    auto c = [&](int i, int j) { return r[j][i]; };
    float trace = c(0, 0) + c(1, 1) + c(2, 2);
    if (trace > 0) {
        float k = 0.5f / std::sqrt(trace + 1.0f);
        q[3] = 0.25f / k;
        q[0] = (c(2, 1) - c(1, 2)) * k;
        q[1] = (c(0, 2) - c(2, 0)) * k;
        q[2] = (c(1, 0) - c(0, 1)) * k;
    } else if (c(0, 0) > c(1, 1) && c(0, 0) > c(2, 2)) {
        float k = 2.0f * std::sqrt(1.0f + c(0, 0) - c(1, 1) - c(2, 2));
        q[3] = (c(2, 1) - c(1, 2)) / k;
        q[0] = 0.25f * k;
        q[1] = (c(0, 1) + c(1, 0)) / k;
        q[2] = (c(0, 2) + c(2, 0)) / k;
    } else if (c(1, 1) > c(2, 2)) {
        float k = 2.0f * std::sqrt(1.0f + c(1, 1) - c(0, 0) - c(2, 2));
        q[3] = (c(0, 2) - c(2, 0)) / k;
        q[0] = (c(0, 1) + c(1, 0)) / k;
        q[1] = 0.25f * k;
        q[2] = (c(1, 2) + c(2, 1)) / k;
    } else {
        float k = 2.0f * std::sqrt(1.0f + c(2, 2) - c(0, 0) - c(1, 1));
        q[3] = (c(1, 0) - c(0, 1)) / k;
        q[0] = (c(0, 2) + c(2, 0)) / k;
        q[1] = (c(1, 2) + c(2, 1)) / k;
        q[2] = 0.25f * k;
    }
}

// XYZ euler angles in degrees as the inspector shows them (q = qz * qy * qx).
inline void QuatToEulerDegrees(const float q[4], float out[3]) {
    constexpr float RAD_TO_DEG = 57.2957795f;
    float x = q[0], y = q[1], z = q[2], w = q[3];
    float sinp = 2 * (w * y - z * x);
    out[0] = std::atan2(2 * (w * x + y * z), 1 - 2 * (x * x + y * y)) * RAD_TO_DEG;
    out[1] = (std::fabs(sinp) >= 1 ? std::copysign(1.5707963f, sinp) : std::asin(sinp)) * RAD_TO_DEG;
    out[2] = std::atan2(2 * (w * z + x * y), 1 - 2 * (y * y + z * z)) * RAD_TO_DEG;
}

inline void EulerDegreesToQuat(const float euler[3], float q[4]) {
    constexpr float HALF_DEG_TO_RAD = 3.14159265f / 360.0f;
    float cr = std::cos(euler[0] * HALF_DEG_TO_RAD), sr = std::sin(euler[0] * HALF_DEG_TO_RAD);
    float cp = std::cos(euler[1] * HALF_DEG_TO_RAD), sp = std::sin(euler[1] * HALF_DEG_TO_RAD);
    float cy = std::cos(euler[2] * HALF_DEG_TO_RAD), sy = std::sin(euler[2] * HALF_DEG_TO_RAD);
    q[0] = sr * cp * cy - cr * sp * sy;
    q[1] = cr * sp * cy + sr * cp * sy;
    q[2] = cr * cp * sy - sr * sp * cy;
    q[3] = cr * cp * cy + sr * sp * sy;
}

// RE Engine scenes are right-handed; a LH view/proj would mirror the image.
inline Mat4 LookAtRH(const float eye[3], const float target[3], const float up[3]) {
    auto dot = [](const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    auto normalize = [](float* v) {
        float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        v[0] /= len; v[1] /= len; v[2] /= len;
    };

    float f[3] = { eye[0] - target[0], eye[1] - target[1], eye[2] - target[2] };
    normalize(f);
    float r[3] = { up[1] * f[2] - up[2] * f[1], up[2] * f[0] - up[0] * f[2], up[0] * f[1] - up[1] * f[0] };
    normalize(r);
    float u[3] = { f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0] };

    return Mat4{{
        r[0], u[0], f[0], 0,
        r[1], u[1], f[1], 0,
        r[2], u[2], f[2], 0,
        -dot(r, eye), -dot(u, eye), -dot(f, eye), 1
    }};
}

inline Mat4 PerspectiveFovRH(float fovY, float aspect, float zn, float zf) {
    float ys = 1.0f / std::tan(fovY * 0.5f);
    float xs = ys / aspect;
    return Mat4{{
        xs, 0, 0, 0,
        0, ys, 0, 0,
        0, 0, zf / (zn - zf), -1,
        0, 0, zn * zf / (zn - zf), 0
    }};
}

#endif
