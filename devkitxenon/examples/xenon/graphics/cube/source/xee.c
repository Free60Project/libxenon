#include "xee.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

void M_Load44(struct XenosDevice *xe, int base, eMatrix44 *matrix)
{
	Xe_SetVertexShaderConstantF(xe, base, (float*)matrix, 4);
}

void M_Load43(struct XenosDevice *xe, int base, eMatrix43 *matrix)
{
	Xe_SetVertexShaderConstantF(xe, base, (float*)matrix, 3);
}

const eMatrix44 g_ident = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};

void M_BuildPersp(eMatrix44 *m, float fovy, float aspect, float f, float n)
{
	float cot = 1.0f / tanf(fovy * 0.5F);
	float tmp = 1.0f / (f-n);

	eMatrix44 _m = {
		{cot / aspect, 0, 0, 0},
		{0, cot, 0, 0},
		{0, 0, -n * tmp, 1}, 
		{0, 0, -(f*n) * tmp, 0}};
	memcpy(m, _m, sizeof(_m));
}

void M_Dump(const char *name, eMatrix44 *m)
{
	int i, j;
	printf("-- %s:\n", name);
	for (i=0; i<4; ++i)
	{
		for (j=0; j<4; ++j)
			printf("%3.3f ", (*m)[i][j]);
		printf("\n");
	}
}

eMatrix44 g_proj;

void M_LoadMV(struct XenosDevice *xe, int where)
{
 	eMatrix44 res, worldview;
	memcpy(worldview, g_ident, sizeof(eMatrix44));
	memcpy(worldview, &matrix_stack[matrix_top], sizeof(eMatrix43));

	multiply_matrix_44(res, g_proj, worldview);

	res[0][3] = -res[0][3]; 
	res[1][3] = -res[1][3]; 
	res[2][3] = -res[2][3]; 
	res[3][3] = -res[3][3]; 
	M_Load44(xe, where, &res);
}

void M_LoadMW(struct XenosDevice *xe, int where)
{
 	eMatrix44 world;
	memcpy(world, g_ident, sizeof(eMatrix44));
	memcpy(world, &matrix_stack[matrix_top], sizeof(eMatrix43));
	M_Load44(xe, where, &world);
}
