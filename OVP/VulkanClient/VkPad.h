// ==============================================================
// VkPad.h
// 2D Sketchpad implementation for the Vulkan Graphics Client
// Part of the ORBITER VISUALISATION PROJECT (OVP) - VulkanClient
// Licensed under the MIT License
// ==============================================================

#ifndef __VKCLIENT_VKPAD_H
#define __VKCLIENT_VKPAD_H

#include "VkAPI.h"
#include "Orbitersdk.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

class VkContext;
class VkSwapchain;
class VkSurfNative;
class VkPadFont;
class VkPadPen;
class VkPadBrush;
class VkPad;

namespace oapi { class VulkanClient; }

// Shader switch bitflags packed into VkSkpVtx::fnc (B8G8R8A8_UNORM layout matching D3D9 D3DCOLOR)
constexpr uint32_t VK_SKPSW_LENGTH    = 0x00FF0000u; // compute hairline segment length in vertex shader
constexpr uint32_t VK_SKPSW_COLORKEY  = 0x00008000u; // enable color-key discard on textured quad
constexpr uint32_t VK_SKPSW_FONT      = 0x0000FF00u; // modulate alpha by tFnt.a font coverage
constexpr uint32_t VK_SKPSW_FRAGMENT  = 0x00000000u; // sample color from vertex inClr
constexpr uint32_t VK_SKPSW_PENCOLOR  = 0x00000080u; // sample color from uniform gPen
constexpr uint32_t VK_SKPSW_TEXTURE   = 0x000000FFu; // sample color from tTex
constexpr uint32_t VK_SKPSW_WIDEPEN_L = 0x00000000u; // wide-line left extrusion (-1)
constexpr uint32_t VK_SKPSW_CENTER    = 0x80000000u; // unextruded center (0)
constexpr uint32_t VK_SKPSW_WIDEPEN_R = 0xFF000000u; // wide-line right extrusion (+1)

// Shader flag bits for SkpUniforms::gFlags[0]
constexpr int32_t VK_SKPF_TEX     = 1;
constexpr int32_t VK_SKPF_FONT    = 2;
constexpr int32_t VK_SKPF_KEY     = 4;
constexpr int32_t VK_SKPF_DASH    = 8;
constexpr int32_t VK_SKPF_WIDE    = 16;
constexpr int32_t VK_SKPF_EFFECTS = 32;

#pragma pack(push, 1)
struct VkSkpVtx {
	float    x, y, l;        // location 0: vec3 inPos (2D pos + cumulative dash length)
	float    nx, ny, px, py; // location 1: vec4 inDir (next/uv xy, prev zw)
	uint32_t clr;            // location 2: B8G8R8A8_UNORM -> OAPI ABGR vertex color
	uint32_t fnc;            // location 3: B8G8R8A8_UNORM -> SKPSW_* switches
};
#pragma pack(pop)
static_assert(sizeof(VkSkpVtx) == 36, "VkSkpVtx layout must match Sketchpad.vert");

struct alignas(16) VkSkpUniforms {
	float   gWVP[16];        // column-major mat4
	float   gColorMatrix[16];// column-major mat4
	float   gPen[4];         // RGBA
	float   gKey[4];         // RGBA color key
	float   gSize[4];        // (1/texW, 1/texH, fontScaleX, fontScaleY)
	float   gTarget[4];      // (2/tgtW, 2/tgtH, tgtW, tgtH)
	float   gWidth[4];       // (penWidth, patternScale, evenWidthOffset, 0)
	float   gGamma[4];       // RGB gamma, A unused
	float   gNoiseColor[4];  // RGB noise color, A blend factor
	float   gMisc[4];        // x = noise seed
	int32_t gFlags[4];       // x = VK_SKPF_* bitmask
};
static_assert(sizeof(VkSkpUniforms) == 272, "VkSkpUniforms must match std140 SkpParams (272 bytes)");

// ---------------------------------------------------------------------------
// Per-session Sketchpad Vulkan pipeline, buffer ring, and pad pool
// ---------------------------------------------------------------------------
class VkPadPipeline {
public:
	enum class FilterMode : uint8_t {
		Point = 0,
		Linear = 1,
		Aniso = 2
	};

	enum class ColorBlendOp : uint8_t {
		AlphaBlend = 0,
		Copy       = 1,
		CopyAlpha  = 2,
		CopyColor  = 3
	};

	VkPadPipeline() = default;
	~VkPadPipeline();

	VkPadPipeline(const VkPadPipeline &) = delete;
	VkPadPipeline &operator=(const VkPadPipeline &) = delete;

