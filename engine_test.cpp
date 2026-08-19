/*******************************************************************/
/*                                                                 */
/*  Standalone CLI sanity-check for JitterEngine.                  */
/*                                                                 */
/*  Build (from SDK/Effect/Jitter):                                */
/*    clang++ -std=c++17 -O2 -Wall                                 */
/*       engine_test.cpp JitterEngine.cpp -o engine_test           */
/*                                                                 */
/*  Run:                                                           */
/*    ./engine_test                                                */
/*                                                                 */
/*  Not part of the AE plugin target. Verifies determinism,        */
/*  per-operator seed independence, output range envelopes, and    */
/*  that Slide's 4-channel (XY + RGB-split) outputs are uncorrelated. */
/*                                                                 */
/*******************************************************************/

#include "JitterEngine.h"
#include <cstdio>
#include <cmath>
#include <cstdlib>

using namespace jitter;

static OperatorParams makeOp(double amount, double tps, uint32_t seed, bool enabled) {
	OperatorParams op;
	op.amount = amount;
	op.twitches_per_sec = tps;
	op.seed = seed;
	op.enabled = enabled;
	return op;
}

static Config defaultConfig() {
	Config c;
	c.master.amount = 100.0;
	c.master.ease_in = 50.0;
	c.master.ease_out = 50.0;
	c.master.randomize_min = 0.0;
	c.master.seed = 42;
	c.slide_rgb_split = 100.0;		// full split, so the split path is exercised

	c.slide   = makeOp(100.0, 5.0, 0, true);
	c.scale   = makeOp( 50.0, 3.0, 0, true);
	c.time_op = makeOp(  0.0, 4.0, 0, false);
	c.color   = makeOp(  0.0, 4.0, 0, false);
	c.light   = makeOp(  0.0, 4.0, 0, false);
	c.blur    = makeOp(  0.0, 4.0, 0, false);
	return c;
}

static int s_failures = 0;
static void check(const char* label, bool cond) {
	std::printf("  %-58s %s\n", label, cond ? "OK" : "FAIL");
	if (!cond) ++s_failures;
}

