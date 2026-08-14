/*******************************************************************
 * twitch_engine_test.cpp — tests for the Twitch-compatible engine.
 *
 * The PRNG expectations are real libc rand() output, captured from this
 * machine on both arm64 and x86_64 (they agree). If Rand ever stops
 * matching them, parity with the original is broken.
 *
 *   clang++ -std=c++17 -O2 twitch_engine_test.cpp JitterEngineTwitch.cpp \
 *     -o twitch_engine_test && ./twitch_engine_test
 *******************************************************************/

#include "JitterEngineTwitch.h"

#include <cstdio>
#include <cmath>
#include <thread>
#include <vector>
#include <algorithm>

static int g_failures = 0;

static void check(bool ok, const char *what)
{
	std::printf("  %-62s %s\n", what, ok ? "OK" : "FAIL");
	if (!ok) ++g_failures;
}

static bool close_to(double a, double b, double eps = 1e-12)
{
	return std::fabs(a - b) <= eps;
}

/* Slide magnitude. Tests use this rather than a single component, since
 * direction 0 puts all the motion in y. */
static double mag(const twitch::SlideResult &r)
{
	return std::sqrt(r.x * r.x + r.y * r.y);
}

/* ---- 1. PRNG matches libc exactly ------------------------------- */
static void test_rand()
{
	std::printf("\nPark-Miller against captured libc output\n");

	struct Case { int32_t seed; int32_t expect[6]; };
	const Case cases[] = {
		{   0, {520932930,  28925691,  822784415, 890459872, 145532761, 2132723841}},
		{   1, {    16807, 282475249, 1622650073, 984943658, 1144108930, 470211272}},
		{  42, {   705894, 1126542223, 1579310009, 565444343, 807934826, 421520601}},
		{ 999, { 16790193, 872415994, 1824753089, 411204016, 503520866, 1589625682}},
	};

	for (const Case &c : cases) {
		twitch::Rand r(c.seed);
		bool ok = true;
		for (int i = 0; i < 6; ++i) if (r.next() != c.expect[i]) ok = false;
		char msg[96];
		std::snprintf(msg, sizeof(msg), "seed %d reproduces libc's first six draws", c.seed);
		check(ok, msg);
	}

	// Seed 0 must route through the 123459876 substitution, not stay stuck.
	twitch::Rand z(0);
	check(z.next() == 520932930, "seed 0 takes the 123459876 substitution path");

	// The scale is 1/(2^31-1), not 1/2^31: the largest draw reaches exactly 1.0.
	twitch::Rand m(1);
	double v = m.next01();
	check(close_to(v, 16807.0 / 2147483647.0), "next01 divides by 2^31-1, not 2^31");
	check(v >= 0.0 && v <= 1.0, "next01 stays within [0,1]");
}

/* ---- 2. stream position and seed derivation --------------------- */
static void test_draw()
{
	std::printf("\nStream derivation\n");

	// Index 0 discards five draws and takes the sixth.
	twitch::Rand ref(1);
	ref.discard(5);
	const double expect = ref.next01();
	check(close_to(twitch::DrawRaw(1, 0, 0, 0), expect), "event index 0 takes the sixth draw");

	// The seeds add with no mixing, so these two configurations collide.
	// The original behaves this way and parity requires reproducing it.
	check(close_to(twitch::DrawRaw(5, 0, 0, 3), twitch::DrawRaw(0, 5, 0, 3)),
	      "master+operator seeds collide additively, as the original does");

	// Different K values must give independent streams.
	check(!close_to(twitch::DrawRaw(7, 2, twitch::K_SLIDE_AMP, 4),
	                twitch::DrawRaw(7, 2, twitch::K_SLIDE_ANGLE, 4)),
	      "different stream offsets produce different values");

	// Indices below -4 clamp, so they repeat.
	check(close_to(twitch::DrawRaw(3, 1, 0, -5), twitch::DrawRaw(3, 1, 0, -9)),
	      "indices below -4 clamp to the same value");

	// Determinism: same inputs, same output, no hidden state.
	check(close_to(twitch::DrawRaw(11, 3, twitch::K_BLUR_AMP, 99),
	               twitch::DrawRaw(11, 3, twitch::K_BLUR_AMP, 99)),
	      "draws are deterministic across calls");
}