	bool Init(VkContext &context, VkSwapchain &swap);
	void Destroy();

	bool IsInitialized() const { return ctx != nullptr && pipeLayout != VK_NULL_HANDLE; }

	oapi::Sketchpad *AcquirePad(oapi::VulkanClient &client, oapi::Font *defaultFont, SURFHANDLE surf);
	void ReleasePad(oapi::Sketchpad *sp);
	void OnSurfaceDestroyed(SURFHANDLE surf);

	void FlushActivePads();

	VkContext *Context() const { return ctx; }
	VkSwapchain *Swapchain() const { return swapchain; }

	VkPipeline GetOrCreatePipeline(
		VkFormat colorFormat,
		VkFormat depthFormat,
		ColorBlendOp blend,
		VkPrimitiveTopology topology,
		bool depthEnable);

	VkPipelineLayout PipelineLayout() const { return pipeLayout; }
	VkDescriptorSet AllocateDescriptorSet(uint32_t slot);
	void BeginSlotFrame(uint32_t slot, uint64_t slotGen);

	struct BufferSlice {
		VkBuffer     buffer = VK_NULL_HANDLE;
		VkDeviceSize offset = 0;
		void        *mapped = nullptr;
	};

	BufferSlice AllocVertices(uint32_t slot, VkDeviceSize byteSize);
	BufferSlice AllocIndices(uint32_t slot, VkDeviceSize byteSize);
	BufferSlice AllocUniform(uint32_t slot, VkDeviceSize byteSize);

	VkImageView WhiteView() const { return whiteView; }
	VkImageView NoiseView() const { return noiseView; }
	VkSampler NoiseSampler() const { return samplerNoise; }
	VkSampler SamplerForFilter(FilterMode mode) const;

	void UpdateBatchDescriptorSet(
		VkDescriptorSet dstSet,
		const BufferSlice &uboSlice,
		VkImageView texView,
		VkSampler texSampler,
		VkImageView fntView);

private:
	static constexpr uint32_t kNumSlots = 3;

	struct PipelineKey {
		uint32_t colorFormat;
		uint32_t depthFormat;
		uint8_t  blend;
		uint8_t  isLineList;
		uint8_t  depthEnable;
		uint8_t  pad;

		bool operator==(const PipelineKey &rhs) const
		{
			return colorFormat == rhs.colorFormat &&
			       depthFormat == rhs.depthFormat &&
			       blend == rhs.blend &&
			       isLineList == rhs.isLineList &&
			       depthEnable == rhs.depthEnable;
		}
	};

	struct PipelineKeyHash {
		size_t operator()(const PipelineKey &k) const noexcept
		{
			size_t h = (size_t)k.colorFormat * 1315423911u;
			h ^= ((size_t)k.depthFormat + 0x9e3779b9u + (h << 6) + (h >> 2));
			h ^= ((size_t)k.blend << 16) ^ ((size_t)k.isLineList << 24) ^ ((size_t)k.depthEnable << 25);
			return h;
		}
	};

	struct BufferChunk {
		VkBuffer      buffer = VK_NULL_HANDLE;
		VmaAllocation alloc  = VK_NULL_HANDLE;
		VkDeviceSize  size   = 0;
		VkDeviceSize  used   = 0;
		void         *mapped = nullptr;
	};

	struct BufferArena {
		std::vector<BufferChunk> chunks;
		VkDeviceSize             defaultChunkBytes = 0;
		VkBufferUsageFlags       usage = 0;
		VkDeviceSize             align = 16;
	};

	struct SlotPools {
		uint64_t                      lastGen = UINT64_MAX;
		BufferArena                   vtxArena;
		BufferArena                   idxArena;
		BufferArena                   uboArena;
		std::vector<VkDescriptorPool> descPools;
		size_t                        activeDescPool = 0;
	};

	bool CreateDescriptorAndPipelineLayouts();
	bool CreateSamplers();
	bool CreateHelperTextures();
	bool CreateStaticTexture(
		uint32_t width,
		uint32_t height,
		const uint8_t *bgraPixels,
		VkImage &outImage,
		VmaAllocation &outAlloc,
		VkImageView &outView);
	BufferSlice AllocFromArena(BufferArena &arena, VkDeviceSize byteSize);
	void ResetArena(BufferArena &arena);
	void DestroyArena(BufferArena &arena);
	VkDescriptorPool CreateDescriptorPool();

	VkContext   *ctx = nullptr;
	VkSwapchain *swapchain = nullptr;

