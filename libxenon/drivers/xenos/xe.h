#ifndef __xe_h
#define __xe_h

#include <xetypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XE_MAX_INDICES_PER_DRAW 65535

#define SHADER_TYPE_PIXEL 1
#define SHADER_TYPE_VERTEX 0

#define XE_PRIMTYPE_POINTLIST 1
#define XE_PRIMTYPE_LINELIST 2
#define XE_PRIMTYPE_LINESTRIP 3
#define XE_PRIMTYPE_TRIANGLELIST 4
#define XE_PRIMTYPE_TRIANGLEFAN 5
#define XE_PRIMTYPE_TRIANGLESTRIP 6
#define XE_PRIMTYPE_RECTLIST 8
#define XE_PRIMTYPE_QUADLIST 13

#define XE_CMP_NEVER                 0
#define XE_CMP_LESS                  1
#define XE_CMP_EQUAL                 2
#define XE_CMP_LESSEQUAL             3
#define XE_CMP_GREATER               4
#define XE_CMP_NOTEQUAL              5
#define XE_CMP_GREATEREQUAL          6
#define XE_CMP_ALWAYS                7

#define XE_BLEND_ZERO                 0
#define XE_BLEND_ONE                  1
#define XE_BLEND_SRCCOLOR             4
#define XE_BLEND_INVSRCCOLOR          5
#define XE_BLEND_SRCALPHA             6
#define XE_BLEND_INVSRCALPHA          7
#define XE_BLEND_DESTCOLOR            8
#define XE_BLEND_INVDESTCOLOR         9
#define XE_BLEND_DESTALPHA           10
#define XE_BLEND_INVDESTALPHA        11
#define XE_BLEND_BLENDFACTOR         12
#define XE_BLEND_INVBLENDFACTOR      13
#define XE_BLEND_CONSTANTALPHA       14
#define XE_BLEND_INVCONSTANTALPHA    15
#define XE_BLEND_SRCALPHASAT         16

#define XE_CULL_NONE 0
#define XE_CULL_CW   2
#define XE_CULL_CCW  6

#define XE_BLENDOP_ADD           0
#define XE_BLENDOP_SUBTRACT      1
#define XE_BLENDOP_REVSUBTRACT   4
#define XE_BLENDOP_MIN           2
#define XE_BLENDOP_MAX           3

#define XE_STENCILOP_KEEP        0
#define XE_STENCILOP_ZERO        1
#define XE_STENCILOP_REPLACE     2
#define XE_STENCILOP_INCRSAT     3
#define XE_STENCILOP_DECRSAT     4
#define XE_STENCILOP_INVERT      5
#define XE_STENCILOP_INCR        6
#define XE_STENCILOP_DECR        7

#define XE_TEXADDR_WRAP                   0
#define XE_TEXADDR_MIRROR                 1
#define XE_TEXADDR_CLAMP                  2
#define XE_TEXADDR_MIRRORONCE             3
#define XE_TEXADDR_BORDER_HALF            4
#define XE_TEXADDR_MIRRORONCE_BORDER_HALF 5
#define XE_TEXADDR_BORDER                 6
#define XE_TEXADDR_MIRRORONCE_BORDER      7

#define XE_CLIP_ENABLE_PLANE0  0x0001
#define XE_CLIP_ENABLE_PLANE1  0x0002
#define XE_CLIP_ENABLE_PLANE2  0x0004
#define XE_CLIP_ENABLE_PLANE3  0x0008
#define XE_CLIP_ENABLE_PLANE4  0x0010
#define XE_CLIP_ENABLE_PLANE5  0x0020
#define XE_CLIP_MASTER_DISABLE 0x10000
	
#define XE_FILL_POINT 0x01
#define XE_FILL_WIREFRAME 0x25
#define XE_FILL_SOLID 0x00

struct XenosLock
{
	void *start;
	u32 phys;
	int size;
	int flags;
};

#define XE_SHADER_MAX_INSTANCES 16

#pragma pack(push,1)

struct XenosShader
{
	void *shader;
	u32 size;
			/* we might need more than once instance if we want to use a shader with different VBFs */
	u32 shader_phys[XE_SHADER_MAX_INSTANCES], shader_phys_size, program_control, context_misc;
	void *shader_instance[XE_SHADER_MAX_INSTANCES];
};

	/* the shader file format */
struct XenosShaderHeader
{
	u32 magic;
	u32 offset;
	
	u32 unk1[3];

	u32 off_constants, off_shader;