/* ---- 3. randomize minimum --------------------------------------- */
static void test_randomize_min()
{
	std::printf("\nRandomize Minimum\n");

	check(close_to(twitch::ApplyRandomizeMin(0.0, 0.0), 0.0) &&
	      close_to(twitch::ApplyRandomizeMin(1.0, 0.0), 1.0),
	      "min 0 leaves the uniform untouched");
	check(close_to(twitch::ApplyRandomizeMin(0.0, 100.0), 1.0),
	      "min 100 pins every draw to the maximum");
	check(twitch::ApplyRandomizeMin(0.0, 40.0) >= 0.4 - 1e-12,
	      "min 40 floors the draw at 0.4");

	bool in_range = true;
	for (double u = 0.0; u <= 1.0; u += 0.05) {
		double v = twitch::ApplyRandomizeMinCentered(u, 30.0);
		if (v < 0.0 || v > 1.0) in_range = false;
	}
	check(in_range, "centred variant stays inside [0,1]");
	check(close_to(twitch::ApplyRandomizeMinCentered(0.5, 0.0), 0.5),
	      "centred variant keeps 0.5 at the centre");
}

/* ---- 4. event grid ---------------------------------------------- */
static void test_grid()
{
	std::printf("\nEvent grid\n");

	// 29.97fps, 1 twitch/sec, speed 5 -> (int)5.994 = 5 frames.
	check(twitch::EventPeriodFrames(29.97, 1.0, 5.0) == 5,
	      "29.97fps tps=1 speed=5 truncates to a 5-frame period");
	check(twitch::EventPeriodFrames(24.0, 1.0, 5.0) == 4,
	      "24fps tps=1 speed=5 gives a 4-frame period");
	check(twitch::EventPeriodFrames(24.0, 100.0, 5.0) == 1,
	      "a rate finer than one event per frame floors at 1");
	check(twitch::EventPeriodFrames(24.0, 1.0, 0.0) == 1,
	      "speed 0 does not divide by zero");

	// Master Speed genuinely multiplies the rate.
	check(twitch::EventPeriodFrames(30.0, 1.0, 1.0) != twitch::EventPeriodFrames(30.0, 1.0, 5.0),
	      "master speed changes the period");

	// Events are jittered inside their interval, never on the boundary.
	bool jittered = false, in_interval = true;
	for (int64_t k = 0; k < 20; ++k) {
		double e = twitch::EventTimeFrames(7, 1, k, 5);
		double lo = static_cast<double>(k) * 5.0, hi = lo + 5.0;
		if (e < lo || e > hi) in_interval = false;
		if (std::fabs(e - lo) > 1e-9) jittered = true;
	}
	check(in_interval, "each event lands inside its own interval");
	check(jittered, "events are jittered, not pinned to the grid");
}

/* ---- 5. envelope ------------------------------------------------- */
static void test_envelope()
{
	std::printf("\nTent envelope\n");

	// Ease of 1 frame means a two-frame ramp, since the engine adds one.
	check(close_to(twitch::Envelope(10.0, 10.0, 20.0, 1.0, 1.0), 1.0),
	      "envelope peaks at the event itself");
	check(twitch::Envelope(14.0, 10.0, 20.0, 1.0, 1.0) <= 0.0 + 1e-12,
	      "envelope reaches zero between distant events");
	check(close_to(twitch::Envelope(19.0, 10.0, 20.0, 1.0, 1.0), 0.5),
	      "one frame before the next event the tent is half up");
	check(twitch::Envelope(11.0, 10.0, 20.0, 1.0, 1.0) >= 0.0,
	      "envelope never goes negative");

	// A longer ease widens the ramp rather than rescaling it.
	check(twitch::Envelope(13.0, 10.0, 20.0, 5.0, 5.0) >
	      twitch::Envelope(13.0, 10.0, 20.0, 1.0, 1.0),
	      "a longer ease holds the envelope higher for longer");
}

/* ---- 6. slide ---------------------------------------------------- */
static twitch::SlideParams defaults()
{
	twitch::SlideParams p;
	p.master_amount = 100.0;
	p.master_speed = 5.0;
	p.amount = 50.0;
	p.twitches_per_sec = 1.0;
	p.direction_deg = 0.0;
	p.spread = 0.0;
	p.tendency = 0.0;
	p.randomize_min_pct = 0.0;
	p.rgb_split = 0.0;
	p.ease_in_frames = 1.0;
	p.ease_out_frames = 1.0;
	p.master_seed = 0;
	p.op_seed = 0;
	return p;
}

