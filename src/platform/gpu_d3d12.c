/*
 * Bronto Engine - showing the framebuffer through Direct3D 12 (platform.h, br_gpu).
 *
 * Windows 10 and later. d3d12.dll, dxgi.dll and d3dcompiler_47.dll (all part of
 * Windows) are loaded at run time, and the COM interfaces are called through
 * their tables of methods, by position (the positions are those of d3d12.h and
 * dxgi1_4.h), so building needs no Windows SDK beyond windows.h and running
 * without Direct3D 12 just falls back.
 *
 * Each frame the framebuffer goes into an upload buffer, is copied into a
 * texture the frame's size, and one triangle covering the viewport samples it
 * with a point sampler (nearest-neighbour) onto the window's back buffer, which
 * was cleared black first: the bars. The two small shaders are HLSL, compiled
 * when it starts. One frame is in flight (the CPU waits for the last before
 * reusing the upload buffer); the swapchain is flip-model, presented with vsync,
 * and resized with the window.
 *
 * Included by platform_win32.c, which calls d12_start(hwnd, w, h),
 * d12_present(frame, viewport, w, h) (false: it stopped working) and d12_stop().
 */

/* ---- the declarations ------------------------------------------------------------------------- */
#ifndef STDMETHODCALLTYPE
#  define STDMETHODCALLTYPE __stdcall
#endif

typedef struct { SIZE_T ptr; } D12_CPU_HANDLE;
typedef struct { UINT64 ptr; } D12_GPU_HANDLE;
typedef struct { UINT Count, Quality; } D12_SAMPLE_DESC;
typedef struct { int Type; INT Priority; int Flags; UINT NodeMask; } D12_COMMAND_QUEUE_DESC;
typedef struct {
    UINT Width, Height; int Format; BOOL Stereo; D12_SAMPLE_DESC SampleDesc; UINT BufferUsage; UINT BufferCount;
    int Scaling; int SwapEffect; int AlphaMode; UINT Flags;
} D12_SWAP_CHAIN_DESC1;
typedef struct { int Type; UINT NumDescriptors; int Flags; UINT NodeMask; } D12_DESCRIPTOR_HEAP_DESC;
typedef struct { int Type; int CPUPageProperty; int MemoryPoolPreference; UINT CreationNodeMask; UINT VisibleNodeMask; } D12_HEAP_PROPERTIES;
typedef struct {
    int Dimension; UINT64 Alignment; UINT64 Width; UINT Height; UINT16 DepthOrArraySize; UINT16 MipLevels; int Format;
    D12_SAMPLE_DESC SampleDesc; int Layout; int Flags;
} D12_RESOURCE_DESC;
typedef struct {
    int Format; int ViewDimension; UINT Shader4ComponentMapping;
    union { struct { UINT MostDetailedMip, MipLevels, PlaneSlice; FLOAT ResourceMinLODClamp; } Texture2D; UINT64 pad[3]; } u;
} D12_SRV_DESC;
typedef struct {
    int Type; int Flags;
    union { struct { void *pResource; UINT Subresource; int StateBefore; int StateAfter; } Transition; UINT64 pad[3]; } u;
} D12_RESOURCE_BARRIER;
typedef struct { int Format; UINT Width, Height, Depth, RowPitch; } D12_SUBRESOURCE_FOOTPRINT;
typedef struct {
    void *pResource; int Type;
    union { struct { UINT64 Offset; D12_SUBRESOURCE_FOOTPRINT Footprint; } PlacedFootprint; UINT SubresourceIndex; } u;
} D12_TEXTURE_COPY_LOCATION;
typedef struct { FLOAT TopLeftX, TopLeftY, Width, Height, MinDepth, MaxDepth; } D12_VIEWPORT;
typedef struct { SIZE_T Begin, End; } D12_RANGE;
typedef struct { int RangeType; UINT NumDescriptors, BaseShaderRegister, RegisterSpace, OffsetInDescriptorsFromTableStart; } D12_DESCRIPTOR_RANGE;
typedef struct {
    int ParameterType;
    union { struct { UINT NumDescriptorRanges; const D12_DESCRIPTOR_RANGE *pDescriptorRanges; } DescriptorTable; UINT64 pad[2]; } u;
    int ShaderVisibility;
} D12_ROOT_PARAMETER;
typedef struct {
    int Filter; int AddressU, AddressV, AddressW; FLOAT MipLODBias; UINT MaxAnisotropy; int ComparisonFunc; int BorderColor;
    FLOAT MinLOD, MaxLOD; UINT ShaderRegister, RegisterSpace; int ShaderVisibility;
} D12_STATIC_SAMPLER_DESC;
typedef struct {
    UINT NumParameters; const D12_ROOT_PARAMETER *pParameters; UINT NumStaticSamplers; const D12_STATIC_SAMPLER_DESC *pStaticSamplers; int Flags;
} D12_ROOT_SIGNATURE_DESC;
typedef struct { const void *pShaderBytecode; SIZE_T BytecodeLength; } D12_SHADER_BYTECODE;
typedef struct {
    BOOL BlendEnable, LogicOpEnable; int SrcBlend, DestBlend, BlendOp, SrcBlendAlpha, DestBlendAlpha, BlendOpAlpha, LogicOp;
    UINT8 RenderTargetWriteMask;
} D12_RENDER_TARGET_BLEND_DESC;
typedef struct { int StencilFailOp, StencilDepthFailOp, StencilPassOp, StencilFunc; } D12_DEPTH_STENCILOP_DESC;
typedef struct {
    void *pRootSignature;
    D12_SHADER_BYTECODE VS, PS, DS, HS, GS;
    struct { const void *pSODeclaration; UINT NumEntries; const UINT *pBufferStrides; UINT NumStrides; UINT RasterizedStream; } StreamOutput;
    struct { BOOL AlphaToCoverageEnable, IndependentBlendEnable; D12_RENDER_TARGET_BLEND_DESC RenderTarget[8]; } BlendState;
    UINT SampleMask;
    struct {
        int FillMode, CullMode; BOOL FrontCounterClockwise; INT DepthBias; FLOAT DepthBiasClamp, SlopeScaledDepthBias;
        BOOL DepthClipEnable, MultisampleEnable, AntialiasedLineEnable; UINT ForcedSampleCount; int ConservativeRaster;
    } RasterizerState;
    struct {
        BOOL DepthEnable; int DepthWriteMask; int DepthFunc; BOOL StencilEnable; UINT8 StencilReadMask, StencilWriteMask;
        D12_DEPTH_STENCILOP_DESC FrontFace, BackFace;
    } DepthStencilState;
    struct { const void *pInputElementDescs; UINT NumElements; } InputLayout;
    int IBStripCutValue;
    int PrimitiveTopologyType;
    UINT NumRenderTargets;
    int RTVFormats[8];
    int DSVFormat;
    D12_SAMPLE_DESC SampleDesc;
    UINT NodeMask;
    struct { const void *pCachedBlob; SIZE_T CachedBlobSizeInBytes; } CachedPSO;
    int Flags;
} D12_PIPELINE_DESC;