	VkShaderModule        vertShader = VK_NULL_HANDLE;
	VkShaderModule        fragShader = VK_NULL_HANDLE;
	VkDescriptorSetLayout setLayout  = VK_NULL_HANDLE;
	VkPipelineLayout      pipeLayout = VK_NULL_HANDLE;

	VkSampler samplerPoint  = VK_NULL_HANDLE;
	VkSampler samplerLinear = VK_NULL_HANDLE;
	VkSampler samplerAniso  = VK_NULL_HANDLE;
	VkSampler samplerNoise  = VK_NULL_HANDLE;
	bool      hasAnisotropy = false;

	VkImage       whiteImage = VK_NULL_HANDLE;
	VmaAllocation whiteAlloc = VK_NULL_HANDLE;
	VkImageView   whiteView  = VK_NULL_HANDLE;

	VkImage       noiseImage = VK_NULL_HANDLE;
	VmaAllocation noiseAlloc = VK_NULL_HANDLE;
	VkImageView   noiseView  = VK_NULL_HANDLE;

	VkDeviceSize uboAlignment = 256;

	SlotPools slotPools[kNumSlots]{};
	std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash> pipelines;

	std::vector<std::unique_ptr<VkPad>> freePads;
	std::vector<VkPad *>                activePads;
};

// ---------------------------------------------------------------------------
// 2D/3D Vulkan Sketchpad implementation (oapi::Sketchpad)
// ---------------------------------------------------------------------------
class VkPad : public oapi::Sketchpad {
public:
	VkPad(oapi::VulkanClient &client, VkPadPipeline &pipeline, SURFHANDLE surf);
	~VkPad() override = default;

	void ResetForSurface(SURFHANDLE surf, oapi::Font *defaultFont);
	void BeginDrawing();
	void EndDrawing();
	void LoadDefaults(oapi::Font *defaultFont);
	bool Flush(SURFHANDLE hTarget = nullptr);

	// --- Font, Pen, Brush & Text attributes ---
	oapi::Font  *SetFont(oapi::Font *font) override;
	oapi::Pen   *SetPen(oapi::Pen *pen) override;
	oapi::Brush *SetBrush(oapi::Brush *brush) override;

	void  SetTextAlign(TAlign_horizontal tah = LEFT, TAlign_vertical tav = TOP) override;
	DWORD SetTextColor(DWORD col) override;
	DWORD SetBackgroundColor(DWORD col) override;
	void  SetBackgroundMode(BkgMode mode) override;
	DWORD GetCharSize() override;
	DWORD GetTextWidth(const char *str, int len = 0) override;
	void  SetOrigin(int x, int y) override;
	void  GetOrigin(int *x, int *y) const override;

	// --- Text output ---
	bool Text(int x, int y, const char *str, int len) override;
	bool TextW(int x, int y, const LPWSTR str, int len) override;
	void TextEx(float x, float y, const char *str, float scale = 100.0f, float angle = 0.0f) override;

	// --- Vector & Shape primitives ---
	void Pixel(int x, int y, DWORD col) override;
	void MoveTo(int x, int y) override;
	void LineTo(int x, int y) override;
	void Line(int x0, int y0, int x1, int y1) override;
	void Rectangle(int x0, int y0, int x1, int y1) override;
	void Ellipse(int x0, int y0, int x1, int y1) override;
	void Polygon(const oapi::IVECTOR2 *pt, int npt) override;
	void Polyline(const oapi::IVECTOR2 *pt, int npt) override;
	void PolyPolygon(const oapi::IVECTOR2 *pt, const int *npt, const int nlines) override;
	void PolyPolyline(const oapi::IVECTOR2 *pt, const int *npt, const int nlines) override;

	// --- Sketchpad2 / 3 extensions ---
	HDC  GetDC() override { return nullptr; }
	void GetRenderSurfaceSize(LPSIZE size) override;
	void QuickPen(DWORD color, float width = 1.0f, DWORD style = 1) override;
	void QuickBrush(DWORD color) override;
	void SetGlobalLineScale(float width = 1.0f, float pattern = 1.0f) override;

	void SetWorldTransform(const oapi::FMATRIX4 *pWT = nullptr) override;
	void SetWorldTransform2D(float scale = 1.0f, float rot = 0.0f, const oapi::IVECTOR2 *c = nullptr, const oapi::IVECTOR2 *t = nullptr) override;
	void SetWorldScaleTransform2D(const oapi::FVECTOR2 *scale = nullptr, const oapi::IVECTOR2 *tr = nullptr) override;
	void SetWorldBillboard(const oapi::FVECTOR3 &pos, float scale = 1.0f, bool bFixed = true, const oapi::FVECTOR3 *up = nullptr) override;
	void PushWorldTransform() override;
	void PopWorldTransform() override;