int main() {
	Config c = defaultConfig();

	std::printf("Timeline (seed=42, ease_in=ease_out=50):\n");
	std::printf("  t      slide_x  slide_y  scale    rgb_x    rgb_y\n");
	for (int i = 0; i <= 20; i++) {
		double t = i * 0.1;
		Output o = Evaluate(c, t);
		std::printf("  %5.2f  %7.2f  %7.2f  %6.4f  %7.2f  %7.2f\n",
			t, o.slide_x, o.slide_y, o.scale, o.split_r_x, o.split_r_y);
	}

	std::printf("\nTests:\n");

	// ---- Determinism ----
	{
		Output a = Evaluate(c, 1.234);
		Output b = Evaluate(c, 1.234);
		check("determinism: same call gives same output",
			a.slide_x == b.slide_x &&
			a.slide_y == b.slide_y &&
			a.scale == b.scale &&
			a.split_r_x == b.split_r_x);
	}

	// ---- Seed independence between operators ----
	{
		Config c2 = c;
		c2.scale.seed = 999;
		Output o1 = Evaluate(c, 1.234);
		Output o2 = Evaluate(c2, 1.234);
		check("scale seed change does not affect slide",   o1.slide_x == o2.slide_x);
		check("scale seed change does affect scale",       o1.scale != o2.scale);
		check("scale seed change does not affect split_r", o1.split_r_x == o2.split_r_x);
	}

	// ---- Master seed reshuffles everything ----
	{
		Config c2 = c;
		c2.master.seed = 7;
		Output o1 = Evaluate(c, 1.234);
		Output o2 = Evaluate(c2, 1.234);
		check("master seed change affects slide",     o1.slide_x != o2.slide_x);
		check("master seed change affects scale",     o1.scale != o2.scale);
		check("master seed change affects split_r", o1.split_r_x != o2.split_r_x);
	}

	// ---- Disabled operator returns identity ----
	{
		Config c2 = c;
		c2.slide.enabled = false;
		Output o = Evaluate(c2, 1.234);
		check("disabled slide → slide_x == 0",      o.slide_x == 0.0);
		check("disabled slide → slide_y == 0",      o.slide_y == 0.0);
		check("disabled slide → split_r_x == 0",  o.split_r_x == 0.0);
	}

	// ---- Master amount=0 zeros everything (scale stays at 1.0) ----
	{
		Config c2 = c;
		c2.master.amount = 0.0;
		Output o = Evaluate(c2, 1.234);
		check("master.amount=0 → slide_x == 0",       o.slide_x == 0.0);
		check("master.amount=0 → scale == 1.0",       o.scale == 1.0);
		check("master.amount=0 → split_r_x == 0",   o.split_r_x == 0.0);
	}

	// ---- Slide channels are independent (XY vs RGB-split) ----
	{
		// Split 0 leaves every channel riding the coherent slide.
		Config z = defaultConfig();
		z.slide_rgb_split = 0.0;
		bool all_zero = true;
		for (int i = 0; i < 100; i++) {
			Output o = Evaluate(z, i * 0.07);
			if (o.split_r_x != 0.0 || o.split_g_x != 0.0 || o.split_b_x != 0.0 ||
			    o.split_r_y != 0.0 || o.split_g_y != 0.0 || o.split_b_y != 0.0) all_zero = false;
		}
		check("split 0 → every channel stays on the base slide", all_zero);

		// Full split sends the three channels to genuinely different places.
		double max_rg = 0.0, max_rb = 0.0;
		for (int i = 0; i < 200; i++) {
			Output o = Evaluate(c, i * 0.07);
			max_rg = std::fmax(max_rg, std::fabs(o.split_r_x - o.split_g_x));
			max_rb = std::fmax(max_rb, std::fabs(o.split_r_x - o.split_b_x));
		}
		check("split 100 → red, green and blue diverge from each other",
			max_rg > 10.0 && max_rb > 10.0);
	}

	// ---- A small shake can still carry a large split ----
	// This is the whole point of the control, and the thing the old additive
	// model made impossible: at 10% slide the split used to be scaled by the
	// slide amount AND the master amount AND its own slider, landing around
	// half a pixel. The separation should now be the same order as the shake.
	{
		Config q = defaultConfig();
		q.master.amount = 10.0;
		q.slide.amount = 10.0;
		q.slide_rgb_split = 100.0;

		double sum_slide = 0.0, sum_sep = 0.0;
		for (int i = 0; i < 300; i++) {
			Output o = Evaluate(q, i * 0.07);
			sum_slide += std::fabs(o.slide_x);
			sum_sep   += std::fabs(o.split_r_x - o.split_b_x);
		}
		check("at 10% slide with full split, separation is comparable to the shake",
			sum_sep > 0.5 * sum_slide);
	}

	// ---- Slide RGB-split x and y are independent ----
	{
		double max_abs_diff = 0.0;
		for (int i = 0; i < 200; i++) {
			Output o = Evaluate(c, i * 0.07);
			double diff = std::fabs(o.split_r_x - o.split_r_y);
			if (diff > max_abs_diff) max_abs_diff = diff;
		}
		check("split_r_x and split_r_y are independent (max|x-y| > 10px)",
			max_abs_diff > 10.0);
	}

	// ---- Negative time clamps to zero ----
	{
		Output o_neg  = Evaluate(c, -1.0);
		Output o_zero = Evaluate(c,  0.0);
		check("negative time clamps to t=0",
			o_neg.slide_x == o_zero.slide_x &&
			o_neg.slide_y == o_zero.slide_y);
	}

	// ---- Very large time still deterministic, no NaN/inf ----
	{
		Output a = Evaluate(c, 1e6);
		Output b = Evaluate(c, 1e6);
		check("t=1e6 deterministic", a.slide_x == b.slide_x);
		check("t=1e6 stays in envelope (no NaN/inf)",
			std::isfinite(a.slide_x) && std::isfinite(a.scale) &&
			std::fabs(a.slide_x) <= 500.0001);
	}

	// ---- Output ranges respect calibration envelopes ----
	{
		double max_slide = 0, max_rgb = 0, max_scale_dev = 0;
		for (int i = 0; i < 1000; i++) {
			double t = i * 0.05;
			Output o = Evaluate(c, t);
			if (std::fabs(o.slide_x)     > max_slide)     max_slide     = std::fabs(o.slide_x);
			// Deltas are relative to the slide, so the envelope that matters is
			// where each channel actually lands: base + delta.
			const double pos_r = o.slide_x + o.split_r_x;
			const double pos_g = o.slide_x + o.split_g_x;
			const double pos_b = o.slide_x + o.split_b_x;
			max_rgb = std::fmax(max_rgb, std::fmax(std::fabs(pos_r),
			          std::fmax(std::fabs(pos_g), std::fabs(pos_b))));
			double dev = std::fabs(o.scale - 1.0);
			if (dev > max_scale_dev) max_scale_dev = dev;
		}
		std::printf("  (observed max: slide=%.1fpx  split channel pos=%.1fpx  scale_dev=%.3f)\n",
			max_slide, max_rgb, max_scale_dev);
		check("slide stays within +/- 500px envelope", max_slide <= 500.0001);
		check("each split channel lands inside the +/- 500px slide envelope",
			max_rgb <= 500.0001);
		check("scale dev stays within 0.5", max_scale_dev <= 0.5001);
	}

	// ---- Behaviour: ease_out=0 (linear out) gives near-linear final segment ----
	{
		Config c2 = c;
		c2.master.ease_in = 0.0;
		c2.master.ease_out = 0.0;
		// Sample tightly across an event boundary.
		Output a = Evaluate(c2, 0.199);
		Output b = Evaluate(c2, 0.201);
		double jump = std::fabs(b.slide_x - a.slide_x);
		check("linear easing gives small boundary jump (<10px)", jump < 10.0);
	}

	// ---- Randomize Minimum increases average event magnitude ----
	// (Mid-event values still cross 0 when adjacent events have opposite signs;
	// what we check is that event peaks themselves get pushed toward full mag.)
	{
		Config c0 = c;  c0.master.randomize_min =   0.0;
		Config c1 = c;  c1.master.randomize_min = 100.0;
		// Sample at exact event boundaries (twitches/sec=5, so t = i * 0.2).
		double avg_mag_0 = 0.0, avg_mag_100 = 0.0;
		const int N = 100;
		for (int i = 1; i <= N; i++) {
			double t = i * 0.2 + 1e-6;	// just past boundary so we read v_curr
			avg_mag_0   += std::fabs(Evaluate(c0, t).slide_x);
			avg_mag_100 += std::fabs(Evaluate(c1, t).slide_x);
		}
		avg_mag_0   /= N;
		avg_mag_100 /= N;
		std::printf("  (avg event-peak slide_x: rand_min=0 → %.1fpx, rand_min=100 → %.1fpx)\n",
			avg_mag_0, avg_mag_100);
		check("randomize_min=100 raises average event peak magnitude",
			avg_mag_100 > avg_mag_0 + 30.0);
	}

	std::printf("\n%s (%d failure%s)\n",
		s_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
		s_failures, s_failures == 1 ? "" : "s");
	return s_failures == 0 ? 0 : 1;
}