/* The interface pointers (each is a pointer to a table of methods). */
typedef struct d12_obj { void **vtbl; } d12_obj;

/* `var`: method n of an object, a function returning RET taking PARAMS (`this` first). */
#define D12_METHOD(obj, n, RET, PARAMS, var) \
    RET (STDMETHODCALLTYPE *var) PARAMS; { void *p_ = ((d12_obj *)(obj))->vtbl[n]; memcpy(&var, &p_, sizeof(var)); }

static ULONG d12_release(void *obj) {
    if (!obj) return 0;
    {
        D12_METHOD(obj, 2, ULONG, (void *), f)
        return f(obj);
    }
}

/* The GUIDs (d3d12.h, dxgi1_2.h, dxgi1_4.h). */
static const GUID k_iid_device = { 0x189819f1, 0x1db6, 0x4b57, { 0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7 } };
static const GUID k_iid_queue = { 0x0ec870a6, 0x5d7e, 0x4c22, { 0x8c, 0xfc, 0x5b, 0xaa, 0xe0, 0x76, 0x16, 0xed } };
static const GUID k_iid_allocator = { 0x6102dee4, 0xaf59, 0x4b09, { 0xb9, 0x99, 0xb4, 0x4d, 0x73, 0xf0, 0x9b, 0x24 } };
static const GUID k_iid_list = { 0x5b160d0f, 0xac1b, 0x4185, { 0x8b, 0xa8, 0xb3, 0xae, 0x42, 0xa5, 0xa4, 0x55 } };
static const GUID k_iid_fence = { 0x0a753dcf, 0xc4d8, 0x4b91, { 0xad, 0xf6, 0xbe, 0x5a, 0x60, 0xd9, 0x5a, 0x76 } };
static const GUID k_iid_resource = { 0x696442be, 0xa72e, 0x4059, { 0xbc, 0x79, 0x5b, 0x5c, 0x98, 0x04, 0x0f, 0xad } };
static const GUID k_iid_heap = { 0x8efb471d, 0x616c, 0x4f49, { 0x90, 0xf7, 0x12, 0x7b, 0xb7, 0x63, 0xfa, 0x51 } };
static const GUID k_iid_root = { 0xc54a6b66, 0x72df, 0x4ee8, { 0x8b, 0xe5, 0xa9, 0x46, 0xa1, 0x42, 0x92, 0x14 } };
static const GUID k_iid_pipeline = { 0x765a30f3, 0xf624, 0x4c6f, { 0xa8, 0x28, 0xac, 0xe9, 0x48, 0x62, 0x24, 0x45 } };
static const GUID k_iid_factory2 = { 0x50c83a1c, 0xe072, 0x4c48, { 0x87, 0xb0, 0x36, 0x30, 0xfa, 0x36, 0xa6, 0xd0 } };
static const GUID k_iid_swapchain3 = { 0x94d99bdb, 0xf1f8, 0x4ab0, { 0xb2, 0x36, 0x7d, 0xa0, 0x17, 0x0e, 0xda, 0xb1 } };

