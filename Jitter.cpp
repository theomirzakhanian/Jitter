/*******************************************************************/
/*                                                                 */
/*                      Jitter Plugin                              */
/*           Apple Silicon native port of Video Copilot Twitch     */
/*                                                                 */
/*  Param structure: 69 params across 6 operators —                */
/*  Slide, Scale, Time, Color, Light, Blur.                        */
/*                                                                 */
/*  Implemented operators (v1):                                    */
/*    Slide  — XY motion + Direction/Spread + RGB Split sub-param  */
/*    Scale  — zoom around configurable origin                     */
/*    Time   — frame offset with one-/two-direction clamping       */
/*    Behaviour controls — Ease In, Ease Out, Randomize Minimum    */
/*                                                                 */
/*  v3 stubs (UI matches Twitch, no render impact yet):            */
/*    Color  — HSL filter                                          */
/*    Light  — glow/highlight bloom                                */
/*    Blur   — FFT-based motion blur                               */
/*    Slide/Scale Motion Blur, Use Lens Blur, Show Twitch Only     */
/*                                                                 */
/*******************************************************************/

#include "Jitter.h"
#include "JitterEngine.h"

#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <new>

// ===================================================================
// Debug logging — appends to /tmp/jitter_debug.log so we can see how
// far each render call got before AE killed the plugin.
// Set JITTER_DEBUG_LOG to 0 to disable for ship builds.
// ===================================================================
#define JITTER_DEBUG_LOG 0

static void JLog(const char *fmt, ...) {
#if JITTER_DEBUG_LOG
	FILE *f = std::fopen("/tmp/jitter_debug.log", "a");
	if (!f) return;
	va_list ap;
	va_start(ap, fmt);
	std::vfprintf(f, fmt, ap);
	va_end(ap);
	std::fputc('\n', f);
	std::fclose(f);
#else
	(void)fmt;
#endif
}

// Single-TU build: pull engine implementation in. Avoids needing to add
// JitterEngine.cpp to the existing Xcode target (which predates the engine
// split). Proper fix is a project.pbxproj edit; until then this keeps
// Jitter.cpp self-contained.
#include "JitterEngine.cpp"

// ===================================================================
// Pre-render scratch
// ===================================================================
struct JitterPreRenderData {
	jitter::Output	values;
	double			slide_direction_deg;
	double			slide_spread_pct;
	double			slide_tendency_pct;
	double			scale_origin_x_pct;
	double			scale_origin_y_pct;
	A_long			time_direction;		// TIME_DIR_*
	A_long			light_behaviour;	// LIGHT_BEH_*
	bool			color_colorize;
	bool			show_twitch_only;
};

static void DeleteJitterPreRenderData(void *p) {
	delete static_cast<JitterPreRenderData*>(p);
}

// ===================================================================
// RGB Split helpers — bilinear sample with out-of-bounds → transparent black
// ===================================================================
static inline PF_Pixel SamplePixel8(const PF_EffectWorld *w, double x, double y) {
	int x0 = static_cast<int>(std::floor(x));
	int y0 = static_cast<int>(std::floor(y));
	double fx = x - x0;
	double fy = y - y0;

	PF_Pixel p[4];
	const int dx[4] = { 0, 1, 0, 1 };
	const int dy[4] = { 0, 0, 1, 1 };
	for (int i = 0; i < 4; i++) {
		int xi = x0 + dx[i];
		int yi = y0 + dy[i];
		if (xi < 0 || yi < 0 || xi >= w->width || yi >= w->height) {
			p[i].alpha = p[i].red = p[i].green = p[i].blue = 0;
		} else {
			p[i] = *reinterpret_cast<const PF_Pixel*>(
				reinterpret_cast<const A_u_char*>(w->data) + yi * w->rowbytes + xi * sizeof(PF_Pixel));
		}
	}

	double w00 = (1.0 - fx) * (1.0 - fy);
	double w10 =        fx  * (1.0 - fy);
	double w01 = (1.0 - fx) *        fy;
	double w11 =        fx  *        fy;

	PF_Pixel out;
	out.alpha = static_cast<A_u_char>(p[0].alpha*w00 + p[1].alpha*w10 + p[2].alpha*w01 + p[3].alpha*w11 + 0.5);
	out.red   = static_cast<A_u_char>(p[0].red  *w00 + p[1].red  *w10 + p[2].red  *w01 + p[3].red  *w11 + 0.5);
	out.green = static_cast<A_u_char>(p[0].green*w00 + p[1].green*w10 + p[2].green*w01 + p[3].green*w11 + 0.5);
	out.blue  = static_cast<A_u_char>(p[0].blue *w00 + p[1].blue *w10 + p[2].blue *w01 + p[3].blue *w11 + 0.5);
	return out;
}

static inline PF_Pixel16 SamplePixel16(const PF_EffectWorld *w, double x, double y) {
	int x0 = static_cast<int>(std::floor(x));
	int y0 = static_cast<int>(std::floor(y));
	double fx = x - x0;
	double fy = y - y0;

	PF_Pixel16 p[4];
	const int dx[4] = { 0, 1, 0, 1 };
	const int dy[4] = { 0, 0, 1, 1 };
	for (int i = 0; i < 4; i++) {
		int xi = x0 + dx[i];
		int yi = y0 + dy[i];
		if (xi < 0 || yi < 0 || xi >= w->width || yi >= w->height) {
			p[i].alpha = p[i].red = p[i].green = p[i].blue = 0;
		} else {
			p[i] = *reinterpret_cast<const PF_Pixel16*>(
				reinterpret_cast<const A_u_char*>(w->data) + yi * w->rowbytes + xi * sizeof(PF_Pixel16));
		}
	}

	double w00 = (1.0 - fx) * (1.0 - fy);
	double w10 =        fx  * (1.0 - fy);
	double w01 = (1.0 - fx) *        fy;
	double w11 =        fx  *        fy;

	PF_Pixel16 out;
	out.alpha = static_cast<A_u_short>(p[0].alpha*w00 + p[1].alpha*w10 + p[2].alpha*w01 + p[3].alpha*w11 + 0.5);
	out.red   = static_cast<A_u_short>(p[0].red  *w00 + p[1].red  *w10 + p[2].red  *w01 + p[3].red  *w11 + 0.5);
	out.green = static_cast<A_u_short>(p[0].green*w00 + p[1].green*w10 + p[2].green*w01 + p[3].green*w11 + 0.5);
	out.blue  = static_cast<A_u_short>(p[0].blue *w00 + p[1].blue *w10 + p[2].blue *w01 + p[3].blue *w11 + 0.5);
	return out;
}

struct RGBSplitInfo {
	const PF_EffectWorld	*src;
	double					offset_x;
	double					offset_y;
};

// ===================================================================
// Color operator — HSL hue rotation, in-place on temp world.
// Twitch uses HslRangeSelectionFilter; we do a simpler full-image hue
// rotation modulated by the Color engine value. Colorize and Randomize
// are wired but use the same hue-rotation core for v1.
// ===================================================================

struct ColorInfo {
	double	hue_shift;	// in [-0.5, 0.5] → full ±180° hue rotation
	bool	colorize;	// if true, force saturation = 1.0 (vivid)
};

static inline void rgb_to_hsl(double r, double g, double b, double &h, double &s, double &l) {
	double mx = std::fmax(r, std::fmax(g, b));
	double mn = std::fmin(r, std::fmin(g, b));
	l = (mx + mn) * 0.5;
	if (mx == mn) { h = 0; s = 0; return; }
	double d = mx - mn;
	s = (l > 0.5) ? d / (2.0 - mx - mn) : d / (mx + mn);
	if      (mx == r) h = ((g - b) / d) + (g < b ? 6.0 : 0.0);
	else if (mx == g) h = ((b - r) / d) + 2.0;
	else              h = ((r - g) / d) + 4.0;
	h /= 6.0;
}

static inline double hue2rgb(double p, double q, double t) {
	if (t < 0.0) t += 1.0;
	if (t > 1.0) t -= 1.0;
	if (t < 1.0/6.0) return p + (q - p) * 6.0 * t;
	if (t < 0.5)     return q;
	if (t < 2.0/3.0) return p + (q - p) * (2.0/3.0 - t) * 6.0;
	return p;
}

static inline void hsl_to_rgb(double h, double s, double l, double &r, double &g, double &b) {
	if (s == 0) { r = g = b = l; return; }
	double q = (l < 0.5) ? l * (1.0 + s) : l + s - l * s;
	double p = 2.0 * l - q;
	r = hue2rgb(p, q, h + 1.0/3.0);
	g = hue2rgb(p, q, h);
	b = hue2rgb(p, q, h - 1.0/3.0);
}

