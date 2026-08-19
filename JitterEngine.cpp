/*******************************************************************/
/*                                                                 */
/*                      Jitter Engine - Implementation             */
/*                                                                 */
/*******************************************************************/

#include "JitterEngine.h"
#include <cmath>

namespace jitter {

// ===================================================================
// Calibration constants.
//
// Each MAX_* is the magnitude of the operator's output when:
//     master.amount = 100, op.amount = 100, full event swing (±1)
// ===================================================================
static const double MAX_SLIDE_PIXELS    = 500.0;	// Slide  — peak ± in pixels
static const double MAX_SCALE_DEV       = 0.5;		// Scale  — ±50% (1.0 ± 0.5) at full
static const double MAX_TIME_OFFSET_SEC = 1.0;		// Time   — peak ± offset in seconds

// ---- Operator IDs ----
// Used as a salt so operator A's seed=5 doesn't collide with operator B's seed=5.
enum OperatorId : uint32_t {
	OP_SLIDE = 1,
	OP_SCALE = 2,
	OP_TIME  = 3,
	OP_COLOR = 4,
	OP_LIGHT = 5,
	OP_BLUR  = 6
};

// ---- Hashing ----
// splitmix64: small, fast, well-distributed avalanche function.
static inline uint64_t splitmix64(uint64_t x) {
	x += 0x9E3779B97F4A7C15ULL;
	x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
	x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
	return x ^ (x >> 31);
}

// Deterministically combine five 32-bit inputs into one 64-bit hash.
static inline uint64_t hash5(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
	uint64_t h = splitmix64(static_cast<uint64_t>(a) | (static_cast<uint64_t>(b) << 32));
	h = splitmix64(h ^ (static_cast<uint64_t>(c) | (static_cast<uint64_t>(d) << 32)));
	h = splitmix64(h ^ static_cast<uint64_t>(e));
	return h;
}

// Convert a 64-bit hash into a uniform value in [-1, 1].
static inline double valueFromHash(uint64_t h) {
	double u = static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0); // 2^53
	return u * 2.0 - 1.0;
}

// Apply Randomize Minimum: shift the absolute magnitude into [min/100, 1] so
// the operator never twitches by less than `randomize_min` percent of full.
// Sign is preserved.
static inline double applyRandomizeMin(double v, double randomize_min_pct) {
	double m = randomize_min_pct / 100.0;
	if (m <= 0.0) return v;
	if (m >= 1.0) return v >= 0.0 ? 1.0 : -1.0;
	double mag = std::fabs(v);
	double shaped = m + mag * (1.0 - m);
	return v >= 0.0 ? shaped : -shaped;
}

// ---- Easing ----
// Twitch's "Ease In" / "Ease Out" each go 0..100. Both at 0 = linear; both
// near 100 = sharp snap with smooth landing. Independent control of curve
// shape on each side. Implemented as a Bezier-like blend of identity and
// smooth step, separately near t=0 vs t=1.
static inline double ease(double t, double ease_in_pct, double ease_out_pct) {
	if (t <= 0.0) return 0.0;
	if (t >= 1.0) return 1.0;

	double ein  = ease_in_pct  / 100.0;	// 0..1, 0 = linear in, 1 = held flat then snap
	double eout = ease_out_pct / 100.0;	// 0..1, 0 = linear out, 1 = snap then settle

	// Smoothstep gives gentle ease in/out, S-curve with continuous derivatives.
	double smooth = t * t * (3.0 - 2.0 * t);

	// Build a piecewise blend: more weight to smoothstep early when ease_in
	// is high; more weight to smoothstep late when ease_out is high; otherwise
	// linear.
	double w_smooth = 0.0;
	if (t < 0.5) {
		w_smooth = ein;
	} else {
		w_smooth = eout;
	}
	return smooth * w_smooth + t * (1.0 - w_smooth);
}

// ---- Per-channel evaluation ----
// Compute one channel of an operator at time t. Returns a value in [-1, 1].
static double evaluateChannel(
	const MasterParams& master,
	uint32_t op_seed,
	uint32_t op_id,
	uint32_t channel_id,
	double effective_twitches_per_sec,
	double t)
{
	if (effective_twitches_per_sec <= 0.0) return 0.0;
	if (!(std::isfinite(t) && std::isfinite(effective_twitches_per_sec))) return 0.0;
	if (t < 0.0) t = 0.0;

	double idx_f = t * effective_twitches_per_sec;

	// An AE expression can drive a param to NaN or infinity, and the render
	// path's isfinite guards only sanitize the engine's OUTPUT — by then this
	// cast has already run, and converting a non-finite double to an integer
	// type is undefined. Bail out before the cast instead.
	if (!std::isfinite(idx_f)) return 0.0;

	double idx_floor = std::floor(idx_f);
	if (std::fabs(idx_floor) > 9.0e15) return 0.0;		// past int64's exact range

	uint32_t idx = static_cast<uint32_t>(static_cast<int64_t>(idx_floor) & 0xFFFFFFFF);
	double local_t = idx_f - idx_floor;

	uint64_t h_curr = hash5(master.seed, op_seed, op_id, channel_id, idx);
	uint64_t h_next = hash5(master.seed, op_seed, op_id, channel_id, idx + 1u);

	double v_curr = applyRandomizeMin(valueFromHash(h_curr), master.randomize_min);
	double v_next = applyRandomizeMin(valueFromHash(h_next), master.randomize_min);

	double e = ease(local_t, master.ease_in, master.ease_out);
	return v_curr + (v_next - v_curr) * e;
}

