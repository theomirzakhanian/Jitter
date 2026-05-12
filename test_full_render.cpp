/*******************************************************************
 * test_full_render.cpp
 *
 * Full mock-AE harness. Drives the actual EffectMain() entry point
 * through PF_Cmd_GLOBAL_SETUP → PARAMS_SETUP → SMART_PRE_RENDER →
 * SMART_RENDER, with every AE callback our render code uses mocked.
 * Inspects the output pixel buffer and verifies it's not transparent.
 *
 * Catches the class of bugs that aren't catchable by the engine-only
 * harness — e.g., the temp_world.extent_hint=0 transparency bug.
 *
 * Build (from SDK/Effect/Jitter):
 *   clang++ -std=c++17 -O2 -Wall -Wno-unused-function -Wno-macro-redefined \
 *     -I../../Headers -I../../Headers/SP -I../../Util -I../../Resources \
 *     -DAE_OS_MAC=1 -DPF_DEEP_COLOR_AWARE=1 \
 *     test_full_render.cpp Mac/Jitter_Strings.cpp -o test_full_render
 *   ./test_full_render
 *******************************************************************/

#include "Jitter.h"
#include "JitterEngine.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <map>
#include <string>
#include <cmath>

// We link against Jitter.cpp which already declares EffectMain via Jitter.h
// (which is included above). Just need to include the right header.
char *GetStringPtr(int strNum);	// from Jitter_Strings.cpp

// ==========================================================================
// Mock world allocation
// ==========================================================================

static std::vector<std::vector<uint8_t>*> g_world_buffers;	// keep alive

static void alloc_world(PF_EffectWorld *w, A_long width, A_long height, bool deep, bool clear) {
	A_long pix = deep ? 8 : 4;
	A_long rb = width * pix;
	auto *buf = new std::vector<uint8_t>(rb * height, 0);
	g_world_buffers.push_back(buf);
	w->width = width;
	w->height = height;
	w->rowbytes = rb;
	w->data = reinterpret_cast<PF_PixelPtr>(buf->data());
	w->world_flags = deep ? PF_WorldFlag_DEEP : (PF_WorldFlags)0;
	w->extent_hint.left = 0;
	w->extent_hint.top = 0;
	w->extent_hint.right = width;
	w->extent_hint.bottom = height;
	if (clear) std::memset(buf->data(), 0, rb * height);
}

bool g_break_extent_hint = false;	// when true, simulate AE not initializing extent_hint (temp world via new_world)
bool g_break_output_extent_hint = false;	// when true, simulate AE not initializing output_worldP->extent_hint

static PF_Err mock_new_world(PF_ProgPtr ref, A_long w, A_long h,
                             PF_NewWorldFlags flags, PF_EffectWorld *world)
{
	(void)ref;
	bool deep = (flags & PF_NewWorldFlag_DEEP_PIXELS) != 0;
	bool clear = (flags & PF_NewWorldFlag_CLEAR_PIXELS) != 0;
	alloc_world(world, w, h, deep, clear);
	if (g_break_extent_hint) {
		// Simulate AE not setting extent_hint — verifies our render code's
		// explicit extent_hint setup protects against this.
		world->extent_hint = {0, 0, 0, 0};
	}
	return PF_Err_NONE;
}

static PF_Err mock_dispose_world(PF_ProgPtr ref, PF_EffectWorld *w) {
	(void)ref; (void)w;
	return PF_Err_NONE;
}