static PF_Err ColorPixel8(void *refcon, A_long x, A_long y, PF_Pixel *in, PF_Pixel *out) {
	(void)x; (void)y;
	ColorInfo *info = static_cast<ColorInfo*>(refcon);
	double r = in->red / 255.0, g = in->green / 255.0, b = in->blue / 255.0;
	double h, s, l;
	rgb_to_hsl(r, g, b, h, s, l);
	h = std::fmod(h + info->hue_shift + 1.0, 1.0);
	if (info->colorize) s = std::fmax(s, 0.5);
	hsl_to_rgb(h, s, l, r, g, b);
	out->alpha = in->alpha;
	out->red   = static_cast<A_u_char>(std::round(std::fmin(255.0, std::fmax(0.0, r * 255.0))));
	out->green = static_cast<A_u_char>(std::round(std::fmin(255.0, std::fmax(0.0, g * 255.0))));
	out->blue  = static_cast<A_u_char>(std::round(std::fmin(255.0, std::fmax(0.0, b * 255.0))));
	return PF_Err_NONE;
}

static PF_Err ColorPixel16(void *refcon, A_long x, A_long y, PF_Pixel16 *in, PF_Pixel16 *out) {
	(void)x; (void)y;
	ColorInfo *info = static_cast<ColorInfo*>(refcon);
	const double M = 32768.0;
	double r = in->red / M, g = in->green / M, b = in->blue / M;
	double h, s, l;
	rgb_to_hsl(r, g, b, h, s, l);
	h = std::fmod(h + info->hue_shift + 1.0, 1.0);
	if (info->colorize) s = std::fmax(s, 0.5);
	hsl_to_rgb(h, s, l, r, g, b);
	out->alpha = in->alpha;
	out->red   = static_cast<A_u_short>(std::round(std::fmin(M, std::fmax(0.0, r * M))));
	out->green = static_cast<A_u_short>(std::round(std::fmin(M, std::fmax(0.0, g * M))));
	out->blue  = static_cast<A_u_short>(std::round(std::fmin(M, std::fmax(0.0, b * M))));
	return PF_Err_NONE;
}

// ===================================================================
// Light operator — additive highlight/shadow bloom.
// Twitch uses gamma-corrected Bezier-disk bokeh (proper lens blur);
// we do a simpler thresholded brightness lift/cut for v1. Light
// Behaviour: Brighter (lift highlights), Darker (deepen shadows),
// Both (lift highs + deepen shadows together).
// ===================================================================

struct LightInfo {
	double	amount;		// signed — sign controls direction in Both mode
	A_long	behaviour;	// LIGHT_BEH_*
};

static PF_Err LightPixel8(void *refcon, A_long x, A_long y, PF_Pixel *in, PF_Pixel *out) {
	(void)x; (void)y;
	LightInfo *info = static_cast<LightInfo*>(refcon);
	double r = in->red / 255.0, g = in->green / 255.0, b = in->blue / 255.0;
	// Rec.709 luma
	double y_lum = 0.2126 * r + 0.7152 * g + 0.0722 * b;
	double bright_w = std::fmax(0.0, y_lum - 0.5) * 2.0;	// 0..1 over upper half
	double shadow_w = std::fmax(0.0, 0.5 - y_lum) * 2.0;	// 0..1 over lower half
	double scale = 0.0;
	if (info->behaviour == LIGHT_BEH_BRIGHTER) scale =  info->amount * bright_w;
	if (info->behaviour == LIGHT_BEH_DARKER)   scale = -info->amount * shadow_w;
	if (info->behaviour == LIGHT_BEH_BOTH)     scale =  info->amount * (bright_w - shadow_w);
	r = std::fmin(1.0, std::fmax(0.0, r + scale));
	g = std::fmin(1.0, std::fmax(0.0, g + scale));
	b = std::fmin(1.0, std::fmax(0.0, b + scale));
	out->alpha = in->alpha;
	out->red   = static_cast<A_u_char>(std::round(r * 255.0));
	out->green = static_cast<A_u_char>(std::round(g * 255.0));
	out->blue  = static_cast<A_u_char>(std::round(b * 255.0));
	return PF_Err_NONE;
}

static PF_Err LightPixel16(void *refcon, A_long x, A_long y, PF_Pixel16 *in, PF_Pixel16 *out) {
	(void)x; (void)y;
	LightInfo *info = static_cast<LightInfo*>(refcon);
	const double M = 32768.0;
	double r = in->red / M, g = in->green / M, b = in->blue / M;
	double y_lum = 0.2126 * r + 0.7152 * g + 0.0722 * b;
	double bright_w = std::fmax(0.0, y_lum - 0.5) * 2.0;
	double shadow_w = std::fmax(0.0, 0.5 - y_lum) * 2.0;
	double scale = 0.0;
	if (info->behaviour == LIGHT_BEH_BRIGHTER) scale =  info->amount * bright_w;
	if (info->behaviour == LIGHT_BEH_DARKER)   scale = -info->amount * shadow_w;
	if (info->behaviour == LIGHT_BEH_BOTH)     scale =  info->amount * (bright_w - shadow_w);
	r = std::fmin(1.0, std::fmax(0.0, r + scale));
	g = std::fmin(1.0, std::fmax(0.0, g + scale));
	b = std::fmin(1.0, std::fmax(0.0, b + scale));
	out->alpha = in->alpha;
	out->red   = static_cast<A_u_short>(std::round(r * M));
	out->green = static_cast<A_u_short>(std::round(g * M));
	out->blue  = static_cast<A_u_short>(std::round(b * M));
	return PF_Err_NONE;
}

// ===================================================================
// Blur operator — directional motion blur, gaussian-weighted N-tap.
// Twitch uses FFT-based convolution; we approximate with a direct N-tap
// gaussian along Slide Direction (or random direction if Slide off).
// ===================================================================

struct BlurInfo {
	const PF_EffectWorld	*src;	// blur reads from this world
	double					dx;		// step vector in pixels per tap
	double					dy;
	int						radius;	// taps each side
};

static PF_Err BlurPixel8(void *refcon, A_long x, A_long y, PF_Pixel *in, PF_Pixel *out) {
	(void)in;
	BlurInfo *info = static_cast<BlurInfo*>(refcon);
	double sumA = 0, sumR = 0, sumG = 0, sumB = 0, sumW = 0;
	int R = info->radius;
	double sigma = std::fmax(1.0, R / 2.0);
	double inv2sig2 = 1.0 / (2.0 * sigma * sigma);
	for (int i = -R; i <= R; i++) {
		double w = std::exp(-(i * i) * inv2sig2);
		PF_Pixel s = SamplePixel8(info->src, x + i * info->dx, y + i * info->dy);
		sumA += w * s.alpha;
		sumR += w * s.red;
		sumG += w * s.green;
		sumB += w * s.blue;
		sumW += w;
	}
	out->alpha = static_cast<A_u_char>(std::round(sumA / sumW));
	out->red   = static_cast<A_u_char>(std::round(sumR / sumW));
	out->green = static_cast<A_u_char>(std::round(sumG / sumW));
	out->blue  = static_cast<A_u_char>(std::round(sumB / sumW));
	return PF_Err_NONE;
}

static PF_Err BlurPixel16(void *refcon, A_long x, A_long y, PF_Pixel16 *in, PF_Pixel16 *out) {
	(void)in;
	BlurInfo *info = static_cast<BlurInfo*>(refcon);
	double sumA = 0, sumR = 0, sumG = 0, sumB = 0, sumW = 0;
	int R = info->radius;
	double sigma = std::fmax(1.0, R / 2.0);
	double inv2sig2 = 1.0 / (2.0 * sigma * sigma);
	for (int i = -R; i <= R; i++) {
		double w = std::exp(-(i * i) * inv2sig2);
		PF_Pixel16 s = SamplePixel16(info->src, x + i * info->dx, y + i * info->dy);
		sumA += w * s.alpha;
		sumR += w * s.red;
		sumG += w * s.green;
		sumB += w * s.blue;
		sumW += w;
	}
	out->alpha = static_cast<A_u_short>(std::round(sumA / sumW));
	out->red   = static_cast<A_u_short>(std::round(sumR / sumW));
	out->green = static_cast<A_u_short>(std::round(sumG / sumW));
	out->blue  = static_cast<A_u_short>(std::round(sumB / sumW));
	return PF_Err_NONE;
}