	void DrawPoly(const HPOLY hPoly, DWORD flags = 0) override;
	void Lines(const oapi::FVECTOR2 *pt1, int nlines) override;

	void CopyRect(const SURFHANDLE pSrc, const LPRECT s, int tx, int ty) override;
	void StretchRect(const SURFHANDLE pSrc, const LPRECT s = nullptr, const LPRECT t = nullptr) override;
	void RotateRect(const SURFHANDLE pSrc, const LPRECT s, int cx, int cy, float angle = 0.0f, float sw = 1.0f, float sh = 1.0f) override;
	void ColorKey(const SURFHANDLE pSrc, const LPRECT s, int tx, int ty) override;
	void ColorFill(DWORD color, const LPRECT t) override;
	void CopyTetragon(const SURFHANDLE pSrc, const LPRECT _s, const oapi::FVECTOR2 pt[4]) override;
	void FillTetragon(DWORD color, const oapi::FVECTOR2 pt[4]) override;
	void GradientFillRect(const LPRECT rect, DWORD c1, DWORD c2, bool bVertical = false) override;
	void StretchRegion(const skpRegion *rgn, const SURFHANDLE hSrc, const LPRECT out) override;

	void ClipRect(const LPRECT clip = nullptr) override;
	void Clipper(int idx, const VECTOR3 *pPos = nullptr, double cos_angle = 0.0, double dir = 0.0) override;
	void DepthEnable(bool bEnable) override;

	void SetViewMode(SkpView mode = ORTHO) override;
	const oapi::FMATRIX4 *ViewMatrix() const override;
	const oapi::FMATRIX4 *ProjectionMatrix() const override;
	const oapi::FMATRIX4 *GetViewProjectionMatrix() const override;
	void SetViewMatrix(const oapi::FMATRIX4 *pV = nullptr) override;
	void SetProjectionMatrix(const oapi::FMATRIX4 *pP = nullptr) override;
	void SetClipDistance(float zmin, float zmax) override;

	const oapi::FMATRIX4 *GetColorMatrix() override;
	void SetColorMatrix(const oapi::FMATRIX4 *pMatrix = nullptr) override;
	void SetBrightness(const oapi::FVECTOR4 *pBrightness = nullptr) override;
	oapi::FVECTOR4 GetRenderParam(RenderParam param) override;
	void SetRenderParam(RenderParam param, const oapi::FVECTOR4 *data = nullptr) override;

	void SetBlendState(BlendState dwState = (BlendState)(BlendState::ALPHABLEND | BlendState::FILTER_LINEAR)) override;
	void ColorCompatibility(bool bEnable) override;
	void Clear(DWORD color = 0, bool bColor = true, bool bDepth = true) override;
	int  DrawMeshGroup(const MESHHANDLE hMesh, DWORD grp, MeshFlags flags = MeshFlags::SMOOTH_SHADE, const SURFHANDLE hTex = nullptr) override;

private:
	struct DrawBatch {
		uint32_t                    firstIndex = 0;
		uint32_t                    indexCount = 0;
		VkSkpUniforms               params{};
		VkSurfNative               *tex = nullptr;
		VkSurfNative               *fntTex = nullptr;
		VkPadPipeline::FilterMode   filter = VkPadPipeline::FilterMode::Linear;
		VkPadPipeline::ColorBlendOp blend = VkPadPipeline::ColorBlendOp::AlphaBlend;
		bool                        isLineList = false;
		bool                        depthEnable = false;
		VkRect2D                    scissor{};
	};

	bool ResolveTargetInfo(
		VkImageView &outColorView,
		VkFormat &outColorFormat,
		VkExtent2D &outExtent,
		VkImageView &outDepthView,
		VkFormat &outDepthFormat) const;

	void UpdateWVP();
	void RefreshEffectsFlag();
	uint32_t ResolveCompatColor(DWORD color) const;

	VkSkpUniforms BuildUniforms(
		int32_t featureFlags,
		VkSurfNative *tex,
		uint32_t colorKeyAbgr,
		float effectivePenWidth,
		const oapi::FMATRIX4 *overrideWorld = nullptr) const;

	DrawBatch &BeginBatch(
		bool isLineList,
		int32_t featureFlags,
		VkSurfNative *tex,
		VkSurfNative *fntTex,
		uint32_t colorKeyAbgr,
		float effectivePenWidth,
		const oapi::FMATRIX4 *overrideWorld = nullptr);