typedef HRESULT (WINAPI *D3D12CreateDevice_fn)(void *adapter, int min_level, const GUID *iid, void **device);
typedef HRESULT (WINAPI *D3D12SerializeRootSignature_fn)(const D12_ROOT_SIGNATURE_DESC *, int version, void **blob, void **error);
typedef HRESULT (WINAPI *CreateDXGIFactory1_fn)(const GUID *iid, void **factory);
typedef HRESULT (WINAPI *D3DCompile_fn)(const void *src, SIZE_T size, const char *name, const void *defines, void *include,
                                         const char *entry, const char *target, UINT flags1, UINT flags2, void **code, void **errors);

#define D12_BUFFERS 2
#define D12_FORMAT_RGBA8 28
#define D12_STATE_PRESENT 0
#define D12_STATE_RENDER_TARGET 0x4
#define D12_STATE_PIXEL_SHADER_RESOURCE 0x80
#define D12_STATE_COPY_DEST 0x400
#define D12_STATE_GENERIC_READ 0xAC3

static struct {
    HMODULE d3d12, dxgi, compiler;
    void *factory, *device, *queue, *allocator, *list, *fence, *swapchain;
    void *rtv_heap, *srv_heap, *root, *pipeline;
    void *buffers[D12_BUFFERS];
    UINT rtv_step;
    UINT64 fence_value;
    HANDLE fence_event;
    void *texture, *upload;                  /* the frame on the GPU, and its upload buffer */
    unsigned char *upload_map;
    UINT pitch;                              /* bytes per row in the upload buffer (a multiple of 256) */
    int tex_w, tex_h, tex_state;
    int win_w, win_h;                        /* the size the back buffers are */
    HWND hwnd;
} D12;

