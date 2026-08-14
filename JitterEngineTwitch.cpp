/*******************************************************************
 * JitterEngineTwitch.cpp — Twitch-compatible value engine.
 *
 * Recovered by disassembly of the original plugin; see notes/ for the
 * evidence behind each formula. Every number here traces to a specific
 * decompiled statement rather than to tuning by eye.
 *******************************************************************/

#include "JitterEngineTwitch.h"

#include <cmath>
#include <algorithm>

namespace twitch {

static const double kPi = 3.14159265358979323846;

/* The original converts degrees with a NEGATED factor, which flips the
 * direction of travel relative to the obvious convention. Reproduced as
 * found rather than corrected. */
static const double kDegToRadNeg = -0.017453292519943295;

double DrawRaw(int32_t master_seed, int32_t op_seed, int32_t k, int64_t event_idx)
{
	Rand rng(master_seed + op_seed + k);

	// Indices below -4 are clamped, so early negative times repeat a value.
	int64_t idx = event_idx < -4 ? -4 : event_idx;
	int64_t n = idx < 0 ? -idx : idx;

	rng.discard(static_cast<int>(n) + 5);
	return rng.next01();
}

double ApplyRandomizeMin(double u, double randomize_min_pct)
{
	const double m = randomize_min_pct / 100.0;
	return m + u * (1.0 - m);
}

double ApplyRandomizeMinCentered(double u, double randomize_min_pct)
{
	const double m = randomize_min_pct / 100.0;
	const double s = 2.0 * std::fabs(u - 0.5);
	const double sign = (u - 0.5) >= 0.0 ? 1.0 : -1.0;
	double v = sign * (m + s * (1.0 - m)) / 2.0;
	v += 0.5;
	return std::min(1.0, std::max(0.0, v));
}

int EventPeriodFrames(double fps, double twitches_per_sec, double master_speed)
{
	const double rate = twitches_per_sec * master_speed;
	if (!(rate > 0.0) || !(fps > 0.0)) return 1;

	// Truncation toward zero, then a floor of one frame. The realised rate
	// is fps/P, so a requested rate finer than one event per frame saturates.
	int p = static_cast<int>(fps / rate);
	return p < 1 ? 1 : p;
}

double EventTimeFrames(int32_t master_seed, int32_t op_seed, int64_t k, int period_frames)
{
	const double u = DrawRaw(master_seed, op_seed, K_EVENT_GRID, k);
	return (static_cast<double>(k) + u) * static_cast<double>(period_frames);
}

double Envelope(double t_frames, double prev_event, double next_event,
                double ease_in_frames, double ease_out_frames)
{
	// Both eases are frame counts and the engine adds one before dividing,
	// so an ease of 0 still gives a single-frame ramp rather than a step.
	const double out_span = ease_out_frames + 1.0;
	const double in_span  = ease_in_frames + 1.0;

	const double a = (t_frames - prev_event) / out_span;	// rise since the last event
	const double b = (next_event - t_frames) / in_span;		// fall toward the next

	const double rise = 1.0 - a;
	const double fall = 1.0 - b;
	return std::max(std::max(0.0, rise), fall);
}

/* One slide vector: an amplitude drawn off amp_k, an angle off angle_k. */
static void SlideVector(const SlideParams &p, int32_t amp_k, int32_t angle_k,
                        int64_t idx, double env, double width, double height,
                        bool centered_min, double *out_x, double *out_y)
{
	const double u_amp = DrawRaw(p.master_seed, p.op_seed, amp_k, idx);
	const double u_ang = DrawRaw(p.master_seed, p.op_seed, angle_k, idx);

	const double r = centered_min ? ApplyRandomizeMinCentered(u_amp, p.randomize_min_pct)
	                              : ApplyRandomizeMin(u_amp, p.randomize_min_pct);

	// Tendency is a signed DC bias on the uniform, applied before the
	// direction vector is formed. There is no power curve and nothing is
	// normalized against a fixed pixel maximum.
	const double scale = (p.master_amount * 0.01) * p.amount;
	const double amp   = 2.0 * scale * (r - (1.0 + p.tendency) / 2.0);

	const double theta = p.direction_deg * kDegToRadNeg
	                   + (u_ang - 0.5) * 2.0 * kPi * p.spread;

	// Magnitude is a fraction of the frame, so a 4K comp slides twice as
	// far in pixels as a 1080p one. X rides width, Y rides height.
	*out_x = -std::sin(theta) * 0.01 * width  * env * amp;
	*out_y = -std::cos(theta) * 0.01 * height * env * amp;
}

SlideResult EvaluateSlide(const SlideParams &p, double frame, double fps,
                          double width, double height)
{
	SlideResult res = {0, 0, 0, 0, 0, 0, 0, 0};

	const int period = EventPeriodFrames(fps, p.twitches_per_sec, p.master_speed);

	// Sampled at the frame centre, not the frame start.
	const double t = frame + 0.5;

	const int64_t k = static_cast<int64_t>(std::floor(t / static_cast<double>(period)));

	const double prev_evt = EventTimeFrames(p.master_seed, p.op_seed, k, period);
	const double next_evt = EventTimeFrames(p.master_seed, p.op_seed, k + 1, period);
	const double env = Envelope(t, prev_evt, next_evt, p.ease_in_frames, p.ease_out_frames);

	SlideVector(p, K_SLIDE_AMP, K_SLIDE_ANGLE, k, env, width, height, true, &res.x, &res.y);

	// RGB Split blends between the coherent slide and three independent
	// vectors. At full split the coherent component vanishes entirely,
	// which an additive offset model can never reproduce.
	const double s = p.rgb_split / 100.0;
	if (s > 0.0) {
		double rx, ry, gx, gy, bx, by;
		SlideVector(p, K_SPLIT_AMP_1, K_SPLIT_ANGLE_1, k, env, width, height, false, &rx, &ry);
		SlideVector(p, K_SPLIT_AMP_2, K_SPLIT_ANGLE_2, k, env, width, height, false, &gx, &gy);
		SlideVector(p, K_SPLIT_AMP_3, K_SPLIT_ANGLE_3, k, env, width, height, false, &bx, &by);
		res.r_x = (1.0 - s) * res.x + s * rx;
		res.r_y = (1.0 - s) * res.y + s * ry;
		res.g_x = (1.0 - s) * res.x + s * gx;
		res.g_y = (1.0 - s) * res.y + s * gy;
		res.b_x = (1.0 - s) * res.x + s * bx;
		res.b_y = (1.0 - s) * res.y + s * by;
	} else {
		res.r_x = res.g_x = res.b_x = res.x;
		res.r_y = res.g_y = res.b_y = res.y;
	}

	return res;
}

}	// namespace twitch