// ==========================================================================
// transform_world: real bilinear matrix transform
// ==========================================================================
static PF_Err mock_transform_world(PF_ProgPtr ref, PF_Quality quality, PF_ModeFlags m_flags,
	PF_Field field, const PF_EffectWorld *src, const PF_CompositeMode *cmode,
	const PF_MaskWorld *mask, const PF_FloatMatrix *matrices, A_long num_matrices,
	PF_Boolean src2dst, const PF_Rect *dest_rect, PF_EffectWorld *dst)
{
	(void)ref; (void)quality; (void)m_flags; (void)field; (void)cmode; (void)mask;
	(void)num_matrices; (void)src2dst;
	const PF_FloatMatrix &m = matrices[0];
	double a = m.mat[0][0], b = m.mat[0][1];
	double c = m.mat[1][0], d = m.mat[1][1];
	double tx = m.mat[2][0], ty = m.mat[2][1];
	double det = a * d - b * c;
	if (std::fabs(det) < 1e-12) return PF_Err_NONE;
	double inv_a =  d / det, inv_b = -b / det;
	double inv_c = -c / det, inv_d =  a / det;
	double inv_tx = -(inv_a * tx + inv_b * ty);
	double inv_ty = -(inv_c * tx + inv_d * ty);
	PF_Rect r = dest_rect ? *dest_rect :
		PF_Rect{0, 0, static_cast<A_short>(dst->width), static_cast<A_short>(dst->height)};
	bool deep = (dst->world_flags & PF_WorldFlag_DEEP) != 0;
	for (A_short y = r.top; y < r.bottom; y++) {
		for (A_short x = r.left; x < r.right; x++) {
			double sx = inv_a * x + inv_b * y + inv_tx;
			double sy = inv_c * x + inv_d * y + inv_ty;
			int sxi = static_cast<int>(sx);
			int syi = static_cast<int>(sy);
			if (sxi < 0 || syi < 0 || sxi >= src->width || syi >= src->height) continue;
			if (deep) {
				const PF_Pixel16 *p = reinterpret_cast<const PF_Pixel16*>(
					reinterpret_cast<const uint8_t*>(src->data) + syi * src->rowbytes + sxi * 8);
				PF_Pixel16 *o = reinterpret_cast<PF_Pixel16*>(
					reinterpret_cast<uint8_t*>(dst->data) + y * dst->rowbytes + x * 8);
				*o = *p;
			} else {
				const PF_Pixel *p = reinterpret_cast<const PF_Pixel*>(
					reinterpret_cast<const uint8_t*>(src->data) + syi * src->rowbytes + sxi * 4);
				PF_Pixel *o = reinterpret_cast<PF_Pixel*>(
					reinterpret_cast<uint8_t*>(dst->data) + y * dst->rowbytes + x * 4);
				*o = *p;
			}
		}
	}
	return PF_Err_NONE;
}

// ==========================================================================
// fill / iterate / copy
// ==========================================================================
static PF_Err mock_fill(PF_ProgPtr ref, const PF_Pixel *color, const PF_Rect *r, PF_EffectWorld *w) {
	(void)ref;
	PF_Rect rect = r ? *r :
		PF_Rect{0, 0, static_cast<A_short>(w->width), static_cast<A_short>(w->height)};
	for (A_short y = rect.top; y < rect.bottom; y++) {
		PF_Pixel *row = reinterpret_cast<PF_Pixel*>(
			reinterpret_cast<uint8_t*>(w->data) + y * w->rowbytes);
		for (A_short x = rect.left; x < rect.right; x++) row[x] = *color;
	}
	return PF_Err_NONE;
}

static PF_Err mock_fill16(PF_ProgPtr ref, const PF_Pixel16 *color, const PF_Rect *r, PF_EffectWorld *w) {
	(void)ref;
	PF_Rect rect = r ? *r :
		PF_Rect{0, 0, static_cast<A_short>(w->width), static_cast<A_short>(w->height)};
	for (A_short y = rect.top; y < rect.bottom; y++) {
		PF_Pixel16 *row = reinterpret_cast<PF_Pixel16*>(
			reinterpret_cast<uint8_t*>(w->data) + y * w->rowbytes);
		for (A_short x = rect.left; x < rect.right; x++) row[x] = *color;
	}
	return PF_Err_NONE;
}

static PF_Err mock_iterate8(PF_InData *in, A_long base, A_long final_,
	PF_EffectWorld *src, const PF_Rect *area, void *refcon,
	PF_Err (*pix_fn)(void*, A_long, A_long, PF_Pixel*, PF_Pixel*),
	PF_EffectWorld *dst)
{
	(void)in; (void)base; (void)final_;
	PF_Rect r = area ? *area :
		PF_Rect{0, 0, static_cast<A_short>(dst->width), static_cast<A_short>(dst->height)};
	for (A_long y = r.top; y < r.bottom; y++) {
		for (A_long x = r.left; x < r.right; x++) {
			PF_Pixel *spix = reinterpret_cast<PF_Pixel*>(
				reinterpret_cast<uint8_t*>(src->data) + y * src->rowbytes + x * 4);
			PF_Pixel *dpix = reinterpret_cast<PF_Pixel*>(
				reinterpret_cast<uint8_t*>(dst->data) + y * dst->rowbytes + x * 4);
			pix_fn(refcon, x, y, spix, dpix);
		}
	}
	return PF_Err_NONE;
}