/* ---- the methods used (by position in each interface's table) ---------------------------------- */
static HRESULT d12_query(void *obj, const GUID *iid, void **out) { D12_METHOD(obj, 0, HRESULT, (void *, const GUID *, void **), f) return f(obj, iid, out); }
/* ID3DBlob */
static void *blob_data(void *b) { D12_METHOD(b, 3, void *, (void *), f) return f(b); }
static SIZE_T blob_size(void *b) { D12_METHOD(b, 4, SIZE_T, (void *), f) return f(b); }
/* ID3D12Device */
static HRESULT dev_create_queue(const D12_COMMAND_QUEUE_DESC *d, void **out) {
    D12_METHOD(D12.device, 8, HRESULT, (void *, const D12_COMMAND_QUEUE_DESC *, const GUID *, void **), f)
    return f(D12.device, d, &k_iid_queue, out);
}
static HRESULT dev_create_allocator(void **out) {
    D12_METHOD(D12.device, 9, HRESULT, (void *, int, const GUID *, void **), f)
    return f(D12.device, 0, &k_iid_allocator, out);
}
static HRESULT dev_create_pipeline(const D12_PIPELINE_DESC *d, void **out) {
    D12_METHOD(D12.device, 10, HRESULT, (void *, const D12_PIPELINE_DESC *, const GUID *, void **), f)
    return f(D12.device, d, &k_iid_pipeline, out);
}
static HRESULT dev_create_list(void **out) {
    D12_METHOD(D12.device, 12, HRESULT, (void *, UINT, int, void *, void *, const GUID *, void **), f)
    return f(D12.device, 0, 0, D12.allocator, D12.pipeline, &k_iid_list, out);
}
static HRESULT dev_create_heap(const D12_DESCRIPTOR_HEAP_DESC *d, void **out) {
    D12_METHOD(D12.device, 14, HRESULT, (void *, const D12_DESCRIPTOR_HEAP_DESC *, const GUID *, void **), f)
    return f(D12.device, d, &k_iid_heap, out);
}
static UINT dev_increment(int type) { D12_METHOD(D12.device, 15, UINT, (void *, int), f) return f(D12.device, type); }
static HRESULT dev_create_root(const void *blob, SIZE_T size, void **out) {
    D12_METHOD(D12.device, 16, HRESULT, (void *, UINT, const void *, SIZE_T, const GUID *, void **), f)
    return f(D12.device, 0, blob, size, &k_iid_root, out);
}
static void dev_create_srv(void *res, const D12_SRV_DESC *d, D12_CPU_HANDLE at) {
    D12_METHOD(D12.device, 18, void, (void *, void *, const D12_SRV_DESC *, D12_CPU_HANDLE), f)
    f(D12.device, res, d, at);
}
static void dev_create_rtv(void *res, D12_CPU_HANDLE at) {
    D12_METHOD(D12.device, 20, void, (void *, void *, const void *, D12_CPU_HANDLE), f)
    f(D12.device, res, NULL, at);
}
static HRESULT dev_create_resource(const D12_HEAP_PROPERTIES *hp, const D12_RESOURCE_DESC *d, int state, void **out) {
    D12_METHOD(D12.device, 27, HRESULT, (void *, const D12_HEAP_PROPERTIES *, int, const D12_RESOURCE_DESC *, int, const void *, const GUID *, void **), f)
    return f(D12.device, hp, 0, d, state, NULL, &k_iid_resource, out);
}
static HRESULT dev_create_fence(void **out) {
    D12_METHOD(D12.device, 36, HRESULT, (void *, UINT64, int, const GUID *, void **), f)
    return f(D12.device, 0, 0, &k_iid_fence, out);
}
/* ID3D12DescriptorHeap: these return a struct, which COM hands back through a pointer after `this` */
static D12_CPU_HANDLE heap_cpu_start(void *heap) {
    D12_CPU_HANDLE h;
    D12_METHOD(heap, 9, D12_CPU_HANDLE *, (void *, D12_CPU_HANDLE *), f)
    f(heap, &h);
    return h;
}
static D12_GPU_HANDLE heap_gpu_start(void *heap) {
    D12_GPU_HANDLE h;
    D12_METHOD(heap, 10, D12_GPU_HANDLE *, (void *, D12_GPU_HANDLE *), f)
    f(heap, &h);
    return h;
}
/* ID3D12Resource */
static HRESULT res_map(void *res, void **out) {
    D12_RANGE none = { 0, 0 };
    D12_METHOD(res, 8, HRESULT, (void *, UINT, const D12_RANGE *, void **), f)
    return f(res, 0, &none, out);
}
/* ID3D12CommandAllocator */
static HRESULT alloc_reset(void) { D12_METHOD(D12.allocator, 8, HRESULT, (void *), f) return f(D12.allocator); }
/* ID3D12Fence */
static UINT64 fence_completed(void) { D12_METHOD(D12.fence, 8, UINT64, (void *), f) return f(D12.fence); }
static HRESULT fence_on_completion(UINT64 v, HANDLE e) { D12_METHOD(D12.fence, 9, HRESULT, (void *, UINT64, HANDLE), f) return f(D12.fence, v, e); }
/* ID3D12CommandQueue */
static void queue_execute(void) {
    void *lists[1];
    D12_METHOD(D12.queue, 10, void, (void *, UINT, void *const *), f)
    lists[0] = D12.list;
    f(D12.queue, 1, lists);
}
static HRESULT queue_signal(UINT64 v) { D12_METHOD(D12.queue, 14, HRESULT, (void *, void *, UINT64), f) return f(D12.queue, D12.fence, v); }
/* ID3D12GraphicsCommandList */
static HRESULT list_close(void) { D12_METHOD(D12.list, 9, HRESULT, (void *), f) return f(D12.list); }
static HRESULT list_reset(void) { D12_METHOD(D12.list, 10, HRESULT, (void *, void *, void *), f) return f(D12.list, D12.allocator, D12.pipeline); }
static void list_draw(UINT vertices) { D12_METHOD(D12.list, 12, void, (void *, UINT, UINT, UINT, UINT), f) f(D12.list, vertices, 1, 0, 0); }
static void list_copy_texture(const D12_TEXTURE_COPY_LOCATION *dst, const D12_TEXTURE_COPY_LOCATION *src) {
    D12_METHOD(D12.list, 16, void, (void *, const D12_TEXTURE_COPY_LOCATION *, UINT, UINT, UINT, const D12_TEXTURE_COPY_LOCATION *, const void *), f)
    f(D12.list, dst, 0, 0, 0, src, NULL);
}
static void list_topology(int t) { D12_METHOD(D12.list, 20, void, (void *, int), f) f(D12.list, t); }
static void list_viewport(const D12_VIEWPORT *v) { D12_METHOD(D12.list, 21, void, (void *, UINT, const D12_VIEWPORT *), f) f(D12.list, 1, v); }
static void list_scissor(const RECT *r) { D12_METHOD(D12.list, 22, void, (void *, UINT, const RECT *), f) f(D12.list, 1, r); }
static void list_barrier(const D12_RESOURCE_BARRIER *b) { D12_METHOD(D12.list, 26, void, (void *, UINT, const D12_RESOURCE_BARRIER *), f) f(D12.list, 1, b); }
static void list_heaps(void) {
    void *heaps[1];
    D12_METHOD(D12.list, 28, void, (void *, UINT, void *const *), f)
    heaps[0] = D12.srv_heap;
    f(D12.list, 1, heaps);
}
static void list_root(void) { D12_METHOD(D12.list, 30, void, (void *, void *), f) f(D12.list, D12.root); }
static void list_table(D12_GPU_HANDLE h) { D12_METHOD(D12.list, 32, void, (void *, UINT, D12_GPU_HANDLE), f) f(D12.list, 0, h); }
static void list_targets(const D12_CPU_HANDLE *rtv) {
    D12_METHOD(D12.list, 46, void, (void *, UINT, const D12_CPU_HANDLE *, BOOL, const D12_CPU_HANDLE *), f)
    f(D12.list, 1, rtv, FALSE, NULL);
}
static void list_clear(D12_CPU_HANDLE rtv) {
    static const FLOAT black[4] = { 0, 0, 0, 1 };
    D12_METHOD(D12.list, 48, void, (void *, D12_CPU_HANDLE, const FLOAT *, UINT, const RECT *), f)
    f(D12.list, rtv, black, 0, NULL);
}
/* IDXGIFactory2, IDXGISwapChain1/3 */
static HRESULT factory_swapchain(const D12_SWAP_CHAIN_DESC1 *d, void **out) {
    D12_METHOD(D12.factory, 15, HRESULT, (void *, void *, HWND, const D12_SWAP_CHAIN_DESC1 *, const void *, void *, void **), f)
    return f(D12.factory, D12.queue, D12.hwnd, d, NULL, NULL, out);
}
static HRESULT factory_window_association(void) {
    D12_METHOD(D12.factory, 8, HRESULT, (void *, HWND, UINT), f)
    return f(D12.factory, D12.hwnd, 2);                      /* no Alt+Enter: the engine does fullscreen itself */
}
static HRESULT swap_present(void) { D12_METHOD(D12.swapchain, 8, HRESULT, (void *, UINT, UINT), f) return f(D12.swapchain, 1, 0); }
static HRESULT swap_buffer(UINT i, void **out) {
    D12_METHOD(D12.swapchain, 9, HRESULT, (void *, UINT, const GUID *, void **), f)
    return f(D12.swapchain, i, &k_iid_resource, out);
}
static HRESULT swap_resize(UINT w, UINT h) {
    D12_METHOD(D12.swapchain, 13, HRESULT, (void *, UINT, UINT, UINT, int, UINT), f)
    return f(D12.swapchain, D12_BUFFERS, w, h, 0, 0);
}
static UINT swap_index(void) { D12_METHOD(D12.swapchain, 36, UINT, (void *), f) return f(D12.swapchain); }