static PF_Err RGBSplitPixel8(void *refcon, A_long x, A_long y, PF_Pixel *in, PF_Pixel *out) {
	(void)in;
	RGBSplitInfo *info = static_cast<RGBSplitInfo*>(refcon);
	PF_Pixel r = SamplePixel8(info->src, x - info->offset_x, y - info->offset_y);
	PF_Pixel g = SamplePixel8(info->src, x,                  y);
	PF_Pixel b = SamplePixel8(info->src, x + info->offset_x, y + info->offset_y);
	out->red   = r.red;
	out->green = g.green;
	out->blue  = b.blue;
	out->alpha = g.alpha;
	return PF_Err_NONE;
}

static PF_Err RGBSplitPixel16(void *refcon, A_long x, A_long y, PF_Pixel16 *in, PF_Pixel16 *out) {
	(void)in;
	RGBSplitInfo *info = static_cast<RGBSplitInfo*>(refcon);
	PF_Pixel16 r = SamplePixel16(info->src, x - info->offset_x, y - info->offset_y);
	PF_Pixel16 g = SamplePixel16(info->src, x,                  y);
	PF_Pixel16 b = SamplePixel16(info->src, x + info->offset_x, y + info->offset_y);
	out->red   = r.red;
	out->green = g.green;
	out->blue  = b.blue;
	out->alpha = g.alpha;
	return PF_Err_NONE;
}

// ===================================================================
// Forward declarations
// ===================================================================
static PF_Err About(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output);
static PF_Err GlobalSetup(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output);
static PF_Err ParamsSetup(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output);
static PF_Err Render(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output);
static PF_Err SmartPreRender(PF_InData *in_data, PF_OutData *out_data, PF_PreRenderExtra *extra);
static PF_Err SmartRender(PF_InData *in_data, PF_OutData *out_data, PF_SmartRenderExtra *extra);

// ===================================================================
// About
// ===================================================================
static PF_Err
About(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
	AEGP_SuiteHandler suites(in_data->pica_basicP);
	suites.ANSICallbacksSuite1()->sprintf(
		out_data->return_msg,
		"Jitter / Twitch v%d.%d\rApple Silicon native port of Video Copilot Twitch.",
		MAJOR_VERSION, MINOR_VERSION);
	return PF_Err_NONE;
}

// ===================================================================
// GlobalSetup — outflags match Twitch (plus our SUPPORTS_THREADED_RENDERING).
// ===================================================================
static PF_Err
GlobalSetup(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
	out_data->my_version = PF_VERSION(MAJOR_VERSION, MINOR_VERSION, BUG_VERSION, STAGE_VERSION, BUILD_VERSION);

	// Twitch's flags + ours. Match Twitch on what we honor; add MFR support.
	// NON_PARAM_VARY is required because output depends on current_time even
	// when every param is constant (the engine buckets time into events). On a
	// still image AE would otherwise treat all frames as identical and serve
	// stale cache.
	out_data->out_flags  = PF_OutFlag_DEEP_COLOR_AWARE |
	                       PF_OutFlag_USE_OUTPUT_EXTENT |
	                       PF_OutFlag_WIDE_TIME_INPUT |
	                       PF_OutFlag_NON_PARAM_VARY;
	out_data->out_flags2 = PF_OutFlag2_SUPPORTS_SMART_RENDER |
	                       PF_OutFlag2_SUPPORTS_THREADED_RENDERING |
	                       PF_OutFlag2_PARAM_GROUP_START_COLLAPSED_FLAG |
	                       PF_OutFlag2_REVEALS_ZERO_ALPHA;
	return PF_Err_NONE;
}