// ---- Public API ----
Output Evaluate(const Config& cfg, double t) {
	Output out;
	out.slide_x = 0.0;
	out.slide_y = 0.0;
	out.split_r_x = 0.0;  out.split_r_y = 0.0;
	out.split_g_x = 0.0;  out.split_g_y = 0.0;
	out.split_b_x = 0.0;  out.split_b_y = 0.0;
	out.scale = 1.0;
	out.time_offset = 0.0;
	out.color = 0.0;
	out.light = 0.0;
	out.blur = 0.0;

	double master_amount = cfg.master.amount / 100.0;

	auto evalOp = [&](const OperatorParams& op, uint32_t op_id, uint32_t ch) -> double {
		if (!op.enabled) return 0.0;
		if (op.amount <= 0.0) return 0.0;
		if (op.twitches_per_sec <= 0.0) return 0.0;
		double v = evaluateChannel(cfg.master, op.seed, op_id, ch, op.twitches_per_sec, t);
		return v * (op.amount / 100.0) * master_amount;
	};

	// Slide: 4 independent channels — XY translation + RGB-split offset.
	// All driven by the same Slide event grid (same twitches/sec, same op seed),
	// but uncorrelated random values per channel via channel_id.
	out.slide_x     = evalOp(cfg.slide, OP_SLIDE, 0) * MAX_SLIDE_PIXELS;
	out.slide_y     = evalOp(cfg.slide, OP_SLIDE, 1) * MAX_SLIDE_PIXELS;
	// RGB Split blends the coherent slide against three independent vectors
	// drawn from the same event grid at the same amplitude. At 0 every channel
	// rides the slide; at 100 the shared motion cancels completely and each
	// channel goes its own way. Because the independent vectors use the slide's
	// own amplitude rather than a separate smaller ceiling, a 10% slide with
	// full split gives a 10%-sized separation with no common shake — which is
	// the whole point of the control, and what the original does.
	const double split = cfg.slide_rgb_split / 100.0;
	if (split > 0.0) {
		const double s = split > 1.0 ? 1.0 : split;
		const double ind_r_x = evalOp(cfg.slide, OP_SLIDE, 2) * MAX_SLIDE_PIXELS;
		const double ind_r_y = evalOp(cfg.slide, OP_SLIDE, 3) * MAX_SLIDE_PIXELS;
		const double ind_g_x = evalOp(cfg.slide, OP_SLIDE, 4) * MAX_SLIDE_PIXELS;
		const double ind_g_y = evalOp(cfg.slide, OP_SLIDE, 5) * MAX_SLIDE_PIXELS;
		const double ind_b_x = evalOp(cfg.slide, OP_SLIDE, 6) * MAX_SLIDE_PIXELS;
		const double ind_b_y = evalOp(cfg.slide, OP_SLIDE, 7) * MAX_SLIDE_PIXELS;

		// Stored as deltas from the base slide, so the render path can keep
		// applying the slide as a transform and only offset the sampling.
		out.split_r_x = s * (ind_r_x - out.slide_x);
		out.split_r_y = s * (ind_r_y - out.slide_y);
		out.split_g_x = s * (ind_g_x - out.slide_x);
		out.split_g_y = s * (ind_g_y - out.slide_y);
		out.split_b_x = s * (ind_b_x - out.slide_x);
		out.split_b_y = s * (ind_b_y - out.slide_y);
	}

	out.scale       = 1.0 + evalOp(cfg.scale,   OP_SCALE, 0) * MAX_SCALE_DEV;
	out.time_offset =        evalOp(cfg.time_op, OP_TIME,  0) * MAX_TIME_OFFSET_SEC;

	// v3 placeholders — kept wired so seed/amount changes are observable but
	// no rendering happens for these operators yet (Color = HSL, Light = glow,
	// Blur = FFT).
	out.color = evalOp(cfg.color, OP_COLOR, 0);
	out.light = evalOp(cfg.light, OP_LIGHT, 0);
	out.blur  = evalOp(cfg.blur,  OP_BLUR,  0);

	return out;
}

} // namespace jitter
