#ifndef DDRII_RUNTIME_MATH_H
#define DDRII_RUNTIME_MATH_H

void Matrix34_SetIdentity(float *matrix34);
void Matrix34_Copy(float *dest, const float *src);
void Matrix34_Multiply(float *dest, const float *lhs, const float *rhs);
void Matrix34_GetTranslation(float *outVec3, const float *matrix34);
void Matrix34_SetTranslation(float *matrix34, const float *vec3);

#endif