// ===================================================================
// ParamsSetup — exactly 68 PF_ADD_* calls (in addition to the implicit
// input layer at index 0). num_params = 69.
// ===================================================================
static PF_Err
ParamsSetup(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
	PF_Err		err = PF_Err_NONE;
	PF_ParamDef	def;

	#define ADD_FLOAT(NAME, MIN, MAX, DFLT, PREC, DISK_ID) \
		do { AEFX_CLR_STRUCT(def); \
			PF_ADD_FLOAT_SLIDERX(NAME, (MIN), (MAX), (MIN), (MAX), (DFLT), \
				(PREC), 0, 0, (DISK_ID)); \
		} while (0)

	#define ADD_CHECK(NAME, DFLT, DISK_ID) \
		do { AEFX_CLR_STRUCT(def); \
			PF_ADD_CHECKBOXX(NAME, (DFLT), 0, (DISK_ID)); \
		} while (0)

	#define ADD_TOPIC(NAME, DISK_ID) \
		do { AEFX_CLR_STRUCT(def); PF_ADD_TOPIC(NAME, (DISK_ID)); } while (0)

	#define ADD_END_TOPIC(DISK_ID) \
		do { AEFX_CLR_STRUCT(def); PF_END_TOPIC(DISK_ID); } while (0)

	// === Order matches Twitch's __DATA __data physical layout exactly ===

	// Master Amount (disk ID 2)
	ADD_FLOAT(STR(StrID_Amount), AMOUNT_MIN, AMOUNT_MAX, AMOUNT_DFLT,
		PF_Precision_HUNDREDTHS, AMOUNT_DISK_ID);

	// Master Speed (disk ID 1) — Twitch has this; engine currently ignores its value
	// but the slot exists so saved Twitch projects deserialize cleanly.
	ADD_FLOAT(STR(StrID_Speed), 0.0, 100.0, 100.0,
		PF_Precision_HUNDREDTHS, SPEED_DISK_ID);

	// Enable group — alphabetical: Blur, Color, Light, Scale, Slide, Time
	ADD_TOPIC(STR(StrID_Enable_Group), ENABLE_GROUP_START_DISK_ID);
	ADD_CHECK(STR(StrID_Enable_Blur),  FALSE, ENABLE_BLUR_DISK_ID);
	ADD_CHECK(STR(StrID_Enable_Color), FALSE, ENABLE_COLOR_DISK_ID);
	ADD_CHECK(STR(StrID_Enable_Light), FALSE, ENABLE_LIGHT_DISK_ID);
	ADD_CHECK(STR(StrID_Enable_Scale), FALSE, ENABLE_SCALE_DISK_ID);
	ADD_CHECK(STR(StrID_Enable_Slide), TRUE,  ENABLE_SLIDE_DISK_ID);
	ADD_CHECK(STR(StrID_Enable_Time),  FALSE, ENABLE_TIME_DISK_ID);
	ADD_END_TOPIC(ENABLE_GROUP_END_DISK_ID);

	// Behaviour group — Twitch puts Master Seed AND Show Twitch Only INSIDE this group
	ADD_TOPIC(STR(StrID_Behaviour_Group), BEHAVIOUR_GROUP_START_DISK_ID);
	ADD_FLOAT(STR(StrID_Behaviour_Ease_In),  0.0, 100.0, EASE_IN_DFLT,
		PF_Precision_HUNDREDTHS, BEHAVIOUR_EASE_IN_DISK_ID);
	ADD_FLOAT(STR(StrID_Behaviour_Ease_Out), 0.0, 100.0, EASE_OUT_DFLT,
		PF_Precision_HUNDREDTHS, BEHAVIOUR_EASE_OUT_DISK_ID);
	ADD_CHECK(STR(StrID_Show_Twitch_Only), FALSE, SHOW_TWITCH_ONLY_DISK_ID);
	ADD_FLOAT(STR(StrID_Seed), SEED_MIN, SEED_MAX, SEED_DFLT,
		PF_Precision_INTEGER, SEED_DISK_ID);
	ADD_FLOAT(STR(StrID_Behaviour_Randomize_Minimum), 0.0, 100.0, RANDOMIZE_MIN_DFLT,
		PF_Precision_HUNDREDTHS, BEHAVIOUR_RANDOMIZE_MIN_DISK_ID);
	ADD_END_TOPIC(BEHAVIOUR_GROUP_END_DISK_ID);

	// Operator Controls — alphabetical sub-groups: Blur, Color, Light, Scale, Slide, Time
	ADD_TOPIC(STR(StrID_Operator_Controls_Group), OPERATOR_CONTROLS_GROUP_START_DISK_ID);

	// --- Blur — Amount, Twitches, Tint, Holdout, Sharpness, Boost, Opacity,
	//     Transfer Mode, Aspect, Use Lens Blur, Seed
	ADD_TOPIC(STR(StrID_Blur_Group), BLUR_GROUP_START_DISK_ID);
	ADD_FLOAT(STR(StrID_Blur_Amount),   OP_AMOUNT_MIN, OP_AMOUNT_MAX, OP_AMOUNT_DFLT,
		PF_Precision_HUNDREDTHS, BLUR_AMOUNT_DISK_ID);
	ADD_FLOAT(STR(StrID_Blur_Twitches), OP_TWITCHES_MIN, OP_TWITCHES_MAX, OP_TWITCHES_DFLT,
		PF_Precision_HUNDREDTHS, BLUR_TWITCHES_DISK_ID);
	AEFX_CLR_STRUCT(def);
	PF_ADD_COLOR(STR(StrID_Blur_Tint), PF_MAX_CHAN8, PF_MAX_CHAN8, PF_MAX_CHAN8, BLUR_TINT_DISK_ID);
	ADD_FLOAT(STR(StrID_Blur_Holdout), 0.0, 100.0, 0.0,
		PF_Precision_HUNDREDTHS, BLUR_HOLDOUT_DISK_ID);
	ADD_FLOAT(STR(StrID_Blur_Holdout_Sharpness), 0.0, 100.0, 50.0,
		PF_Precision_HUNDREDTHS, BLUR_HOLDOUT_SHARPNESS_DISK_ID);
	ADD_FLOAT(STR(StrID_Blur_Boost), 0.0, 100.0, 0.0,
		PF_Precision_HUNDREDTHS, BLUR_BOOST_DISK_ID);
	ADD_FLOAT(STR(StrID_Blur_Opacity), 0.0, 100.0, 100.0,
		PF_Precision_HUNDREDTHS, BLUR_OPACITY_DISK_ID);
	AEFX_CLR_STRUCT(def);
	PF_ADD_POPUPX(STR(StrID_Blur_Transfer_Mode), 4, BLUR_XFER_NORMAL,
		STR(StrID_Blur_Transfer_Mode_Choices), 0, BLUR_TRANSFER_MODE_DISK_ID);
	ADD_FLOAT(STR(StrID_Blur_Aspect), -100.0, 100.0, 0.0,
		PF_Precision_HUNDREDTHS, BLUR_ASPECT_DISK_ID);
	ADD_CHECK(STR(StrID_Scale_Use_Lens_Blur), FALSE, BLUR_USE_LENS_BLUR_DISK_ID);
	ADD_FLOAT(STR(StrID_Blur_Seed), OP_SEED_MIN, OP_SEED_MAX, OP_SEED_DFLT,
		PF_Precision_INTEGER, BLUR_SEED_DISK_ID);
	ADD_END_TOPIC(BLUR_GROUP_END_DISK_ID);

	// --- Color — Amount, Twitches, Colorize, Randomize, Seed
	ADD_TOPIC(STR(StrID_Color_Group), COLOR_GROUP_START_DISK_ID);
	ADD_FLOAT(STR(StrID_Color_Amount),   OP_AMOUNT_MIN, OP_AMOUNT_MAX, OP_AMOUNT_DFLT,
		PF_Precision_HUNDREDTHS, COLOR_AMOUNT_DISK_ID);
	ADD_FLOAT(STR(StrID_Color_Twitches), OP_TWITCHES_MIN, OP_TWITCHES_MAX, OP_TWITCHES_DFLT,
		PF_Precision_HUNDREDTHS, COLOR_TWITCHES_DISK_ID);
	ADD_CHECK(STR(StrID_Color_Colorize), FALSE, COLOR_COLORIZE_DISK_ID);
	ADD_FLOAT(STR(StrID_Color_Randomize), 0.0, 100.0, 0.0,
		PF_Precision_HUNDREDTHS, COLOR_RANDOMIZE_DISK_ID);
	ADD_FLOAT(STR(StrID_Color_Seed), OP_SEED_MIN, OP_SEED_MAX, OP_SEED_DFLT,
		PF_Precision_INTEGER, COLOR_SEED_DISK_ID);
	ADD_END_TOPIC(COLOR_GROUP_END_DISK_ID);

	// --- Light — Amount, Twitches, Behaviour, Seed
	ADD_TOPIC(STR(StrID_Light_Group), LIGHT_GROUP_START_DISK_ID);
	ADD_FLOAT(STR(StrID_Light_Amount),   OP_AMOUNT_MIN, OP_AMOUNT_MAX, OP_AMOUNT_DFLT,
		PF_Precision_HUNDREDTHS, LIGHT_AMOUNT_DISK_ID);
	ADD_FLOAT(STR(StrID_Light_Twitches), OP_TWITCHES_MIN, OP_TWITCHES_MAX, OP_TWITCHES_DFLT,
		PF_Precision_HUNDREDTHS, LIGHT_TWITCHES_DISK_ID);
	AEFX_CLR_STRUCT(def);
	PF_ADD_POPUPX(STR(StrID_Light_Behaviour), 3, LIGHT_BEH_BOTH,
		STR(StrID_Light_Behaviour_Choices), 0, LIGHT_BEHAVIOUR_DISK_ID);
	ADD_FLOAT(STR(StrID_Light_Seed), OP_SEED_MIN, OP_SEED_MAX, OP_SEED_DFLT,
		PF_Precision_INTEGER, LIGHT_SEED_DISK_ID);
	ADD_END_TOPIC(LIGHT_GROUP_END_DISK_ID);

	// --- Scale — Amount, Twitches, Origin, Origin Randomize, Motion Blur, Seed
	ADD_TOPIC(STR(StrID_Scale_Group), SCALE_GROUP_START_DISK_ID);
	ADD_FLOAT(STR(StrID_Scale_Amount),   OP_AMOUNT_MIN, OP_AMOUNT_MAX, OP_AMOUNT_DFLT,
		PF_Precision_HUNDREDTHS, SCALE_AMOUNT_DISK_ID);
	ADD_FLOAT(STR(StrID_Scale_Twitches), OP_TWITCHES_MIN, OP_TWITCHES_MAX, OP_TWITCHES_DFLT,
		PF_Precision_HUNDREDTHS, SCALE_TWITCHES_DISK_ID);
	AEFX_CLR_STRUCT(def);
	PF_ADD_POINT(STR(StrID_Scale_Origin), 50, 50, FALSE, SCALE_ORIGIN_DISK_ID);
	ADD_FLOAT(STR(StrID_Scale_Origin_Randomize), 0.0, 100.0, 0.0,
		PF_Precision_HUNDREDTHS, SCALE_ORIGIN_RANDOMIZE_DISK_ID);
	ADD_FLOAT(STR(StrID_Scale_Motion_Blur), 0.0, 100.0, 0.0,
		PF_Precision_HUNDREDTHS, SCALE_MOTION_BLUR_DISK_ID);
	ADD_FLOAT(STR(StrID_Scale_Seed), OP_SEED_MIN, OP_SEED_MAX, OP_SEED_DFLT,
		PF_Precision_INTEGER, SCALE_SEED_DISK_ID);
	ADD_END_TOPIC(SCALE_GROUP_END_DISK_ID);

	// --- Slide — Amount, Twitches, Direction, Spread, Tendency, RGB Split, Motion Blur, Seed
	ADD_TOPIC(STR(StrID_Slide_Group), SLIDE_GROUP_START_DISK_ID);
	ADD_FLOAT(STR(StrID_Slide_Amount),   OP_AMOUNT_MIN, OP_AMOUNT_MAX, OP_AMOUNT_DFLT,
		PF_Precision_HUNDREDTHS, SLIDE_AMOUNT_DISK_ID);
	ADD_FLOAT(STR(StrID_Slide_Twitches), OP_TWITCHES_MIN, OP_TWITCHES_MAX, OP_TWITCHES_DFLT,
		PF_Precision_HUNDREDTHS, SLIDE_TWITCHES_DISK_ID);
	ADD_FLOAT(STR(StrID_Slide_Direction), 0.0, 360.0, SLIDE_DIR_DFLT,
		PF_Precision_HUNDREDTHS, SLIDE_DIRECTION_DISK_ID);
	ADD_FLOAT(STR(StrID_Slide_Spread),   0.0, 100.0, SLIDE_SPREAD_DFLT,
		PF_Precision_HUNDREDTHS, SLIDE_SPREAD_DISK_ID);
	ADD_FLOAT(STR(StrID_Slide_Tendency), 0.0, 100.0, SLIDE_TENDENCY_DFLT,
		PF_Precision_HUNDREDTHS, SLIDE_TENDENCY_DISK_ID);
	ADD_FLOAT(STR(StrID_Slide_RGB_Split), 0.0, 100.0, SLIDE_RGB_SPLIT_DFLT,
		PF_Precision_HUNDREDTHS, SLIDE_RGB_SPLIT_DISK_ID);
	ADD_FLOAT(STR(StrID_Slide_Motion_Blur), 0.0, 100.0, SLIDE_MOBLUR_DFLT,
		PF_Precision_HUNDREDTHS, SLIDE_MOTION_BLUR_DISK_ID);
	ADD_FLOAT(STR(StrID_Slide_Seed), OP_SEED_MIN, OP_SEED_MAX, OP_SEED_DFLT,
		PF_Precision_INTEGER, SLIDE_SEED_DISK_ID);
	ADD_END_TOPIC(SLIDE_GROUP_END_DISK_ID);

	// --- Time — Amount, Twitches, Direction, Seed
	ADD_TOPIC(STR(StrID_Time_Group), TIME_GROUP_START_DISK_ID);
	ADD_FLOAT(STR(StrID_Time_Amount),   OP_AMOUNT_MIN, OP_AMOUNT_MAX, OP_AMOUNT_DFLT,
		PF_Precision_HUNDREDTHS, TIME_AMOUNT_DISK_ID);
	ADD_FLOAT(STR(StrID_Time_Twitches), OP_TWITCHES_MIN, OP_TWITCHES_MAX, OP_TWITCHES_DFLT,
		PF_Precision_HUNDREDTHS, TIME_TWITCHES_DISK_ID);
	AEFX_CLR_STRUCT(def);
	PF_ADD_POPUPX(STR(StrID_Time_Direction), 3, TIME_DIR_BOTH,
		STR(StrID_Time_Direction_Choices), 0, TIME_DIRECTION_DISK_ID);
	ADD_FLOAT(STR(StrID_Time_Seed), OP_SEED_MIN, OP_SEED_MAX, OP_SEED_DFLT,
		PF_Precision_INTEGER, TIME_SEED_DISK_ID);
	ADD_END_TOPIC(TIME_GROUP_END_DISK_ID);

	ADD_END_TOPIC(OPERATOR_CONTROLS_GROUP_END_DISK_ID);

	#undef ADD_FLOAT
	#undef ADD_CHECK
	#undef ADD_TOPIC
	#undef ADD_END_TOPIC

	out_data->num_params = JITTER_NUM_PARAMS;
	return err;
}