static PF_Err mock_iterate16(PF_InData *in, A_long base, A_long final_,
	PF_EffectWorld *src, const PF_Rect *area, void *refcon,
	PF_Err (*pix_fn)(void*, A_long, A_long, PF_Pixel16*, PF_Pixel16*),
	PF_EffectWorld *dst)
{
	(void)in; (void)base; (void)final_;
	PF_Rect r = area ? *area :
		PF_Rect{0, 0, static_cast<A_short>(dst->width), static_cast<A_short>(dst->height)};
	for (A_long y = r.top; y < r.bottom; y++) {
		for (A_long x = r.left; x < r.right; x++) {
			PF_Pixel16 *spix = reinterpret_cast<PF_Pixel16*>(
				reinterpret_cast<uint8_t*>(src->data) + y * src->rowbytes + x * 8);
			PF_Pixel16 *dpix = reinterpret_cast<PF_Pixel16*>(
				reinterpret_cast<uint8_t*>(dst->data) + y * dst->rowbytes + x * 8);
			pix_fn(refcon, x, y, spix, dpix);
		}
	}
	return PF_Err_NONE;
}

static PF_Err mock_copy_hq(PF_ProgPtr ref, PF_EffectWorld *src, PF_EffectWorld *dst,
                           PF_Rect *sr, PF_Rect *dr) {
	(void)ref; (void)sr; (void)dr;
	A_long pix = (dst->world_flags & PF_WorldFlag_DEEP) ? 8 : 4;
	A_long h = std::min(src->height, dst->height);
	A_long w = std::min(src->width, dst->width);
	for (A_short y = 0; y < h; y++) {
		std::memcpy(
			reinterpret_cast<uint8_t*>(dst->data) + y * dst->rowbytes,
			reinterpret_cast<const uint8_t*>(src->data) + y * src->rowbytes,
			w * pix);
	}
	return PF_Err_NONE;
}

static int mock_sprintf(char *buf, const char *fmt, ...) {
	va_list ap; va_start(ap, fmt);
	int n = std::vsprintf(buf, fmt, ap);
	va_end(ap);
	return n;
}

// ==========================================================================
// Suite tables
// ==========================================================================
static PF_FillMatteSuite2 g_fill_suite{};
static PF_WorldTransformSuite1 g_xform_suite{};
static PF_Iterate8Suite2 g_iter8_suite{};
static PF_Iterate16Suite2 g_iter16_suite{};
static PF_ANSICallbacksSuite1 g_ansi_suite{};

static void setup_suite_tables() {
	g_fill_suite.fill = mock_fill;
	g_fill_suite.fill16 = mock_fill16;
	g_xform_suite.copy_hq = mock_copy_hq;
	// transform_world via suite — we point to a thunk that ignores 1 extra arg
	g_xform_suite.transform_world = nullptr;	// our render uses utils->transform_world
	g_iter8_suite.iterate = mock_iterate8;
	g_iter16_suite.iterate = mock_iterate16;
	g_ansi_suite.sprintf = mock_sprintf;
}

// ==========================================================================
// SPBasicSuite — resolves named suites
// ==========================================================================
static SPErr mock_acquire_suite(const char *name, int32 version, const void **suite) {
	(void)version;
	if (std::strcmp(name, kPFFillMatteSuite) == 0) { *suite = &g_fill_suite; return 0; }
	if (std::strcmp(name, kPFWorldTransformSuite) == 0) { *suite = &g_xform_suite; return 0; }
	if (std::strcmp(name, kPFIterate8Suite) == 0) { *suite = &g_iter8_suite; return 0; }
	if (std::strcmp(name, kPFIterate16Suite) == 0) { *suite = &g_iter16_suite; return 0; }
	if (std::strcmp(name, kPFANSISuite) == 0) { *suite = &g_ansi_suite; return 0; }
	*suite = nullptr;
	return -1;
}
static SPErr mock_release_suite(const char *name, int32 version) {
	(void)name; (void)version; return 0;
}
static SPBoolean mock_is_equal(const char *a, const char *b) { return std::strcmp(a, b) == 0; }
static SPErr mock_alloc(size_t sz, void **p) { *p = std::malloc(sz); return *p ? 0 : -1; }
static SPErr mock_free(void *p) { std::free(p); return 0; }
static SPErr mock_realloc(void *p, size_t sz, void **np) { *np = std::realloc(p, sz); return *np ? 0 : -1; }
static SPErr mock_undefined(void) { return -1; }

