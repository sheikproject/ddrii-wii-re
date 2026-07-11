#include "runtime/math.h"

void Matrix34_SetIdentity(float *matrix34) {
    /* 0x801B0120 initializes a 3x4 matrix to identity.

       Layout:
       [1,0,0,0]
       [0,1,0,0]
       [0,0,1,0] */
    if (matrix34 == 0) {
        return;
    }

    matrix34[0] = 1.0f;
    matrix34[1] = 0.0f;
    matrix34[2] = 0.0f;
    matrix34[3] = 0.0f;
    matrix34[4] = 0.0f;
    matrix34[5] = 1.0f;
    matrix34[6] = 0.0f;
    matrix34[7] = 0.0f;
    matrix34[8] = 0.0f;
    matrix34[9] = 0.0f;
    matrix34[10] = 1.0f;
    matrix34[11] = 0.0f;
}

void Matrix34_Copy(float *dest, const float *src) {
    int i;

    /* 0x801459EC is a tiny wrapper around FUN_801B0150(src, dest). All known
       callers use it as a 3x4 matrix copy. */
    if (dest == 0 || src == 0) {
        return;
    }

    for (i = 0; i < 12; i++) {
        dest[i] = src[i];
    }
}

void Matrix44_Copy(float *dest, const float *src) {
    int i;

    /* 0x801459FC wraps FUN_801B0E00(src, dest). The leaf copies 16 words, so keep it
       separate from the 3x4 matrix helpers. */
    if (dest == 0 || src == 0) {
        return;
    }

    for (i = 0; i < 16; i++) {
        dest[i] = src[i];
    }
}

void Matrix34_Multiply(float *dest, const float *lhs, const float *rhs) {
    /* 0x80145A0C is a tiny wrapper around FUN_801B0190(lhs, rhs, dest). Callers use
       it to compose parent/object 3x4 transforms. */
    float tmp[12];
    int row;
    int col;

    if (dest == 0 || lhs == 0 || rhs == 0) {
        return;
    }

    for (row = 0; row < 3; row++) {
        for (col = 0; col < 3; col++) {
            tmp[row * 4 + col] =
                lhs[row * 4 + 0] * rhs[col + 0] +
                lhs[row * 4 + 1] * rhs[col + 4] +
                lhs[row * 4 + 2] * rhs[col + 8];
        }
        tmp[row * 4 + 3] =
            lhs[row * 4 + 0] * rhs[3] +
            lhs[row * 4 + 1] * rhs[7] +
            lhs[row * 4 + 2] * rhs[11] +
            lhs[row * 4 + 3];
    }

    Matrix34_Copy(dest, tmp);
}

void Matrix34_GetTranslation(float *outVec3, const float *matrix34) {
    /* 0x80145CA4 copies the translation column from matrix offsets
       +0x0C/+0x1C/+0x2C into a compact vec3. */
    if (outVec3 == 0 || matrix34 == 0) {
        return;
    }

    outVec3[0] = matrix34[3];
    outVec3[1] = matrix34[7];
    outVec3[2] = matrix34[11];
}

void Matrix34_SetTranslation(float *matrix34, const float *vec3) {
    /* 0x80145C88 writes a compact vec3 into the translation column at matrix
       offsets +0x0C/+0x1C/+0x2C. */
    if (matrix34 == 0 || vec3 == 0) {
        return;
    }

    matrix34[3] = vec3[0];
    matrix34[7] = vec3[1];
    matrix34[11] = vec3[2];
}