// ===================================================================
// SmartPreRender — full implementation. Reads every param including
// POPUP / POINT / COLOR types, wires Behaviour controls into the engine
// config, applies Slide Direction/Spread modulation to the slide vector,
// clamps Time Direction. Color/Light/Blur params are read so the engine
// can advance their event sequences (for future render impl), but the
// engine currently no-ops their render impact.
// ===================================================================
static PF_Err
SmartPreRender(PF_InData *in_data, PF_OutData *out_data, PF_PreRenderExtra *extra)
{
	JLog("---- SmartPreRender start ---- t=%ld ts=%lu", (long)in_data->current_time, (unsigned long)in_data->time_scale);
	PF_Err		err = PF_Err_NONE;
	PF_ParamDef	def;

	#define CHECK_F(IDX, FIELD) do { \
		AEFX_CLR_STRUCT(def); \
		ERR(PF_CHECKOUT_PARAM(in_data, (IDX), \
			in_data->current_time, in_data->time_step, in_data->time_scale, &def)); \
		(FIELD) = def.u.fs_d.value; \
	} while (0)
	#define CHECK_B(IDX, FIELD) do { \
		AEFX_CLR_STRUCT(def); \
		ERR(PF_CHECKOUT_PARAM(in_data, (IDX), \
			in_data->current_time, in_data->time_step, in_data->time_scale, &def)); \
		(FIELD) = (def.u.bd.value != 0); \
	} while (0)
	#define CHECK_POPUP(IDX, FIELD) do { \
		AEFX_CLR_STRUCT(def); \
		ERR(PF_CHECKOUT_PARAM(in_data, (IDX), \
			in_data->current_time, in_data->time_step, in_data->time_scale, &def)); \
		(FIELD) = def.u.pd.value; \
	} while (0)

	jitter::Config cfg;
	double seed_tmp = 0;
	bool   show_twitch_only = false;
	double slide_direction = 0, slide_spread = 100;
	double scale_origin_x_fixed = 0, scale_origin_y_fixed = 0;
	A_long time_direction = TIME_DIR_BOTH;

	// ---- Master ----
	CHECK_F(AMOUNT_PARAM, cfg.master.amount);

	// ---- Enable group ----
	CHECK_B(ENABLE_SLIDE, cfg.slide.enabled);
	CHECK_B(ENABLE_SCALE, cfg.scale.enabled);
	CHECK_B(ENABLE_TIME,  cfg.time_op.enabled);
	CHECK_B(ENABLE_COLOR, cfg.color.enabled);
	CHECK_B(ENABLE_LIGHT, cfg.light.enabled);
	CHECK_B(ENABLE_BLUR,  cfg.blur.enabled);

	// ---- Master Seed ----
	CHECK_F(SEED_PARAM, seed_tmp);
	cfg.master.seed = static_cast<uint32_t>(seed_tmp);

	// ---- Behaviour ----
	CHECK_F(BEHAVIOUR_EASE_IN,        cfg.master.ease_in);
	CHECK_F(BEHAVIOUR_EASE_OUT,       cfg.master.ease_out);
	CHECK_F(BEHAVIOUR_RANDOMIZE_MIN,  cfg.master.randomize_min);

	// ---- Slide ----
	CHECK_F(SLIDE_AMOUNT,    cfg.slide.amount);
	CHECK_F(SLIDE_TWITCHES,  cfg.slide.twitches_per_sec);
	CHECK_F(SLIDE_SEED,      seed_tmp);  cfg.slide.seed = static_cast<uint32_t>(seed_tmp);
	double slide_tendency = 0;
	CHECK_F(SLIDE_DIRECTION, slide_direction);
	CHECK_F(SLIDE_SPREAD,    slide_spread);
	CHECK_F(SLIDE_TENDENCY,  slide_tendency);
	{ double t; CHECK_F(SLIDE_MOTION_BLUR, t); (void)t; }	// shutter-angle integration: M6+
	double slide_rgb_split_amt = 0;
	CHECK_F(SLIDE_RGB_SPLIT, slide_rgb_split_amt);

	// ---- Scale ----
	CHECK_F(SCALE_AMOUNT,    cfg.scale.amount);
	CHECK_F(SCALE_TWITCHES,  cfg.scale.twitches_per_sec);
	CHECK_F(SCALE_SEED,      seed_tmp);  cfg.scale.seed = static_cast<uint32_t>(seed_tmp);
	{
		// PF_Param_POINT — values in fixed-point absolute layer pixels
		AEFX_CLR_STRUCT(def);
		ERR(PF_CHECKOUT_PARAM(in_data, SCALE_ORIGIN, in_data->current_time,
			in_data->time_step, in_data->time_scale, &def));
		scale_origin_x_fixed = static_cast<double>(def.u.td.x_value) / 65536.0;
		scale_origin_y_fixed = static_cast<double>(def.u.td.y_value) / 65536.0;
	}
	{ double t; CHECK_F(SCALE_ORIGIN_RANDOMIZE, t); (void)t; }
	{ double t; CHECK_F(SCALE_MOTION_BLUR, t);     (void)t; }
	// Use Lens Blur lives in the Blur operator group in Twitch — read in Blur section below

	// ---- Time ----
	CHECK_F(TIME_AMOUNT,    cfg.time_op.amount);
	CHECK_F(TIME_TWITCHES,  cfg.time_op.twitches_per_sec);
	CHECK_F(TIME_SEED,      seed_tmp);  cfg.time_op.seed = static_cast<uint32_t>(seed_tmp);
	CHECK_POPUP(TIME_DIRECTION, time_direction);

	// ---- Color ----
	bool color_colorize = false;
	CHECK_F(COLOR_AMOUNT,    cfg.color.amount);
	CHECK_F(COLOR_TWITCHES,  cfg.color.twitches_per_sec);
	CHECK_F(COLOR_SEED,      seed_tmp);  cfg.color.seed = static_cast<uint32_t>(seed_tmp);
	{ double t; CHECK_F(COLOR_RANDOMIZE, t); (void)t; }
	CHECK_B(COLOR_COLORIZE,  color_colorize);

	// ---- Light ----
	A_long light_behaviour = LIGHT_BEH_BOTH;
	CHECK_F(LIGHT_AMOUNT,    cfg.light.amount);
	CHECK_F(LIGHT_TWITCHES,  cfg.light.twitches_per_sec);
	CHECK_F(LIGHT_SEED,      seed_tmp);  cfg.light.seed = static_cast<uint32_t>(seed_tmp);
	CHECK_POPUP(LIGHT_BEHAVIOUR, light_behaviour);

	// ---- Blur ----
	CHECK_F(BLUR_AMOUNT,    cfg.blur.amount);
	CHECK_F(BLUR_TWITCHES,  cfg.blur.twitches_per_sec);
	CHECK_F(BLUR_SEED,      seed_tmp);  cfg.blur.seed = static_cast<uint32_t>(seed_tmp);
	{ double t; CHECK_F(BLUR_ASPECT, t);             (void)t; }
	{ double t; CHECK_F(BLUR_BOOST, t);              (void)t; }
	{ double t; CHECK_F(BLUR_HOLDOUT, t);            (void)t; }
	{ double t; CHECK_F(BLUR_HOLDOUT_SHARPNESS, t);  (void)t; }
	{ double t; CHECK_F(BLUR_OPACITY, t);            (void)t; }
	{
		AEFX_CLR_STRUCT(def);	// COLOR — read but not used until v3 Blur impl
		ERR(PF_CHECKOUT_PARAM(in_data, BLUR_TINT, in_data->current_time,
			in_data->time_step, in_data->time_scale, &def));
	}
	{ A_long t; CHECK_POPUP(BLUR_TRANSFER_MODE, t); (void)t; }
	{ bool   t; CHECK_B(BLUR_USE_LENS_BLUR, t);     (void)t; }

	CHECK_B(SHOW_TWITCH_ONLY, show_twitch_only);

	#undef CHECK_F
	#undef CHECK_B
	#undef CHECK_POPUP
	JLog("PreRender: param reads done, err=%d", (int)err);
	if (err) return err;

	// Evaluate engine at current frame time.
	double t_sec = static_cast<double>(in_data->current_time) /
	               static_cast<double>(in_data->time_scale);
	jitter::Output result = jitter::Evaluate(cfg, t_sec);

	// Defensive NaN/inf guard — should never trip with our engine but
	// cheap insurance against any future regression that produces non-finite.
	if (!std::isfinite(result.slide_x))     result.slide_x = 0.0;
	if (!std::isfinite(result.slide_y))     result.slide_y = 0.0;
	if (!std::isfinite(result.scale) || result.scale <= 0.0) result.scale = 1.0;
	if (!std::isfinite(result.time_offset)) result.time_offset = 0.0;
	if (!std::isfinite(result.rgb_split_x)) result.rgb_split_x = 0.0;
	if (!std::isfinite(result.rgb_split_y)) result.rgb_split_y = 0.0;

	// Slide RGB Split sub-param scales the engine's rgb_split channels.
	result.rgb_split_x *= slide_rgb_split_amt / 100.0;
	result.rgb_split_y *= slide_rgb_split_amt / 100.0;

	// Slide Direction + Spread: when Direction != 0, project the slide
	// vector onto the direction axis and shrink the perpendicular component
	// by Spread%.
	if (slide_direction > 0.0 && slide_direction < 360.0) {
		const double pi = 3.14159265358979323846;
		double dir_rad = slide_direction * (pi / 180.0);
		double dx = std::cos(dir_rad);
		double dy = std::sin(dir_rad);
		double along = result.slide_x * dx + result.slide_y * dy;
		double perp_x = result.slide_x - along * dx;
		double perp_y = result.slide_y - along * dy;
		double spread = slide_spread / 100.0;
		result.slide_x = along * dx + perp_x * spread;
		result.slide_y = along * dy + perp_y * spread;
	}

	// Time Direction popup clamp.
	if (time_direction == TIME_DIR_FORWARD_ONLY  && result.time_offset < 0.0) result.time_offset = -result.time_offset;
	if (time_direction == TIME_DIR_BACKWARD_ONLY && result.time_offset > 0.0) result.time_offset = -result.time_offset;

	// Slide Tendency: 0 = linear (no change), higher = bias slide magnitude
	// toward extremes (gives snappy pops vs gentle drift). Applied as a power
	// curve on the normalized magnitude — exponent shrinks from 1.0 → 0.5
	// across the [0..100] tendency range.
	if (slide_tendency > 0.0) {
		const double SLIDE_MAX = 500.0;	// matches MAX_SLIDE_PIXELS in JitterEngine.cpp
		double exp_pow = 1.0 - (slide_tendency / 100.0) * 0.5;	// 1.0 → 0.5
		auto bias = [&](double v) -> double {
			double mag = std::fabs(v) / SLIDE_MAX;
			if (mag <= 0.0) return 0.0;
			double shaped = std::pow(mag, exp_pow);
			return (v >= 0.0 ? 1.0 : -1.0) * shaped * SLIDE_MAX;
		};
		result.slide_x = bias(result.slide_x);
		result.slide_y = bias(result.slide_y);
	}

	// Stash for SmartRender.
	JitterPreRenderData *pdata = new (std::nothrow) JitterPreRenderData;
	if (!pdata) return PF_Err_OUT_OF_MEMORY;
	pdata->values              = result;
	pdata->slide_direction_deg = slide_direction;
	pdata->slide_spread_pct    = slide_spread;
	pdata->slide_tendency_pct  = slide_tendency;
	pdata->scale_origin_x_pct  = scale_origin_x_fixed;
	pdata->scale_origin_y_pct  = scale_origin_y_fixed;
	pdata->time_direction      = time_direction;
	pdata->light_behaviour     = light_behaviour;
	pdata->color_colorize      = color_colorize;
	pdata->show_twitch_only    = show_twitch_only;
	extra->output->pre_render_data            = pdata;
	extra->output->delete_pre_render_data_func = DeleteJitterPreRenderData;

	// Time-offset checkout.
	A_long sample_time = in_data->current_time;
	if (std::fabs(result.time_offset) > 0.0) {
		double offset_ticks_f = result.time_offset * in_data->time_scale;
		A_long offset_ticks   = static_cast<A_long>(std::lround(offset_ticks_f));
		if (std::abs(offset_ticks) >= in_data->time_step / 2) {
			A_long target = in_data->current_time + offset_ticks;
			A_long max_t  = in_data->total_time - in_data->time_step;
			if (max_t < 0) max_t = 0;
			if (target < 0) target = 0;
			if (target > max_t) target = max_t;
			sample_time = target;
		}
	}

	JLog("PreRender: about to checkout_layer at sample_time=%ld", (long)sample_time);
	PF_RenderRequest req = extra->input->output_request;
	PF_CheckoutResult in_result;
	ERR(extra->cb->checkout_layer(in_data->effect_ref, JITTER_INPUT, JITTER_INPUT,
		&req, sample_time, in_data->time_step, in_data->time_scale, &in_result));
	JLog("PreRender: checkout_layer returned err=%d", (int)err);
	if (!err) {
		PF_LRect inflated = in_result.max_result_rect;
		A_long w = inflated.right - inflated.left;
		A_long h = inflated.bottom - inflated.top;
		A_long margin = 200 + (w > h ? w : h) / 2;
		inflated.left -= margin;  inflated.top -= margin;
		inflated.right += margin; inflated.bottom += margin;
		UnionLRect(&in_result.result_rect, &extra->output->result_rect);
		UnionLRect(&inflated,              &extra->output->max_result_rect);
		JLog("PreRender: rects unioned, in_rect=(%d,%d,%d,%d) inflated=(%d,%d,%d,%d)",
			(int)in_result.result_rect.left, (int)in_result.result_rect.top,
			(int)in_result.result_rect.right, (int)in_result.result_rect.bottom,
			(int)inflated.left, (int)inflated.top, (int)inflated.right, (int)inflated.bottom);
	}
	JLog("---- SmartPreRender end err=%d ----", (int)err);
	return err;
}