	u32 unk2[2];
};

struct XenosShaderData
{
	u32 sh_off, sh_size;
	u32 program_control, context_misc;
	
	u32 unk1[4];
};

struct XenosShaderVertex
{
	u32 cnt0, cnt_vfetch, cnt2;
};

#pragma pack(pop)

#define SWIZZLE_XYZW 0x688
#define SWIZZLE_XYZ1 0xA88 // 101 010 001 000
#define SWIZZLE_XY01 0xA08 // 101 000 001 000
#define SWIZZLE_XY__ 0xFC8 // 111 111 001 000
#define SWIZZLE_XYZ_ 0xEC8 // 111 010 001 000
#define SWIZZLE_XYZ0 0x0C8 // 000 010 001 000
#define SWIZZLE_XY0_ 0xE08 // 111 000 001 000

	/* each vertex buffer element fills FOUR FLOAT components.
	   the 'usage' specifies which of them (position, color, texuv, ..)
	   the 'fmt' specified in which form they lie in memory. if you 
	     specify float3, the remaining component will be filled up with
	     the 0 or 1, according to the swizzling.
	*/

#define XE_TYPE_FLOAT2    37
#define XE_TYPE_FLOAT3    57
#define XE_TYPE_FLOAT4    38
#define XE_TYPE_UBYTE4    6

	/* the usage must match the shader */
#define XE_USAGE_POSITION     0
#define XE_USAGE_BLENDWEIGHTS 1
#define XE_USAGE_BLENDINDICES 2
#define XE_USAGE_NORMAL       3
#define XE_USAGE_PSIZE        4
#define XE_USAGE_TEXCOORD     5
#define XE_USAGE_TANGENT      6
#define XE_USAGE_BINORMAL     7
#define XE_USAGE_TESSFACTOR   8
#define XE_USAGE_POSITIONT    9
#define XE_USAGE_COLOR       10
#define XE_USAGE_FOG         11
#define XE_USAGE_DEPTH       12
#define XE_USAGE_SAMPLE      13

	/* texture formats */
#define XE_FMT_MASK          0x3F
#define XE_FMT_8             2
#define XE_FMT_8888          6
#define XE_FMT_5551          3
#define XE_FMT_565           4
#define XE_FMT_16161616      26
#define XE_FMT_ARGB          0x80
#define XE_FMT_BGRA          0x00

#define XE_FMT_16BE          0x40

struct XenosVBFElement
{
	int usage; /* XE_USAGE */
	int index;
	int fmt; /* XE_TYPE */
};

struct XenosVBFFormat
{
	int num;
	struct XenosVBFElement e[10];
};

struct XenosSurface
{
	int width, height, wpitch, hpitch, tiled, format;
	u32 ptr, ptr_mip;
	int bypp;

    int use_filtering;
    int u_addressing,v_addressing;
    	
	void *base;

	struct XenosLock lock;
};

struct XenosVertexBuffer
{
	u32 phys_base;
	int vertices;
	int size, space; /* in DWORDs */
	void *base;
	
	struct XenosLock lock;
	
	struct XenosVertexBuffer *next;
};

#define XE_FMT_INDEX16 0
#define XE_FMT_INDEX32 1

struct XenosIndexBuffer
{
	u32 phys_base;
	int indices; /* actual size, in indices */
	int size; /* in bytes */
	void *base;
	int fmt; /* 0 for 16bit, 1 for 32bit */
	
	struct XenosLock lock;
};

struct XenosDevice
{
	float alu_constants[256 * 4 * 2];
	u32 fetch_constants[96 * 2];
	
	u32 alu_dirty; /* 16 * 4 constants per bit */
	u32 fetch_dirty; /* 3 * 2 per bit */

	float clipplane[6*4];
	
	u32 integer_constants[10*4];
	u32 controlpacket[9], stencildata[2];
	unsigned int alpharef; // should be moved into state

	struct XenosShader *vs, *ps;
	int vs_index;

#define DIRTY_ALU      0x0001
#define DIRTY_FETCH    0x0002
#define DIRTY_CLIP     0x0004
#define DIRTY_INTEGER  0x0008
#define DIRTY_CONTROL  0x0010
#define DIRTY_SHADER   0x0020
#define DIRTY_MISC     0x0040
	int dirty;

		/* private */
	u32 rb_secondary_base;
	volatile void *rb, *rb_primary, *rb_secondary;
	int rb_primary_wptr, rb_secondary_wptr;
	int rb_secondary_boundary;