static SPBasicSuite g_basic_suite = {
	mock_acquire_suite,
	mock_release_suite,
	mock_is_equal,
	mock_alloc,
	mock_free,
	mock_realloc,
	(SPErr (*)(void))mock_undefined,
};

// ==========================================================================
// PF_ParamDef capture and lookup. ParamsSetup calls PF_ADD_PARAM via
// in_data->inter.add_param. checkout_param later retrieves by index.
// ==========================================================================
static std::vector<PF_ParamDef> g_param_defs;

static PF_Err mock_add_param(PF_ProgPtr ref, A_long index, PF_ParamDef *def) {
	(void)ref; (void)index;
	g_param_defs.push_back(*def);
	return PF_Err_NONE;
}

static PF_Err mock_checkout_param(PF_ProgPtr ref, PF_ParamIndex idx, A_long time,
                                  A_long step, A_u_long scale, PF_ParamDef *out)
{
	(void)ref; (void)time; (void)step; (void)scale;
	// AE param index 0 is the implicit input layer; user params start at 1.
	// g_param_defs is 0-indexed by ParamsSetup call order. So shift by 1.
	int slot = idx - 1;
	if (slot < 0 || slot >= (int)g_param_defs.size()) {
		AEFX_CLR_STRUCT(*out);
		return PF_Err_NONE;
	}
	*out = g_param_defs[slot];
	return PF_Err_NONE;
}

static PF_Err mock_checkin_param(PF_ProgPtr ref, PF_ParamDef *def) {
	(void)ref; (void)def;
	return PF_Err_NONE;
}

// ==========================================================================
// PF_PreRenderCallbacks / PF_SmartRenderCallbacks
// ==========================================================================
static PF_EffectWorld g_input_world;
static PF_EffectWorld g_output_world;

static PF_Err mock_checkout_layer(PF_ProgPtr ref, PF_ParamIndex idx, A_long checkout_id,
	const PF_RenderRequest *req, A_long time, A_long step, A_u_long scale,
	PF_CheckoutResult *out)
{
	(void)ref; (void)idx; (void)checkout_id; (void)req; (void)time; (void)step; (void)scale;
	out->result_rect = {0, 0, (A_short)g_input_world.width, (A_short)g_input_world.height};
	out->max_result_rect = out->result_rect;
	out->par.num = 1; out->par.den = 1;
	out->solid = TRUE;
	out->ref_width = g_input_world.width;
	out->ref_height = g_input_world.height;
	for (int i = 0; i < 6; i++) out->reserved[i] = 0;
	return PF_Err_NONE;
}

static PF_Err mock_checkout_layer_pixels(PF_ProgPtr ref, A_long checkout_id, PF_EffectWorld **out) {
	(void)ref; (void)checkout_id;
	*out = &g_input_world;
	return PF_Err_NONE;
}

static PF_Err mock_checkin_layer_pixels(PF_ProgPtr ref, A_long checkout_id) {
	(void)ref; (void)checkout_id;
	return PF_Err_NONE;
}

static PF_Err mock_checkout_output(PF_ProgPtr ref, PF_EffectWorld **out) {
	(void)ref;
	*out = &g_output_world;
	return PF_Err_NONE;
}

// ==========================================================================
// in_data utils + inter setup
// ==========================================================================
static _PF_UtilCallbacks g_utils{};
static PF_InteractCallbacks g_inter{};
static PF_PreRenderCallbacks g_pre_cb{};
static PF_SmartRenderCallbacks g_smart_cb{};

static void setup_callbacks() {
	g_utils.new_world = mock_new_world;
	g_utils.dispose_world = mock_dispose_world;
	g_utils.transform_world = mock_transform_world;
	g_utils.copy = (PF_Err (*)(PF_ProgPtr, PF_EffectWorld*, PF_EffectWorld*, PF_Rect*, PF_Rect*))mock_copy_hq;
	g_utils.fill = mock_fill;

	g_inter.add_param = mock_add_param;
	g_inter.checkout_param = mock_checkout_param;
	g_inter.checkin_param = mock_checkin_param;

	g_pre_cb.checkout_layer = mock_checkout_layer;

	g_smart_cb.checkout_layer_pixels = mock_checkout_layer_pixels;
	g_smart_cb.checkin_layer_pixels = mock_checkin_layer_pixels;
	g_smart_cb.checkout_output = mock_checkout_output;
}