static void test_slide()
{
	std::printf("\nSlide operator\n");

	twitch::SlideParams p = defaults();

	twitch::SlideResult a = twitch::EvaluateSlide(p, 12.0, 24.0, 1920, 1080);
	twitch::SlideResult b = twitch::EvaluateSlide(p, 12.0, 24.0, 1920, 1080);
	check(close_to(a.x, b.x) && close_to(a.y, b.y), "evaluation is deterministic");

	// Magnitude scales with the frame, so 4K moves twice as far as 1080p.
	twitch::SlideResult hd = twitch::EvaluateSlide(p, 12.0, 24.0, 1920, 1080);
	twitch::SlideResult uhd = twitch::EvaluateSlide(p, 12.0, 24.0, 3840, 2160);
	check(close_to(uhd.x, hd.x * 2.0, 1e-9) && close_to(uhd.y, hd.y * 2.0, 1e-9),
	      "slide is a fraction of the frame, so 4K travels twice as far");

	// Direction is compass-style: the angle feeds -sin into x and -cos into
	// y, so 0 degrees slides vertically and 90 slides horizontally. Angle 0
	// is a real direction, not the sentinel the original Jitter engine
	// treated it as.
	p = defaults();
	twitch::SlideResult d0 = twitch::EvaluateSlide(p, 7.0, 24.0, 1920, 1080);
	check(std::fabs(d0.y) > 1e-9, "direction 0 produces motion rather than being skipped");
	check(close_to(d0.x, 0.0, 1e-9), "direction 0 is vertical: x stays zero");

	p.direction_deg = 90.0;
	twitch::SlideResult d90 = twitch::EvaluateSlide(p, 7.0, 24.0, 1920, 1080);
	check(std::fabs(d90.x) > 1e-9 && close_to(d90.y, 0.0, 1e-9),
	      "direction 90 is horizontal: y stays zero");

	// Tendency is bipolar: the two signs must not agree.
	p = defaults();
	p.tendency = -1.0;
	double neg = mag(twitch::EvaluateSlide(p, 7.0, 24.0, 1920, 1080));
	p.tendency = 1.0;
	double pos = mag(twitch::EvaluateSlide(p, 7.0, 24.0, 1920, 1080));
	check(!close_to(neg, pos), "tendency is bipolar, both signs reachable");

	// Amplitude must stay bounded by the sliders; the old engine could
	// amplify tenfold past them through its power curve.
	p = defaults();
	p.amount = 1.0;			// a 1% slide
	double small = mag(twitch::EvaluateSlide(p, 7.0, 24.0, 1920, 1080));
	p.amount = 50.0;
	double large = mag(twitch::EvaluateSlide(p, 7.0, 24.0, 1920, 1080));
	check(small <= large + 1e-9, "a smaller Amount never produces a larger slide");

	// RGB split is a blend: at full split the channels separate. Give the
	// split vectors an angular spread so the separation is visible in both
	// components rather than collapsing onto one axis.
	p = defaults();
	p.spread = 1.0;
	p.rgb_split = 100.0;
	twitch::SlideResult s = twitch::EvaluateSlide(p, 7.0, 24.0, 1920, 1080);
	check(!close_to(s.r_x, s.b_x) || !close_to(s.r_y, s.b_y),
	      "full split separates the channels");
	check(!close_to(s.r_x, s.x) || !close_to(s.r_y, s.y),
	      "full split moves channels off the coherent slide");
	p.rgb_split = 0.0;
	twitch::SlideResult ns = twitch::EvaluateSlide(p, 7.0, 24.0, 1920, 1080);
	check(close_to(ns.r_x, ns.x) && close_to(ns.b_x, ns.x),
	      "zero split leaves all channels on the base slide");
}

/* ---- 7. MFR safety ----------------------------------------------- */
static void test_threading()
{
	std::printf("\nMulti-frame rendering safety\n");

	twitch::SlideParams p = defaults();

	std::vector<double> serial;
	for (int f = 0; f < 200; ++f)
		serial.push_back(mag(twitch::EvaluateSlide(p, f, 24.0, 1920, 1080)));

	// Same frames, evaluated out of order across threads.
	std::vector<double> parallel(200, 0.0);
	std::vector<std::thread> pool;
	for (int t = 0; t < 8; ++t) {
		pool.emplace_back([&, t] {
			for (int f = 199 - t; f >= 0; f -= 8)
				parallel[f] = mag(twitch::EvaluateSlide(p, f, 24.0, 1920, 1080));
		});
	}
	for (auto &th : pool) th.join();

	check(serial == parallel, "out-of-order threaded evaluation matches serial");

	bool any_motion = std::any_of(serial.begin(), serial.end(),
	                              [](double v) { return std::fabs(v) > 1e-9; });
	check(any_motion, "the sequence actually moves");
}

int main()
{
	std::printf("Twitch-compatible engine tests\n");
	test_rand();
	test_draw();
	test_randomize_min();
	test_grid();
	test_envelope();
	test_slide();
	test_threading();

	std::printf("\n%s (%d failures)\n",
	            g_failures ? "TESTS FAILED" : "ALL TESTS PASSED", g_failures);
	return g_failures ? 1 : 0;
}