// ===================================================================
// SmartRender — STAGE 2 with logging.
// ===================================================================
static PF_Err
SmartRender(PF_InData *in_data, PF_OutData *out_data, PF_SmartRenderExtra *extra)
{
	JLog("---- SmartRender start ----");
	PF_Err err = PF_Err_NONE;
	AEGP_SuiteHandler suites(in_data->pica_basicP);
	PF_EffectWorld *input_worldP = NULL, *output_worldP = NULL;

	ERR(extra->cb->checkout_layer_pixels(in_data->effect_ref, JITTER_INPUT, &input_worldP));
	JLog("Render: checkout_layer_pixels err=%d input=%p", (int)err, (void*)input_worldP);
	ERR(extra->cb->checkout_output(in_data->effect_ref, &output_worldP));
	JLog("Render: checkout_output err=%d output=%p", (int)err, (void*)output_worldP);
	if (err || !output_worldP) { JLog("Render: early exit (err or no output)"); return err; }

	// Defensive: AE doesn't always populate output_worldP->extent_hint
	// before SmartRender. We pass it as the dest_rect to transform_world
	// and as src to iterate; if it's (0,0,0,0), nothing gets written and
	// the layer goes transparent. Force it to full bounds here.
	JLog("Render: output extent_hint pre-fix=(%d,%d,%d,%d)",
		(int)output_worldP->extent_hint.left, (int)output_worldP->extent_hint.top,
		(int)output_worldP->extent_hint.right, (int)output_worldP->extent_hint.bottom);
	output_worldP->extent_hint.left   = 0;
	output_worldP->extent_hint.top    = 0;
	output_worldP->extent_hint.right  = output_worldP->width;
	output_worldP->extent_hint.bottom = output_worldP->height;
	if (!input_worldP || !input_worldP->data) {
		// Adjustment-layer or no-input case. Fill output with transparent
		// black explicitly — without this, AE's uninitialized output buffer
		// could contain garbage or be fully transparent (looking like the
		// effect "broke" the layer).
		JLog("Render: no input pixels — filling output transparent");
		if (PF_WORLD_IS_DEEP(output_worldP)) {
			PF_Pixel16 zero16 = { 0, 0, 0, 0 };
			ERR(suites.FillMatteSuite2()->fill16(in_data->effect_ref, &zero16, NULL, output_worldP));
		} else {
			PF_Pixel zero8 = { 0, 0, 0, 0 };
			ERR(suites.FillMatteSuite2()->fill(in_data->effect_ref, &zero8, NULL, output_worldP));
		}
		return err;
	}
	JLog("Render: input %dx%d rb=%ld data=%p / output %dx%d rb=%ld data=%p",
		input_worldP->width, input_worldP->height, (long)input_worldP->rowbytes, (void*)input_worldP->data,
		output_worldP->width, output_worldP->height, (long)output_worldP->rowbytes, (void*)output_worldP->data);

	JitterPreRenderData *pdata =
		static_cast<JitterPreRenderData*>(extra->input->pre_render_data);
	JLog("Render: pdata=%p", (void*)pdata);
	if (!pdata) {
		JLog("Render: pdata is NULL — falling back to copy_hq");
		ERR(suites.WorldTransformSuite1()->copy_hq(
			in_data->effect_ref, input_worldP, output_worldP, NULL, NULL));
		return err;
	}
	const jitter::Output &v = pdata->values;
	JLog("Render: values slide=(%.2f,%.2f) scale=%.4f rgb=(%.2f,%.2f)",
		v.slide_x, v.slide_y, v.scale, v.rgb_split_x, v.rgb_split_y);

	// Show Twitch Only — debug viewer. Fill output with a neutral mid-gray
	// so the user can see the effect is "active" but no layer content shows.
	// (Twitch's actual behavior is to render only the modulation overlay;
	// our v1 just gives a clear visual signal that the toggle works.)
	if (pdata->show_twitch_only) {
		PF_Pixel  gray8  = { 255, 128, 128, 128 };
		PF_Pixel16 gray16 = { 32768, 16384, 16384, 16384 };
		if (PF_WORLD_IS_DEEP(output_worldP)) {
			ERR(suites.FillMatteSuite2()->fill16(in_data->effect_ref, &gray16, NULL, output_worldP));
		} else {
			ERR(suites.FillMatteSuite2()->fill(in_data->effect_ref, &gray8, NULL, output_worldP));
		}
		return err;
	}

	// Use Scale Origin point if set, otherwise layer center. PF_Param_POINT
	// defaults to (50, 50) preconverted, which lands as ~0.000762 after
	// /65536 — anything below 1.0 is treated as "default, use center."
	double cx = pdata->scale_origin_x_pct;
	double cy = pdata->scale_origin_y_pct;
	if (cx < 1.0 && cy < 1.0) {
		cx = input_worldP->width  / 2.0;
		cy = input_worldP->height / 2.0;
	}
	double s = v.scale;
	double a = s, b = 0.0, c = 0.0, d = s;
	double tx = v.slide_x + cx * (1.0 - a);
	double ty = v.slide_y + cy * (1.0 - d);
	JLog("Render: matrix a=%.4f d=%.4f tx=%.2f ty=%.2f", a, d, tx, ty);

	PF_FloatMatrix m;
	AEFX_CLR_STRUCT(m);
	m.mat[0][0] = a;   m.mat[0][1] = b;   m.mat[0][2] = 0.0;
	m.mat[1][0] = c;   m.mat[1][1] = d;   m.mat[1][2] = 0.0;
	m.mat[2][0] = tx;  m.mat[2][1] = ty;  m.mat[2][2] = 1.0;

	PF_CompositeMode comp_mode;
	AEFX_CLR_STRUCT(comp_mode);
	comp_mode.xfer      = PF_Xfer_COPY;
	comp_mode.opacity   = 255;
	comp_mode.opacitySu = PF_MAX_CHAN16;	// deep-color opacity; 0 here would mean
	                                    	// fully transparent in 16/32bpc

	bool deep        = PF_WORLD_IS_DEEP(output_worldP);
	bool needs_split = (std::fabs(v.rgb_split_x) >= 0.5) ||
	                   (std::fabs(v.rgb_split_y) >= 0.5);
	bool needs_color = std::fabs(v.color) > 0.001;
	bool needs_light = std::fabs(v.light) > 0.001;
	bool needs_blur  = std::fabs(v.blur)  > 0.001;
	bool needs_temp  = needs_split || needs_color || needs_light || needs_blur;
	JLog("Render: ops color=%.4f light=%.4f blur=%.4f rgbX=%.2f → split=%d color=%d light=%d blur=%d temp=%d",
		v.color, v.light, v.blur, v.rgb_split_x,
		(int)needs_split, (int)needs_color, (int)needs_light, (int)needs_blur, (int)needs_temp);

	// ---- Fast path: just transform → output ----
	if (!needs_temp) {
		JLog("Render: FAST PATH (no operators)");
		if (deep) {
			PF_Pixel16 zero16 = { 0, 0, 0, 0 };
			ERR(suites.FillMatteSuite2()->fill16(in_data->effect_ref, &zero16, NULL, output_worldP));
		} else {
			PF_Pixel zero8 = { 0, 0, 0, 0 };
			ERR(suites.FillMatteSuite2()->fill(in_data->effect_ref, &zero8, NULL, output_worldP));
		}
		ERR((*in_data->utils->transform_world)(
			in_data->effect_ref, in_data->quality, PF_MF_Alpha_STRAIGHT, in_data->field,
			input_worldP, &comp_mode, NULL, &m, 1, TRUE,
			&output_worldP->extent_hint, output_worldP));
		JLog("Render: FAST PATH transform_world err=%d output extent=(%d,%d,%d,%d)",
			(int)err,
			(int)output_worldP->extent_hint.left, (int)output_worldP->extent_hint.top,
			(int)output_worldP->extent_hint.right, (int)output_worldP->extent_hint.bottom);
		return err;
	}

	// ---- Slow path: transform → temp_world → ops → output ----
	JLog("Render: SLOW PATH start");
	PF_EffectWorld temp_world;
	AEFX_CLR_STRUCT(temp_world);
	PF_NewWorldFlags new_flags = PF_NewWorldFlag_CLEAR_PIXELS |
	                             (deep ? PF_NewWorldFlag_DEEP_PIXELS : 0);
	ERR((*in_data->utils->new_world)(in_data->effect_ref,
		output_worldP->width, output_worldP->height, new_flags, &temp_world));
	if (err) return err;

	// new_world doesn't always initialize extent_hint. If it's zero,
	// transform_world's dest_rect is empty and writes nothing → temp stays
	// transparent → all subsequent operators see transparent input → output
	// ends up transparent. Set it explicitly to the full world.
	temp_world.extent_hint.left   = 0;
	temp_world.extent_hint.top    = 0;
	temp_world.extent_hint.right  = temp_world.width;
	temp_world.extent_hint.bottom = temp_world.height;
	JLog("Render: temp_world %dx%d extent=(%d,%d,%d,%d) data=%p",
		temp_world.width, temp_world.height,
		(int)temp_world.extent_hint.left, (int)temp_world.extent_hint.top,
		(int)temp_world.extent_hint.right, (int)temp_world.extent_hint.bottom,
		(void*)temp_world.data);

	ERR((*in_data->utils->transform_world)(
		in_data->effect_ref, in_data->quality, PF_MF_Alpha_STRAIGHT, in_data->field,
		input_worldP, &comp_mode, NULL, &m, 1, TRUE,
		&temp_world.extent_hint, &temp_world));
	JLog("Render: transform_world(input → temp_world) err=%d", (int)err);

	// ---- Color: in-place HSL hue rotation on temp_world ----
	if (!err && needs_color) {
		JLog("Render: Color op v.color=%.4f", v.color);
		ColorInfo cinfo;
		cinfo.hue_shift = v.color * 0.5;	// engine v.color in [-1,1] → ±180° hue rotation
		cinfo.colorize  = pdata->color_colorize;
		if (deep) {
			ERR(suites.Iterate16Suite2()->iterate(
				in_data, 0, temp_world.height, &temp_world, NULL,
				&cinfo, ColorPixel16, &temp_world));
		} else {
			ERR(suites.Iterate8Suite2()->iterate(
				in_data, 0, temp_world.height, &temp_world, NULL,
				&cinfo, ColorPixel8, &temp_world));
		}
		JLog("Render: Color op done err=%d", (int)err);
	}

	// ---- Light: in-place brightness shift on temp_world ----
	if (!err && needs_light) {
		JLog("Render: Light op v.light=%.4f", v.light);
		LightInfo linfo;
		linfo.amount    = v.light * 0.5;	// engine v.light in [-1,1] → ±0.5 luma shift
		linfo.behaviour = pdata->light_behaviour;
		if (deep) {
			ERR(suites.Iterate16Suite2()->iterate(
				in_data, 0, temp_world.height, &temp_world, NULL,
				&linfo, LightPixel16, &temp_world));
		} else {
			ERR(suites.Iterate8Suite2()->iterate(
				in_data, 0, temp_world.height, &temp_world, NULL,
				&linfo, LightPixel8, &temp_world));
		}
		JLog("Render: Light op done err=%d", (int)err);
	}

	// ---- Final pass to output: prefer Blur > RGB Split > copy ----
	// Iterate src/dst use temp_world as src — its extent_hint is known good,
	// while output_worldP's may be unreliable. The pixel function reads from
	// binfo.src/info.src (=temp_world) anyway and writes to dst.
	if (!err && needs_blur) {
		JLog("Render: Blur op v.blur=%.4f", v.blur);
		// Direction along Slide direction angle (or default 0° if unset).
		double dir_rad = pdata->slide_direction_deg * (3.14159265358979323846 / 180.0);
		double dx = std::cos(dir_rad);
		double dy = std::sin(dir_rad);
		// Tap radius scaled by |v.blur| (-1..1) — peak ~20 taps each side.
		int radius = static_cast<int>(std::round(std::fabs(v.blur) * 20.0));
		if (radius < 1) radius = 1;
		BlurInfo binfo;
		binfo.src    = &temp_world;
		binfo.dx     = dx;
		binfo.dy     = dy;
		binfo.radius = radius;
		if (deep) {
			ERR(suites.Iterate16Suite2()->iterate(
				in_data, 0, temp_world.height, &temp_world, NULL,
				&binfo, BlurPixel16, output_worldP));
		} else {
			ERR(suites.Iterate8Suite2()->iterate(
				in_data, 0, temp_world.height, &temp_world, NULL,
				&binfo, BlurPixel8, output_worldP));
		}
		JLog("Render: Blur op done err=%d", (int)err);
	} else if (!err && needs_split) {
		JLog("Render: RGBSplit op rgb=(%.2f,%.2f)", v.rgb_split_x, v.rgb_split_y);
		RGBSplitInfo info;
		info.src      = &temp_world;
		info.offset_x = v.rgb_split_x;
		info.offset_y = v.rgb_split_y;
		if (deep) {
			ERR(suites.Iterate16Suite2()->iterate(
				in_data, 0, temp_world.height, &temp_world, NULL,
				&info, RGBSplitPixel16, output_worldP));
		} else {
			ERR(suites.Iterate8Suite2()->iterate(
				in_data, 0, temp_world.height, &temp_world, NULL,
				&info, RGBSplitPixel8, output_worldP));
		}
		JLog("Render: RGBSplit op done err=%d", (int)err);
	} else if (!err) {
		// Color/Light only — copy temp_world to output.
		JLog("Render: Color/Light only — copy_hq temp→output");
		ERR(suites.WorldTransformSuite1()->copy_hq(
			in_data->effect_ref, &temp_world, output_worldP, NULL, NULL));
		JLog("Render: copy_hq done err=%d", (int)err);
	}

	PF_Err err2 = PF_Err_NONE;
	ERR2((*in_data->utils->dispose_world)(in_data->effect_ref, &temp_world));
	return err;
}