	volatile unsigned int *regs;

	struct XenosSurface tex_fb;
	struct XenosSurface default_fb;
	
	struct XenosSurface *rt;
	int last_wptr;
	
	int vp_xres, vp_yres;
	int frameidx;
	
	u32 clear_color;
	u32 clear_stencil_z;
	int msaa_samples;

	struct XenosVertexBuffer *vb_current, *vb_head;
	int vb_current_pitch;

	struct XenosVertexBuffer *vb_pool;
	struct XenosVertexBuffer *vb_pool_after_frame;
	
	int tris_drawn;
	struct XenosIndexBuffer *current_ib;
	struct XenosVertexBuffer *current_vb;
	
	int edram_colorformat, edram_depthbase, edram_color0base, edram_hizpitch, edram_pitch;
	
	int scissor_enable;
	int scissor_ltrb[4];
};

void Xe_Init(struct XenosDevice *xe);
void __attribute__((noreturn)) Xe_Fatal(struct XenosDevice *xe, const char *fmt, ...);

void Xe_SetRenderTarget(struct XenosDevice *xe, struct XenosSurface *rt);
void Xe_Resolve(struct XenosDevice *xe);

#define XE_SOURCE_COLOR 0
#define XE_SOURCE_DS    4

#define XE_CLEAR_COLOR  1
#define XE_CLEAR_DS     2

void Xe_ResolveInto(struct XenosDevice *xe, struct XenosSurface *surface, int source, int clear);
	
	/* Xe_Clear always clears the complete rendertarget. No excuses. If you want arbitrary targets, use traditional draw.
	   (reason: resolve cannot handle arbitrary shapes) */
void Xe_Clear(struct XenosDevice *xe, int flags);
struct XenosSurface *Xe_GetFramebufferSurface(struct XenosDevice *xe);
void Xe_SetFrameBufferSurface(struct XenosDevice *xe, struct XenosSurface *fb);

void Xe_Execute(struct XenosDevice *xe);
void Xe_Sync(struct XenosDevice *xe);
void Xe_SetClearColor(struct XenosDevice *xe, u32 clearcolor);

void Xe_DirtyAluConstant(struct XenosDevice *xe, int base, int len);
void Xe_DirtyFetch(struct XenosDevice *xe, int base, int len);
struct XenosShader *Xe_LoadShader(struct XenosDevice *xe, const char *filename);
struct XenosShader *Xe_LoadShaderFromMemory(struct XenosDevice *xe, void *shader);
void Xe_InstantiateShader(struct XenosDevice *xe, struct XenosShader *sh, unsigned int index);
int Xe_GetShaderLength(struct XenosDevice *xe, void *sh);
void Xe_ShaderApplyVFetchPatches(struct XenosDevice *xe, struct XenosShader *sh, unsigned int index, const struct XenosVBFFormat *fmt);

int Xe_VBFCalcStride(struct XenosDevice *xe, const struct XenosVBFFormat *fmt);
int Xe_VBFCalcSize(struct XenosDevice *xe, const struct XenosVBFElement *fmt);

void Xe_SetZFunc(struct XenosDevice *xe, int z_func);
void Xe_SetZWrite(struct XenosDevice *xe, int zw);
void Xe_SetZEnable(struct XenosDevice *xe, int zw);
void Xe_SetFillMode(struct XenosDevice *xe, int front, int back);
void Xe_SetBlendControl(struct XenosDevice *xe, int col_src, int col_op, int col_dst, int alpha_src, int alpha_op, int alpha_dst);
void Xe_SetSrcBlend(struct XenosDevice *xe, unsigned int blend);
void Xe_SetDestBlend(struct XenosDevice *xe, unsigned int blend);
void Xe_SetBlendOp(struct XenosDevice *xe, unsigned int blendop);
void Xe_SetSrcBlendAlpha(struct XenosDevice *xe, unsigned int blend);
void Xe_SetDestBlendAlpha(struct XenosDevice *xe, unsigned int blend);
void Xe_SetBlendOpAlpha(struct XenosDevice *xe, unsigned int blendop);
void Xe_SetCullMode(struct XenosDevice *xe, unsigned int cullmode);
void Xe_SetAlphaTestEnable(struct XenosDevice *xe, int enable);
void Xe_SetAlphaFunc(struct XenosDevice *xe, unsigned int func);
void Xe_SetAlphaRef(struct XenosDevice *xe, float alpharef);
void Xe_SetScissor(struct XenosDevice *xe, int enable, int left, int top, int right, int bottom);

	/* bfff is a bitfield {backface,frontface} */
