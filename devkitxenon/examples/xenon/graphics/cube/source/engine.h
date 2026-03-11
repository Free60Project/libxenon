#ifndef __engine_h
#define __engine_h

#ifdef __cplusplus
extern "C" {
#endif

#include <math.h>
	/* first dumb try of an engine. just to test the GX functions. */

typedef float eMatrix43[3][4];
typedef float eMatrix44[4][4];
typedef float eMatrix33[3][3];
typedef float eMatrixProj[7];
typedef float eViewport[6];
typedef float eVector3[3];

void build_rot_matrix(eMatrix43 dst, float xrot, float yrot, float zrot);
void multiply_matrix(eMatrix43 dst, eMatrix43 s1, eMatrix43 s2);
void multiply_matrix_44(eMatrix44 dst, eMatrix44 s1, eMatrix44 s2);
void build_proj_ortho(eMatrixProj proj, float t, float b, float l, float r, float n, float f);
void build_proj_persp(eMatrixProj proj, float fovy, float aspect, float n, float f);
void invert_matrix(eMatrix43 dst, eMatrix43 src);
void multiply_vector(eVector3 dst, eMatrix43 mtx, eVector3 src);

extern eMatrix43 matrix_stack[128];
extern int matrix_top;

	/* my stupid matrix stack */
void glLoadIdentity();
void glTranslate(float x, float y, float z);
void glRotate(float angle, float x, float y, float z);
void glScale(float x, float y, float z);
void glMultMatrix(eMatrix43 matrix);
void glLoadMatrix(eMatrix43 matrix);

void glPushMatrix();
void glPopMatrix();
void gxLoad();

void gluLookAt(float eyex, float eyey, float eyez, 
		float centerx, float centery, float centerz,
		float upx, float upy, float upz);


	/* now some stupid, unoptimized vector stuff */

static inline void vec_zero(eVector3 vec)
{
	vec[0] = 0;
	vec[1] = 0;
	vec[2] = 0;
}

static inline void vec_copy(eVector3 dst, eVector3 src)
{
	dst[0] = src[0];
	dst[1] = src[1];
	dst[2] = src[2];
}

static inline float vec_dot(eVector3 vec1, eVector3 vec2)
{
	return vec1[0] * vec2[0] + vec1[1] * vec2[1] +vec1[2] * vec2[2];
}

static inline void vec_cross(eVector3 dst, eVector3 src1, eVector3 src2)
{
	dst[0] = src1[1] * src2[2] - src1[2] * src2[1];
	dst[1] = src1[2] * src2[0] - src1[0] * src2[2];
	dst[2] = src1[0] * src2[1] - src1[1] * src2[0];
}

static inline float vec_abs(eVector3 vec)
{
	return sqrtf(vec[0]*vec[0] + vec[1] * vec[1] + vec[2] * vec[2]);
}

static inline void vec_normalize(eVector3 dst, eVector3 src)
{
	float invlen = 1 / vec_abs(src);
	dst[0] = src[0] * invlen;
	dst[1] = src[1] * invlen;
	dst[2] = src[2] * invlen;
}

static inline void vec_scale(eVector3 dst, eVector3 src, float scale)
{
	dst[0] = src[0] * scale;
	dst[1] = src[1] * scale;
	dst[2] = src[2] * scale;
}

static inline void vec_add(eVector3 dst, eVector3 src1, eVector3 src2)
{
	dst[0] = src1[0] + src2[0];
	dst[1] = src1[1] + src2[1];
	dst[2] = src1[2] + src2[2];
}

static inline void vec_mac(eVector3 dst, eVector3 src1, eVector3 src2, float f)
{
	dst[0] = src1[0] + src2[0] * f;
	dst[1] = src1[1] + src2[1] * f;
	dst[2] = src1[2] + src2[2] * f;
}

static inline void vec_sub(eVector3 dst, eVector3 src1, eVector3 src2)
{
	dst[0] = src1[0] - src2[0];
	dst[1] = src1[1] - src2[1];
	dst[2] = src1[2] - src2[2];
}


#ifdef __cplusplus
};
#endif

#endif