// ==========================================================================
// Test driver
// ==========================================================================
static void fill_input(PF_EffectWorld *w, uint8_t r, uint8_t g, uint8_t b) {
	for (A_long y = 0; y < w->height; y++) {
		PF_Pixel *row = reinterpret_cast<PF_Pixel*>(
			reinterpret_cast<uint8_t*>(w->data) + y * w->rowbytes);
		for (A_long x = 0; x < w->width; x++) {
			row[x].alpha = 255; row[x].red = r; row[x].green = g; row[x].blue = b;
		}
	}
}

static double frac_transparent(const PF_EffectWorld *w) {
	long count = 0, transparent = 0;
	for (A_long y = 0; y < w->height; y++) {
		const PF_Pixel *row = reinterpret_cast<const PF_Pixel*>(
			reinterpret_cast<const uint8_t*>(w->data) + y * w->rowbytes);
		for (A_long x = 0; x < w->width; x++) {
			count++;
			if (row[x].alpha == 0) transparent++;
		}
	}
	return static_cast<double>(transparent) / count;
}

static double avg_red(const PF_EffectWorld *w) {
	long count = 0; double sum = 0;
	for (A_long y = 0; y < w->height; y++) {
		const PF_Pixel *row = reinterpret_cast<const PF_Pixel*>(
			reinterpret_cast<const uint8_t*>(w->data) + y * w->rowbytes);
		for (A_long x = 0; x < w->width; x++) { sum += row[x].red; count++; }
	}
	return sum / count;
}

// Param overrides applied AFTER ParamsSetup but BEFORE SMART_PRE_RENDER.
struct ParamOverride {
	int    disk_id;
	int    type;	// 0=float, 1=bool, 2=popup
	double float_val;
	bool   bool_val;
	int    popup_val;
};

// One full render pass: set up, call EffectMain through GLOBAL_SETUP /
// PARAMS_SETUP / SMART_PRE_RENDER / SMART_RENDER. Returns final output.
struct RenderResult {
	double frac_transparent;
	double avg_red;
	bool   ok;
};

static RenderResult run_render(int width = 100, int height = 100,
                               const std::vector<ParamOverride> &overrides = {},
                               bool break_extent_hint = false,
                               A_long current_time = 0)
{
	g_world_buffers.clear();
	g_param_defs.clear();

	// Mock in_data
	PF_InData in{};
	PF_OutData out{};
	in.utils = &g_utils;
	in.inter = g_inter;
	in.pica_basicP = &g_basic_suite;
	in.effect_ref = (PF_ProgPtr)0xabc123;
	in.appl_id = 'FXTC';
	in.quality = PF_Quality_HI;
	in.field = PF_Field_FRAME;
	in.current_time = current_time;
	in.time_step = 1;
	in.time_scale = 30;
	in.total_time = 300;

	// 1. GLOBAL_SETUP
	if (EffectMain(PF_Cmd_GLOBAL_SETUP, &in, &out, nullptr, nullptr, nullptr) != PF_Err_NONE)
		return {0, 0, false};

	// 2. PARAMS_SETUP — populates g_param_defs
	if (EffectMain(PF_Cmd_PARAMS_SETUP, &in, &out, nullptr, nullptr, nullptr) != PF_Err_NONE)
		return {0, 0, false};

	// Apply param overrides (simulating saved-project param values)
	for (auto &ov : overrides) {
		for (auto &d : g_param_defs) {
			if (d.uu.id == ov.disk_id) {
				if (ov.type == 0) d.u.fs_d.value = ov.float_val;
				else if (ov.type == 1) d.u.bd.value = ov.bool_val ? TRUE : FALSE;
				else if (ov.type == 2) d.u.pd.value = ov.popup_val;
				break;
			}
		}
	}

	// Sanity: every param should default with `dephault` set (already done by macros).
	// We'll synthesize a "params[]" array AE would pass in: each element is a defaulted
	// PF_ParamDef matching the saved def.

	// 3. Allocate input + output worlds
	alloc_world(&g_input_world, width, height, false, false);
	alloc_world(&g_output_world, width, height, false, true);
	fill_input(&g_input_world, 200, 100, 50);
	if (g_break_output_extent_hint) {
		// Simulate AE handing us output_worldP with extent_hint=(0,0,0,0).
		// Plugin must explicitly initialize it or transform_world / iterate
		// will write nothing to output and we'll see transparent result.
		g_output_world.extent_hint = {0, 0, 0, 0};
	}

	// 4. SMART_PRE_RENDER
	PF_PreRenderInput pin{};
	pin.output_request.rect = {0, 0, (A_short)width, (A_short)height};
	pin.output_request.field = PF_Field_FRAME;
	pin.output_request.channel_mask = PF_ChannelMask_ARGB;
	pin.output_request.preserve_rgb_of_zero_alpha = TRUE;
	pin.bitdepth = 8;
	PF_PreRenderOutput pout{};
	pout.result_rect = {0, 0, (A_short)width, (A_short)height};
	pout.max_result_rect = pout.result_rect;
	PF_PreRenderExtra pre_extra{};
	pre_extra.input = &pin;
	pre_extra.output = &pout;
	pre_extra.cb = &g_pre_cb;
	if (EffectMain(PF_Cmd_SMART_PRE_RENDER, &in, &out, nullptr, nullptr, &pre_extra) != PF_Err_NONE)
		return {0, 0, false};

	// 5. SMART_RENDER
	PF_SmartRenderInput sin{};
	sin.output_request = pin.output_request;
	sin.bitdepth = 8;
	sin.pre_render_data = pout.pre_render_data;
	PF_SmartRenderExtra smart_extra{};
	smart_extra.input = &sin;
	smart_extra.cb = &g_smart_cb;

	// Optionally break extent_hint to demonstrate the bug we just fixed
	if (break_extent_hint) {
		// Hack: mock_new_world will be called inside SMART_RENDER for temp_world.
		// We can't easily zero it post-hoc here; instead we rely on the
		// transparency-bug fix being in place.
	}

	if (EffectMain(PF_Cmd_SMART_RENDER, &in, &out, nullptr, nullptr, &smart_extra) != PF_Err_NONE)
		return {0, 0, false};

	if (pout.delete_pre_render_data_func)
		pout.delete_pre_render_data_func(pout.pre_render_data);

	double t = frac_transparent(&g_output_world);
	double r = avg_red(&g_output_world);
	return {t, r, true};
}

