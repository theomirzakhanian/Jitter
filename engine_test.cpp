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
			t, o.slide_x, o.slide_y, o.scale, o.rgb_split_x, o.rgb_split_y);
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
			a.rgb_split_x == b.rgb_split_x);
	}

	// ---- Seed independence between operators ----
	{
		Config c2 = c;
		c2.scale.seed = 999;
		Output o1 = Evaluate(c, 1.234);
		Output o2 = Evaluate(c2, 1.234);
		check("scale seed change does not affect slide",   o1.slide_x == o2.slide_x);
		check("scale seed change does affect scale",       o1.scale != o2.scale);
		check("scale seed change does not affect rgb_split", o1.rgb_split_x == o2.rgb_split_x);
	}

	// ---- Master seed reshuffles everything ----
	{
		Config c2 = c;
		c2.master.seed = 7;
		Output o1 = Evaluate(c, 1.234);
		Output o2 = Evaluate(c2, 1.234);
		check("master seed change affects slide",     o1.slide_x != o2.slide_x);
		check("master seed change affects scale",     o1.scale != o2.scale);
		check("master seed change affects rgb_split", o1.rgb_split_x != o2.rgb_split_x);
	}

	// ---- Disabled operator returns identity ----
	{
		Config c2 = c;
		c2.slide.enabled = false;
		Output o = Evaluate(c2, 1.234);
		check("disabled slide → slide_x == 0",      o.slide_x == 0.0);
		check("disabled slide → slide_y == 0",      o.slide_y == 0.0);
		check("disabled slide → rgb_split_x == 0",  o.rgb_split_x == 0.0);
	}

	// ---- Master amount=0 zeros everything (scale stays at 1.0) ----
	{
		Config c2 = c;
		c2.master.amount = 0.0;
		Output o = Evaluate(c2, 1.234);
		check("master.amount=0 → slide_x == 0",       o.slide_x == 0.0);
		check("master.amount=0 → scale == 1.0",       o.scale == 1.0);
		check("master.amount=0 → rgb_split_x == 0",   o.rgb_split_x == 0.0);
	}

	// ---- Slide channels are independent (XY vs RGB-split) ----
	{
		double max_xy_vs_rgb = 0.0;
		for (int i = 0; i < 200; i++) {
			Output o = Evaluate(c, i * 0.07);
			// Compare normalized magnitudes — they should diverge.
			double xy   = std::fabs(o.slide_x) / 200.0;
			double rgb  = std::fabs(o.rgb_split_x) / 50.0;
			if (std::fabs(xy - rgb) > max_xy_vs_rgb) max_xy_vs_rgb = std::fabs(xy - rgb);
		}
		check("slide XY and slide RGB-split are independent (max norm diff > 0.3)",
			max_xy_vs_rgb > 0.3);
	}

	// ---- Slide RGB-split x and y are independent ----
	{
		double max_abs_diff = 0.0;
		for (int i = 0; i < 200; i++) {
			Output o = Evaluate(c, i * 0.07);
			double diff = std::fabs(o.rgb_split_x - o.rgb_split_y);
			if (diff > max_abs_diff) max_abs_diff = diff;
		}
		check("rgb_split_x and rgb_split_y are independent (max|x-y| > 10px)",
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
			if (std::fabs(o.rgb_split_x) > max_rgb)       max_rgb       = std::fabs(o.rgb_split_x);
			double dev = std::fabs(o.scale - 1.0);
			if (dev > max_scale_dev) max_scale_dev = dev;
		}
		std::printf("  (observed max: slide=%.1fpx  rgb_split=%.1fpx  scale_dev=%.3f)\n",
			max_slide, max_rgb, max_scale_dev);
		check("slide stays within +/- 500px envelope", max_slide <= 500.0001);
		check("rgb_split stays within +/- 50px envelope", max_rgb <= 50.0001);
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