	void AppendSolidQuad(float x0, float y0, float x1, float y1, uint32_t abgr, const oapi::FMATRIX4 *overrideWorld = nullptr);
	void AppendGradientQuad(float x0, float y0, float x1, float y1, uint32_t cTL, uint32_t cTR, uint32_t cBR, uint32_t cBL, const oapi::FMATRIX4 *overrideWorld = nullptr);
	void AppendTexturedQuad(
		VkSurfNative *src,
		const oapi::FVECTOR2 pos[4],
		const oapi::FVECTOR2 uv[4],
		bool useColorKey,
		bool forceColorKey = false,
		const oapi::FMATRIX4 *overrideWorld = nullptr);
	void AppendStrokePolyline(const oapi::FVECTOR2 *pts, int npt, bool closed);
	void AppendHairlineSegments(const oapi::FVECTOR2 *pts, int npt, bool closed, bool separatePairs = false);
	void AppendWidePolyline(const oapi::FVECTOR2 *pts, int npt, bool closed);

	bool HasActivePen() const;
	uint32_t ActivePenColor() const;
	float ActivePenWidth() const;
	bool ActivePenDashed() const;

	bool HasActiveBrush() const;
	uint32_t ActiveBrushColor() const;

	bool PrintCp1252Run(float x, float y, const char *cp1252, int len, float scale = 1.0f, float angle = 0.0f);

	oapi::VulkanClient &client;
	VkPadPipeline      &pipeline;
	SURFHANDLE          targetSurf = nullptr;
	VkSurfNative       *nativeTarget = nullptr;

	// Current selection & typography state
	oapi::Font  *defFont = nullptr;
	VkPadFont   *cfont   = nullptr;
	VkPadPen    *cpen    = nullptr;
	VkPadBrush  *cbrsh   = nullptr;

	bool     quickPenActive   = false;
	uint32_t quickPenColor    = 0xFFFFFFFFu;
	float    quickPenWidth    = 1.0f;
	bool     quickPenDashed   = false;
	bool     quickBrushActive = false;
	uint32_t quickBrushColor  = 0xFFFFFFFFu;
	bool     colorCompat      = false;

	uint32_t          textColor = 0xFFFFFFFFu; // 0xAABBGGRR
	uint32_t          bkColor   = 0xFF000000u; // 0xAABBGGRR
	BkgMode           bkMode    = BK_TRANSPARENT;
	TAlign_horizontal hAlign    = LEFT;
	TAlign_vertical   vAlign    = TOP;
	oapi::IVECTOR2    origin    = { 0, 0 };
	oapi::IVECTOR2    curPos    = { 0, 0 };

	float penWidthScale = 1.0f;
	float patternScale  = 1.0f;

	// Render state
	VkPadPipeline::ColorBlendOp blendOp     = VkPadPipeline::ColorBlendOp::AlphaBlend;
	VkPadPipeline::FilterMode   texFilter   = VkPadPipeline::FilterMode::Linear;
	bool                        depthEnable = false;
	SkpView                     viewMode    = ORTHO;
	bool                        clipEnabled = false;
	RECT                        clipRect    = {};
	float                       clipNear    = -1000.0f;
	float                       clipFar     = 1000.0f;

	// Transform & effects state
	oapi::FMATRIX4              mWorld{};
	oapi::FMATRIX4              mView{};
	oapi::FMATRIX4              mProj{};
	mutable oapi::FMATRIX4      mVP{};
	oapi::FMATRIX4              mWVP{};
	std::vector<oapi::FMATRIX4> worldStack;

	oapi::FMATRIX4 mColorMatrix{};
	oapi::FVECTOR4 gammaVal     = { 1.0f, 1.0f, 1.0f, 1.0f };
	oapi::FVECTOR4 noiseColor   = { 0.0f, 0.0f, 0.0f, 1.0f };
	bool     hasEffects   = false;
	uint32_t noiseCounter = 0;

	// Pending Clear operation (executed at start of next Flush render pass)
	bool     hasPendingClearColor = false;
	bool     hasPendingClearDepth = false;
	uint32_t pendingClearAbgr     = 0;

	// Batched geometry & draw commands
	std::vector<VkSkpVtx>  vertices;
	std::vector<uint32_t>  indices;
	std::vector<DrawBatch> batches;
	bool                   didSubmitOnTarget = false;
};

#endif // !__VKCLIENT_VKPAD_H