// ==========================================================================
// Tests — change param values via mutating g_param_defs after PARAMS_SETUP
// ==========================================================================

// Helper: find the param index by disk_id (search g_param_defs)
static PF_ParamDef* find_by_disk_id(int disk_id) {
	for (auto &d : g_param_defs) if (d.uu.id == disk_id) return &d;
	return nullptr;
}

static void mutate_param_float(int disk_id, double value) {
	if (auto *p = find_by_disk_id(disk_id)) p->u.fs_d.value = value;
}
static void mutate_param_bool(int disk_id, bool value) {
	if (auto *p = find_by_disk_id(disk_id)) p->u.bd.value = value ? TRUE : FALSE;
}

int main() {
	std::printf("=== Jitter full-render harness (mocks AE end-to-end) ===\n\n");
	setup_suite_tables();
	setup_callbacks();

	int failures = 0;
	auto check = [&](const char *name, bool cond, const char *detail = nullptr) {
		std::printf("  %-58s %s%s%s\n", name, cond ? "OK" : "FAIL",
			detail ? "  — " : "", detail ? detail : "");
		if (!cond) failures++;
	};

	// Test 1: default-state render — output should NOT be fully transparent.
	// This is the big one: catches the temp_world.extent_hint=0 bug class.
	{
		RenderResult r = run_render();
		char detail[256];
		std::snprintf(detail, sizeof(detail), "frac_transparent=%.2f, avg_red=%.0f",
			r.frac_transparent, r.avg_red);
		check("default render: not fully transparent", r.frac_transparent < 0.5, detail);
		check("default render: avg_red close to input (200)",
			std::fabs(r.avg_red - 200) < 50, detail);
	}

	// Test 2: Slide enabled with MODERATE amount=20 on a 1920x1080-ish layer.
	// With slide envelope ±500px (engine MAX), amount=20 gives ~±100px swing.
	// On a 1920x1080 layer, slide of ±100px should leave 90%+ of pixels visible.
	{
		std::vector<ParamOverride> ovs = {
			{AMOUNT_DISK_ID, 0, 100.0, false, 0},
			{ENABLE_SLIDE_DISK_ID, 1, 0.0, true, 0},
			{SLIDE_AMOUNT_DISK_ID, 0, 20.0, false, 0},
			{SLIDE_TWITCHES_DISK_ID, 0, 5.0, false, 0},
		};
		RenderResult r = run_render(1920, 1080, ovs);
		char detail[256];
		std::snprintf(detail, sizeof(detail), "frac_transparent=%.2f, avg_red=%.0f",
			r.frac_transparent, r.avg_red);
		check("slide=20 on 1920x1080: <30% transparent",
			r.frac_transparent < 0.3, detail);
	}

	// Test 3: All-operators-on with MODERATE amounts on 1920x1080.
	// User's likely scenario from saved Twitch project.
	{
		std::vector<ParamOverride> ovs = {
			{AMOUNT_DISK_ID, 0, 100.0, false, 0},
			{ENABLE_SLIDE_DISK_ID, 1, 0.0, true, 0},
			{ENABLE_SCALE_DISK_ID, 1, 0.0, true, 0},
			{ENABLE_TIME_DISK_ID, 1, 0.0, true, 0},
			{ENABLE_COLOR_DISK_ID, 1, 0.0, true, 0},
			{ENABLE_LIGHT_DISK_ID, 1, 0.0, true, 0},
			{ENABLE_BLUR_DISK_ID, 1, 0.0, true, 0},
			{SLIDE_AMOUNT_DISK_ID, 0, 20.0, false, 0},
			{SCALE_AMOUNT_DISK_ID, 0, 20.0, false, 0},
			{COLOR_AMOUNT_DISK_ID, 0, 50.0, false, 0},
			{LIGHT_AMOUNT_DISK_ID, 0, 50.0, false, 0},
			{BLUR_AMOUNT_DISK_ID, 0, 30.0, false, 0},
		};
		RenderResult r = run_render(1920, 1080, ovs);
		char detail[256];
		std::snprintf(detail, sizeof(detail), "frac_transparent=%.2f, avg_red=%.0f",
			r.frac_transparent, r.avg_red);
		check("all-ops moderate on 1920x1080: <30% transparent",
			r.frac_transparent < 0.3, detail);
	}

	// Test 3b: ALL-OPERATORS-ON ZERO AMOUNTS (default-saved scenario).
	// If user's saved project enabled operators but with default amount=0,
	// engine returns 0 for all, and we should produce identity output.
	{
		std::vector<ParamOverride> ovs = {
			{AMOUNT_DISK_ID, 0, 100.0, false, 0},
			{ENABLE_SLIDE_DISK_ID, 1, 0.0, true, 0},
			{ENABLE_COLOR_DISK_ID, 1, 0.0, true, 0},
			{ENABLE_LIGHT_DISK_ID, 1, 0.0, true, 0},
			{ENABLE_BLUR_DISK_ID, 1, 0.0, true, 0},
			// All operator amounts default to 0 (we don't override them)
		};
		RenderResult r = run_render(1920, 1080, ovs);
		char detail[256];
		std::snprintf(detail, sizeof(detail), "frac_transparent=%.2f, avg_red=%.0f",
			r.frac_transparent, r.avg_red);
		check("operators-enabled-amounts-zero on 1920x1080: <10% transparent",
			r.frac_transparent < 0.1, detail);
	}

	// Test 4: Simulate AE's new_world NOT setting extent_hint — verify our
	// explicit fix in SmartRender protects against this. Modest amounts on
	// a large layer so the only thing that could cause transparency is the
	// extent_hint bug.
	{
		std::vector<ParamOverride> ovs = {
			{AMOUNT_DISK_ID, 0, 100.0, false, 0},
			{ENABLE_SLIDE_DISK_ID, 1, 0.0, true, 0},
			{ENABLE_COLOR_DISK_ID, 1, 0.0, true, 0},  // forces slow path
			{SLIDE_AMOUNT_DISK_ID, 0, 5.0, false, 0},  // only ±25px slide on 1920px layer
			{COLOR_AMOUNT_DISK_ID, 0, 30.0, false, 0},
		};
		extern bool g_break_extent_hint;
		g_break_extent_hint = true;	// new_world will leave extent_hint=0
		RenderResult r = run_render(1920, 1080, ovs);
		g_break_extent_hint = false;
		char detail[256];
		std::snprintf(detail, sizeof(detail), "frac_transparent=%.2f", r.frac_transparent);
		check("extent_hint=0 + modest amounts on 1920x1080: <10% transparent",
			r.frac_transparent < 0.1, detail);
	}

	// Test 4b: AE hands us output_worldP with extent_hint=(0,0,0,0).
	// FAST PATH (no operators) — transform_world's dest_rect = output extent_hint,
	// which would be empty without our explicit fix → transparent output.
	{
		std::vector<ParamOverride> ovs = {
			{AMOUNT_DISK_ID, 0, 100.0, false, 0},
			{ENABLE_SLIDE_DISK_ID, 1, 0.0, true, 0},
			{SLIDE_AMOUNT_DISK_ID, 0, 5.0, false, 0},
		};
		extern bool g_break_output_extent_hint;
		g_break_output_extent_hint = true;
		RenderResult r = run_render(1920, 1080, ovs, false, 30);
		g_break_output_extent_hint = false;
		char detail[256];
		std::snprintf(detail, sizeof(detail), "frac_transparent=%.3f avg_red=%.0f",
			r.frac_transparent, r.avg_red);
		check("output extent_hint=0 + slide-only (FAST PATH): not transparent",
			r.frac_transparent < 0.1, detail);
	}

	// Test 4c: SLOW PATH with broken output extent_hint.
	// Iterate(src=output, NULL area) might write nothing if AE consults
	// output's extent_hint for bounds. Fix forces output extent to full.
	{
		std::vector<ParamOverride> ovs = {
			{AMOUNT_DISK_ID, 0, 100.0, false, 0},
			{ENABLE_SLIDE_DISK_ID, 1, 0.0, true, 0},
			{ENABLE_LIGHT_DISK_ID, 1, 0.0, true, 0},
			{ENABLE_BLUR_DISK_ID, 1, 0.0, true, 0},
			{LIGHT_AMOUNT_DISK_ID, 0, 200.0, false, 0},
			{BLUR_AMOUNT_DISK_ID, 0, 50.0, false, 0},
			{SLIDE_AMOUNT_DISK_ID, 0, 50.0, false, 0},
		};
		extern bool g_break_output_extent_hint;
		g_break_output_extent_hint = true;
		RenderResult r = run_render(1920, 1080, ovs, false, 30);
		g_break_output_extent_hint = false;
		char detail[256];
		std::snprintf(detail, sizeof(detail), "frac_transparent=%.3f avg_red=%.0f",
			r.frac_transparent, r.avg_red);
		check("output extent_hint=0 + Hard Shake (SLOW PATH): not transparent",
			r.frac_transparent < 0.3, detail);
	}

	// Test 5: HARD SHAKE — exact saved values from user's failing preset.
	// Light=on, Blur=on, Slide=on, all moderate amounts. Test at frame 30
	// (t=1s) so the engine produces non-zero values, forcing the slow path.
	{
		std::vector<ParamOverride> ovs = {
			{AMOUNT_DISK_ID,          0, 100.0, false, 0},
			{ENABLE_LIGHT_DISK_ID,    1, 0.0,   true,  0},
			{ENABLE_BLUR_DISK_ID,     1, 0.0,   true,  0},
			{ENABLE_SLIDE_DISK_ID,    1, 0.0,   true,  0},
			{LIGHT_AMOUNT_DISK_ID,    0, 200.0, false, 0},
			{BLUR_AMOUNT_DISK_ID,     0, 50.0,  false, 0},
			{SLIDE_AMOUNT_DISK_ID,    0, 50.0,  false, 0},
			{LIGHT_TWITCHES_DISK_ID,  0, 1.0,   false, 0},
			{BLUR_TWITCHES_DISK_ID,   0, 1.0,   false, 0},
			{SLIDE_TWITCHES_DISK_ID,  0, 1.0,   false, 0},
			{SLIDE_DIRECTION_DISK_ID, 0, 190.0, false, 0},
		};
		// Test at multiple frames to capture different engine state.
		for (A_long ct : {0, 15, 30, 45, 60}) {
			RenderResult r = run_render(1920, 1080, ovs, false, ct);
			char detail[256];
			std::snprintf(detail, sizeof(detail),
				"t=%ld frac_transparent=%.3f avg_red=%.0f", (long)ct,
				r.frac_transparent, r.avg_red);
			char name[128];
			std::snprintf(name, sizeof(name),
				"Hard Shake repro @ t=%ld: <30%% transparent", (long)ct);
			check(name, r.frac_transparent < 0.3, detail);
		}
	}

	std::printf("\n%s (%d failure%s)\n",
		failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
		failures, failures == 1 ? "" : "s");
	return failures == 0 ? 0 : 1;
}