/* ---- setting up ------------------------------------------------------------------------------- */

static const char k_d12_hlsl[] =
    "Texture2D frame : register(t0);\n"
    "SamplerState point_sampler : register(s0);\n"
    "struct V { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
    "V vs(uint id : SV_VertexID) {\n"                      /* one triangle over the viewport: (0,0) (2,0) (0,2) */
    "    V o;\n"
    "    float2 p = float2((id << 1) & 2, id & 2);\n"
    "    o.pos = float4(p.x * 2 - 1, 1 - p.y * 2, 0, 1);\n"
    "    o.uv = p;\n"
    "    return o;\n"
    "}\n"
    "float4 ps(V i) : SV_Target { return frame.Sample(point_sampler, i.uv); }\n";

static void d12_wait_idle(void) {
    UINT64 v = ++D12.fence_value;
    if (FAILED(queue_signal(v))) return;
    if (fence_completed() < v && SUCCEEDED(fence_on_completion(v, D12.fence_event))) WaitForSingleObject(D12.fence_event, 5000);
}

static void d12_drop_buffers(void) {
    int i;
    for (i = 0; i < D12_BUFFERS; ++i) { d12_release(D12.buffers[i]); D12.buffers[i] = NULL; }
}

static bool d12_take_buffers(void) {
    D12_CPU_HANDLE at = heap_cpu_start(D12.rtv_heap);
    int i;
    for (i = 0; i < D12_BUFFERS; ++i) {
        if (FAILED(swap_buffer((UINT)i, &D12.buffers[i]))) return false;
        dev_create_rtv(D12.buffers[i], at);
        at.ptr += D12.rtv_step;
    }
    return true;
}

static void *d12_compile(D3DCompile_fn compile, const char *entry, const char *target) {
    void *code = NULL, *errors = NULL;
    HRESULT hr = compile(k_d12_hlsl, sizeof(k_d12_hlsl) - 1, "bronto_present", NULL, NULL, entry, target, 0, 0, &code, &errors);
    if (FAILED(hr)) {
        BR_INFO("Direct3D 12: the %s shader didn't compile: %s", entry, errors ? (const char *)blob_data(errors) : "?");
        d12_release(errors);
        d12_release(code);
        return NULL;
    }
    d12_release(errors);
    return code;
}