void Xe_SetStencilEnable(struct XenosDevice *xe, unsigned int enable);
void Xe_SetStencilFunc(struct XenosDevice *xe, int bfff, unsigned int func);

	/* -1 to leave old value */
void Xe_SetStencilOp(struct XenosDevice *xe, int bfff, int fail, int zfail, int pass);

void Xe_SetStencilRef(struct XenosDevice *xe, int bfff, int ref);
void Xe_SetStencilMask(struct XenosDevice *xe, int bfff, int mask);
void Xe_SetStencilWriteMask(struct XenosDevice *xe, int bfff, int writemask);

void Xe_SetClipPlaneEnables(struct XenosDevice *xe, int enables); // enables is a set of 1<<plane_index
void Xe_SetClipPlane(struct XenosDevice *xe, int idx, float * plane);

void Xe_InvalidateState(struct XenosDevice *xe);
void Xe_SetShader(struct XenosDevice *xe, int type, struct XenosShader *sh, int instance);
void Xe_SetTexture(struct XenosDevice *xe, int index, struct XenosSurface *tex);

struct XenosVertexBuffer *Xe_VBPoolAlloc(struct XenosDevice *xe, int size);
void Xe_VBPoolAdd(struct XenosDevice *xe, struct XenosVertexBuffer *vb);
void Xe_VBReclaim(struct XenosDevice *xe);
void Xe_VBBegin(struct XenosDevice *xe, int pitch); /* pitch, len is nr of vertices */
void Xe_VBPut(struct XenosDevice *xe, void *data, int len);
struct XenosVertexBuffer *Xe_VBEnd(struct XenosDevice *xe);
void Xe_Draw(struct XenosDevice *xe, struct XenosVertexBuffer *vb, struct XenosIndexBuffer *ib);


void Xe_SetIndices(struct XenosDevice *de, struct XenosIndexBuffer *ib);
void Xe_DrawIndexedPrimitive(struct XenosDevice *xe, int type, int base_index, int min_index, int num_vertices, int start_index, int primitive_count);
void Xe_DrawPrimitive(struct XenosDevice *xe, int type, int start, int primitive_count);

void Xe_SetStreamSource(struct XenosDevice *xe, int index, struct XenosVertexBuffer *vb, int offset, int stride);

struct XenosIndexBuffer *Xe_CreateIndexBuffer(struct XenosDevice *xe, int length, int format);
void Xe_DestroyIndexBuffer(struct XenosDevice *xe, struct XenosIndexBuffer *ib);

struct XenosVertexBuffer *Xe_CreateVertexBuffer(struct XenosDevice *xe, int length);
void Xe_DestroyVertexBuffer(struct XenosDevice *xe, struct XenosVertexBuffer *vb);

#define XE_LOCK_READ 1
#define XE_LOCK_WRITE 2

void *Xe_VB_Lock(struct XenosDevice *xe, struct XenosVertexBuffer *vb, int offset, int size, int flags);
void Xe_VB_Unlock(struct XenosDevice *xe, struct XenosVertexBuffer *vb);

void *Xe_IB_Lock(struct XenosDevice *xe, struct XenosIndexBuffer *ib, int offset, int size, int flags);
void Xe_IB_Unlock(struct XenosDevice *xe, struct XenosIndexBuffer *ib);

void Xe_SetVertexShaderConstantF(struct XenosDevice *xe, int start, const float *data, int count); /* count = number of 4 floats */
void Xe_SetPixelShaderConstantF(struct XenosDevice *xe, int start, const float *data, int count); /* count = number of 4 floats */

void Xe_SetVertexShaderConstantB(struct XenosDevice *xe, int index, int value);
void Xe_SetPixelShaderConstantB(struct XenosDevice *xe, int index, int value);

struct XenosSurface *Xe_CreateTexture(struct XenosDevice *xe, unsigned int width, unsigned int height, unsigned int levels, int format, int tiled);
void Xe_DestroyTexture(struct XenosDevice *xe, struct XenosSurface *surface);
void *Xe_Surface_LockRect(struct XenosDevice *xe, struct XenosSurface *surface, int x, int y, int w, int h, int flags);
void Xe_Surface_Unlock(struct XenosDevice *xe, struct XenosSurface *surface);

int Xe_IsVBlank(struct XenosDevice *xe);

#ifdef __cplusplus
};
#endif

#endif