// ===================================================================
// Render (legacy fallback) — passthrough copy.
// ===================================================================
static PF_Err
Render(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
	PF_Err err = PF_Err_NONE;
	AEGP_SuiteHandler suites(in_data->pica_basicP);

	if (PF_Quality_HI == in_data->quality) {
		ERR(suites.WorldTransformSuite1()->copy_hq(
			in_data->effect_ref,
			&params[JITTER_INPUT]->u.ld,
			output,
			&params[JITTER_INPUT]->u.ld.extent_hint,
			&output->extent_hint));
	} else {
		ERR(suites.WorldTransformSuite1()->copy(
			in_data->effect_ref,
			&params[JITTER_INPUT]->u.ld,
			output,
			&params[JITTER_INPUT]->u.ld.extent_hint,
			&output->extent_hint));
	}
	return err;
}

// ===================================================================
// Plugin entry point — registers Jitter (ADBE Jitter) + Twitch (Videocopilot Twitch)
// ===================================================================
extern "C" DllExport
PF_Err PluginDataEntryFunction2(
	PF_PluginDataPtr inPtr,
	PF_PluginDataCB2 inPluginDataCallBackPtr,
	SPBasicSuite* inSPBasicSuitePtr,
	const char* inHostName,
	const char* inHostVersion)
{
	// PF_REGISTER_EFFECT_EXT2 writes into a caller-scoped variable named
	// exactly `result`. See SDK/Util/entry.h for the expansion.
	PF_Err result = PF_Err_INVALID_CALLBACK;

	PF_REGISTER_EFFECT_EXT2(
		inPtr,
		inPluginDataCallBackPtr,
		"Jitter",
		"ADBE Jitter",
		"Video Copilot",
		AE_RESERVED_INFO,
		"EffectMain",
		"https://www.adobe.com");
	PF_Err result_jitter = result;

	// Legacy match name "Videocopilot Twitch" — required so existing
	// projects and animation presets resolve to this plugin.
	PF_REGISTER_EFFECT_EXT2(
		inPtr,
		inPluginDataCallBackPtr,
		"Twitch",
		"Videocopilot Twitch",
		"Video Copilot",
		AE_RESERVED_INFO,
		"EffectMain",
		"https://www.adobe.com");
	PF_Err result_twitch = result;

	return (result_jitter == PF_Err_NONE || result_twitch == PF_Err_NONE)
	       ? PF_Err_NONE : result_jitter;
}

// ===================================================================
// Effect dispatch
// ===================================================================
PF_Err
EffectMain(PF_Cmd cmd, PF_InData *in_data, PF_OutData *out_data,
	PF_ParamDef *params[], PF_LayerDef *output, void *extra)
{
	PF_Err err = PF_Err_NONE;
	try {
		switch (cmd) {
			case PF_Cmd_ABOUT:           err = About(in_data, out_data, params, output); break;
			case PF_Cmd_GLOBAL_SETUP:    err = GlobalSetup(in_data, out_data, params, output); break;
			case PF_Cmd_PARAMS_SETUP:    err = ParamsSetup(in_data, out_data, params, output); break;
			case PF_Cmd_RENDER:          err = Render(in_data, out_data, params, output); break;
			case PF_Cmd_SMART_PRE_RENDER:err = SmartPreRender(in_data, out_data, reinterpret_cast<PF_PreRenderExtra*>(extra)); break;
			case PF_Cmd_SMART_RENDER:    err = SmartRender(in_data, out_data, reinterpret_cast<PF_SmartRenderExtra*>(extra)); break;
		}
	} catch (PF_Err &thrown_err) {
		err = thrown_err;
	}
	return err;
}