static bool d12_pipeline(void) {
    D3D12SerializeRootSignature_fn serialize = W32_PROC(D3D12SerializeRootSignature_fn, D12.d3d12, "D3D12SerializeRootSignature");
    D3DCompile_fn compile;
    D12_DESCRIPTOR_RANGE range;
    D12_ROOT_PARAMETER param;
    D12_STATIC_SAMPLER_DESC sampler;
    D12_ROOT_SIGNATURE_DESC rd;
    D12_PIPELINE_DESC pd;
    void *blob = NULL, *errors = NULL, *vs, *ps;
    HRESULT hr;
    if (!D12.compiler) D12.compiler = LoadLibraryA("d3dcompiler_47.dll");
    compile = W32_PROC(D3DCompile_fn, D12.compiler, "D3DCompile");
    if (!serialize || !compile) { BR_INFO("Direct3D 12: no shader compiler (d3dcompiler_47.dll)"); return false; }
    memset(&range, 0, sizeof(range));
    range.RangeType = 0;                                     /* SRV */
    range.NumDescriptors = 1;
    memset(&param, 0, sizeof(param));
    param.ParameterType = 0;                                 /* a descriptor table */
    param.u.DescriptorTable.NumDescriptorRanges = 1;
    param.u.DescriptorTable.pDescriptorRanges = &range;
    param.ShaderVisibility = 5;                              /* the pixel shader */
    memset(&sampler, 0, sizeof(sampler));
    sampler.Filter = 0;                                      /* min, mag, mip: point */
    sampler.AddressU = sampler.AddressV = sampler.AddressW = 3;   /* clamp */
    sampler.ComparisonFunc = 8;                              /* always */
    sampler.MaxLOD = 3.402823466e+38f;
    sampler.ShaderVisibility = 5;
    memset(&rd, 0, sizeof(rd));
    rd.NumParameters = 1;
    rd.pParameters = &param;
    rd.NumStaticSamplers = 1;
    rd.pStaticSamplers = &sampler;
    if (FAILED(serialize(&rd, 1, &blob, &errors))) {
        BR_INFO("Direct3D 12: the root signature: %s", errors ? (const char *)blob_data(errors) : "?");
        d12_release(errors);
        return false;
    }
    hr = dev_create_root(blob_data(blob), blob_size(blob), &D12.root);
    d12_release(blob);
    d12_release(errors);
    if (FAILED(hr)) return false;
    if (!(vs = d12_compile(compile, "vs", "vs_5_0"))) return false;
    if (!(ps = d12_compile(compile, "ps", "ps_5_0"))) { d12_release(vs); return false; }
    memset(&pd, 0, sizeof(pd));
    pd.pRootSignature = D12.root;
    pd.VS.pShaderBytecode = blob_data(vs);
    pd.VS.BytecodeLength = blob_size(vs);
    pd.PS.pShaderBytecode = blob_data(ps);
    pd.PS.BytecodeLength = blob_size(ps);
    pd.BlendState.RenderTarget[0].SrcBlend = pd.BlendState.RenderTarget[0].SrcBlendAlpha = 2;    /* one */
    pd.BlendState.RenderTarget[0].DestBlend = pd.BlendState.RenderTarget[0].DestBlendAlpha = 1;  /* zero */
    pd.BlendState.RenderTarget[0].BlendOp = pd.BlendState.RenderTarget[0].BlendOpAlpha = 1;      /* add */
    pd.BlendState.RenderTarget[0].LogicOp = 4;                                                   /* no-op */
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = 0xF;
    pd.SampleMask = 0xFFFFFFFFu;
    pd.RasterizerState.FillMode = 3;                         /* solid */
    pd.RasterizerState.CullMode = 1;                         /* none */
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.DepthStencilState.DepthFunc = 8;                      /* (depth is off) */
    pd.DepthStencilState.FrontFace.StencilFailOp = pd.DepthStencilState.FrontFace.StencilDepthFailOp = pd.DepthStencilState.FrontFace.StencilPassOp = 1;
    pd.DepthStencilState.FrontFace.StencilFunc = 8;
    pd.DepthStencilState.BackFace = pd.DepthStencilState.FrontFace;
    pd.PrimitiveTopologyType = 3;                            /* triangles */
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = D12_FORMAT_RGBA8;
    pd.SampleDesc.Count = 1;
    hr = dev_create_pipeline(&pd, &D12.pipeline);
    d12_release(vs);
    d12_release(ps);
    if (FAILED(hr)) { BR_INFO("Direct3D 12: no pipeline (0x%08lx)", (unsigned long)hr); return false; }
    return true;
}

/* The frame's texture and its upload buffer, for a w x h frame. */
static bool d12_texture(int w, int h) {
    D12_HEAP_PROPERTIES hp;
    D12_RESOURCE_DESC rd;
    D12_SRV_DESC sd;
    d12_release(D12.texture);
    d12_release(D12.upload);
    D12.texture = D12.upload = NULL;
    D12.upload_map = NULL;
    D12.tex_w = D12.tex_h = 0;
    memset(&hp, 0, sizeof(hp));
    hp.Type = 1;                                             /* default (GPU memory) */
    memset(&rd, 0, sizeof(rd));
    rd.Dimension = 3;                                        /* texture 2D */
    rd.Width = (UINT64)w;
    rd.Height = (UINT)h;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = D12_FORMAT_RGBA8;
    rd.SampleDesc.Count = 1;
    if (FAILED(dev_create_resource(&hp, &rd, D12_STATE_COPY_DEST, &D12.texture))) return false;
    D12.tex_state = D12_STATE_COPY_DEST;
    D12.pitch = ((UINT)w * 4u + 255u) & ~255u;
    hp.Type = 2;                                             /* upload */
    memset(&rd, 0, sizeof(rd));
    rd.Dimension = 1;                                        /* buffer */
    rd.Width = (UINT64)D12.pitch * (UINT64)h;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = 1;                                           /* row major */
    if (FAILED(dev_create_resource(&hp, &rd, D12_STATE_GENERIC_READ, &D12.upload))) return false;
    if (FAILED(res_map(D12.upload, (void **)&D12.upload_map))) return false;
    memset(&sd, 0, sizeof(sd));
    sd.Format = D12_FORMAT_RGBA8;
    sd.ViewDimension = 4;                                    /* texture 2D */
    sd.Shader4ComponentMapping = 0x1688;                     /* r, g, b, a as they are */
    sd.u.Texture2D.MipLevels = 1;
    dev_create_srv(D12.texture, &sd, heap_cpu_start(D12.srv_heap));
    D12.tex_w = w;
    D12.tex_h = h;
    return true;
}

static void d12_stop(void) {
    if (D12.device && D12.queue && D12.fence) d12_wait_idle();
    d12_drop_buffers();
    d12_release(D12.texture);
    d12_release(D12.upload);
    d12_release(D12.list);
    d12_release(D12.pipeline);
    d12_release(D12.root);
    d12_release(D12.srv_heap);
    d12_release(D12.rtv_heap);
    d12_release(D12.swapchain);
    d12_release(D12.allocator);
    d12_release(D12.fence);
    d12_release(D12.queue);
    d12_release(D12.device);
    d12_release(D12.factory);
    if (D12.fence_event) CloseHandle(D12.fence_event);
    {   /* (the DLLs stay loaded) */
        HMODULE a = D12.d3d12, b = D12.dxgi, c = D12.compiler;
        memset(&D12, 0, sizeof(D12));
        D12.d3d12 = a;
        D12.dxgi = b;
        D12.compiler = c;
    }
}

static bool d12_start(HWND hwnd, int win_w, int win_h) {
    D3D12CreateDevice_fn create_device;
    CreateDXGIFactory1_fn create_factory;
    D12_COMMAND_QUEUE_DESC qd;
    D12_SWAP_CHAIN_DESC1 sd;
    D12_DESCRIPTOR_HEAP_DESC hd;
    void *swapchain1 = NULL;
    HRESULT hr;
    if (!D12.d3d12) D12.d3d12 = LoadLibraryA("d3d12.dll");
    if (!D12.dxgi) D12.dxgi = LoadLibraryA("dxgi.dll");
    create_device = W32_PROC(D3D12CreateDevice_fn, D12.d3d12, "D3D12CreateDevice");
    create_factory = W32_PROC(CreateDXGIFactory1_fn, D12.dxgi, "CreateDXGIFactory1");
    if (!create_device || !create_factory) { BR_INFO("Direct3D 12: not on this computer (Windows 10 or later)"); return false; }
    D12.hwnd = hwnd;
    if (FAILED(create_factory(&k_iid_factory2, &D12.factory))) { BR_INFO("Direct3D 12: no DXGI factory"); d12_stop(); return false; }
    if (FAILED(hr = create_device(NULL, 0xb000, &k_iid_device, &D12.device))) {        /* feature level 11.0 */
        BR_INFO("Direct3D 12: no device (0x%08lx): the GPU or its driver has no Direct3D 12", (unsigned long)hr);
        d12_stop();
        return false;
    }
    memset(&qd, 0, sizeof(qd));                               /* a direct queue */
    if (FAILED(dev_create_queue(&qd, &D12.queue)) || FAILED(dev_create_allocator(&D12.allocator)) || FAILED(dev_create_fence(&D12.fence)) ||
        !(D12.fence_event = CreateEventW(NULL, FALSE, FALSE, NULL))) { BR_INFO("Direct3D 12: couldn't set up the queue"); d12_stop(); return false; }
    memset(&sd, 0, sizeof(sd));
    sd.Width = (UINT)(win_w > 0 ? win_w : 1);
    sd.Height = (UINT)(win_h > 0 ? win_h : 1);
    sd.Format = D12_FORMAT_RGBA8;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = 0x20;                                    /* render target output */
    sd.BufferCount = D12_BUFFERS;
    sd.Scaling = 0;                                           /* stretch (only while a resize catches up) */
    sd.SwapEffect = 4;                                        /* flip, discard */
    if (FAILED(hr = factory_swapchain(&sd, &swapchain1))) { BR_INFO("Direct3D 12: no swapchain for the window (0x%08lx)", (unsigned long)hr); d12_stop(); return false; }
    hr = d12_query(swapchain1, &k_iid_swapchain3, &D12.swapchain);
    d12_release(swapchain1);
    if (FAILED(hr)) { BR_INFO("Direct3D 12: the swapchain is too old (no IDXGISwapChain3)"); d12_stop(); return false; }
    factory_window_association();
    D12.win_w = (int)sd.Width;
    D12.win_h = (int)sd.Height;
    memset(&hd, 0, sizeof(hd));
    hd.Type = 2;                                              /* render target views */
    hd.NumDescriptors = D12_BUFFERS;
    if (FAILED(dev_create_heap(&hd, &D12.rtv_heap))) { d12_stop(); return false; }
    D12.rtv_step = dev_increment(2);
    hd.Type = 0;                                              /* the frame's shader resource view, for the shader */
    hd.NumDescriptors = 1;
    hd.Flags = 1;                                             /* shader visible */
    if (FAILED(dev_create_heap(&hd, &D12.srv_heap))) { d12_stop(); return false; }
    if (!d12_take_buffers() || !d12_pipeline() || FAILED(dev_create_list(&D12.list)) || FAILED(list_close())) {
        BR_INFO("Direct3D 12: couldn't set up drawing");
        d12_stop();
        return false;
    }
    br_gpu_set_active(BR_GPU_D3D12, "Direct3D 12");
    return true;
}

/* ---- each frame ---------------------------------------------------------------------------------- */

static void d12_transition(void *res, int from, int to) {
    D12_RESOURCE_BARRIER b;
    memset(&b, 0, sizeof(b));
    b.u.Transition.pResource = res;
    b.u.Transition.Subresource = 0xFFFFFFFFu;               /* all of it */
    b.u.Transition.StateBefore = from;
    b.u.Transition.StateAfter = to;
    list_barrier(&b);
}

/* False only when Direct3D 12 has stopped working (the caller falls back to GDI). */
static bool d12_present(const br_image *fb, br_recti vp, int win_w, int win_h) {
    D12_TEXTURE_COPY_LOCATION dst, src;
    D12_CPU_HANDLE rtv;
    D12_VIEWPORT v;
    RECT all;
    UINT index;
    int y;
    if (win_w <= 0 || win_h <= 0 || !fb || fb->width <= 0 || fb->height <= 0) return true;
    if (win_w != D12.win_w || win_h != D12.win_h) {           /* the window changed size: new back buffers */
        d12_wait_idle();
        d12_drop_buffers();
        if (FAILED(swap_resize((UINT)win_w, (UINT)win_h)) || !d12_take_buffers()) return false;
        D12.win_w = win_w;
        D12.win_h = win_h;
    }
    if (fb->width != D12.tex_w || fb->height != D12.tex_h) {
        d12_wait_idle();
        if (!d12_texture(fb->width, fb->height)) return false;
    }
    for (y = 0; y < fb->height; ++y)
        memcpy(D12.upload_map + (size_t)y * D12.pitch, fb->pixels + (size_t)y * (size_t)fb->width, (size_t)fb->width * 4u);
    if (FAILED(alloc_reset()) || FAILED(list_reset())) return false;
    /* the frame into its texture */
    if (D12.tex_state != D12_STATE_COPY_DEST) d12_transition(D12.texture, D12.tex_state, D12_STATE_COPY_DEST);
    memset(&dst, 0, sizeof(dst));
    dst.pResource = D12.texture;
    dst.Type = 0;                                             /* subresource 0 */
    memset(&src, 0, sizeof(src));
    src.pResource = D12.upload;
    src.Type = 1;                                             /* a placed footprint */
    src.u.PlacedFootprint.Footprint.Format = D12_FORMAT_RGBA8;
    src.u.PlacedFootprint.Footprint.Width = (UINT)fb->width;
    src.u.PlacedFootprint.Footprint.Height = (UINT)fb->height;
    src.u.PlacedFootprint.Footprint.Depth = 1;
    src.u.PlacedFootprint.Footprint.RowPitch = D12.pitch;
    list_copy_texture(&dst, &src);
    d12_transition(D12.texture, D12_STATE_COPY_DEST, D12_STATE_PIXEL_SHADER_RESOURCE);
    D12.tex_state = D12_STATE_PIXEL_SHADER_RESOURCE;
    /* the window: black, then the frame over the viewport */
    index = swap_index();
    if (index >= D12_BUFFERS) return false;
    d12_transition(D12.buffers[index], D12_STATE_PRESENT, D12_STATE_RENDER_TARGET);
    rtv = heap_cpu_start(D12.rtv_heap);
    rtv.ptr += (SIZE_T)index * D12.rtv_step;
    list_targets(&rtv);
    list_clear(rtv);
    if (vp.w > 0 && vp.h > 0) {
        v.TopLeftX = (FLOAT)vp.x;
        v.TopLeftY = (FLOAT)vp.y;
        v.Width = (FLOAT)vp.w;
        v.Height = (FLOAT)vp.h;
        v.MinDepth = 0;
        v.MaxDepth = 1;
        all.left = all.top = 0;
        all.right = win_w;
        all.bottom = win_h;
        list_root();
        list_heaps();
        list_table(heap_gpu_start(D12.srv_heap));
        list_viewport(&v);
        list_scissor(&all);
        list_topology(4);                                     /* a triangle list */
        list_draw(3);
    }
    d12_transition(D12.buffers[index], D12_STATE_RENDER_TARGET, D12_STATE_PRESENT);
    if (FAILED(list_close())) return false;
    queue_execute();
    if (FAILED(swap_present())) return false;
    d12_wait_idle();                                          /* (one frame in flight: the upload buffer is free again) */
    return true;
}
