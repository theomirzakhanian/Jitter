/*******************************************************************
 * JitterEngineTwitch.h — Twitch-compatible value engine.
 *
 * A second engine implementing the original plugin's recovered model,
 * kept separate from JitterEngine so existing comps keep rendering the
 * way they did in v1.0.0. Selected per-instance by the Engine param.
 *
 * Differences from the original engine, all of them load-bearing:
 *   - Park-Miller PRNG (16807, mod 2^31-1) instead of splitmix64 hashing.
 *   - Time is measured in FRAMES, not seconds. Events land on an integer
 *     frame period, and the realised rate is fps/P rather than the
 *     requested twitches-per-second.
 *   - Events are jittered inside their interval instead of sitting on a
 *     regular grid.
 *   - No interpolation between event values: a constant amplitude is
 *     shaped by a linear tent whose ramps are measured in frames.
 *   - Master Speed multiplies every operator's rate.
 *
 * Stateless and MFR-safe: every draw runs off a local PRNG state, so
 * frames can render in any order on any thread. Do NOT switch this to
 * libc rand(); the original's global state is not thread-safe.
 *******************************************************************/

#pragma once

#include <cstdint>

namespace twitch {

/* ---- PRNG -------------------------------------------------------
 * Park-Miller / Lehmer, matching Darwin libc rand() exactly, including
 * its substitution of 123459876 for a zero state. Verified byte-for-byte
 * against libc on arm64 and x86_64 for seeds 0, 1, 42 and 999. */
class Rand {
public:
	explicit Rand(int32_t seed) : state_(static_cast<int32_t>(seed)) {}

	int32_t next()			// one rand() call
	{
		if (state_ == 0) state_ = 123459876;
		const int32_t hi = state_ / 127773;
		const int32_t lo = state_ % 127773;
		int32_t x = 16807 * lo - 2836 * hi;
		if (x < 0) x += 2147483647;
		state_ = x;
		return x;
	}

	double next01()			// closed [0,1]; the original divides by 2^31-1, not 2^31
	{
		return static_cast<double>(next()) * (1.0 / 2147483647.0);
	}

	void discard(int n) { for (int i = 0; i < n; ++i) next(); }

private:
	int32_t state_;
};

/* Per-quantity stream offsets. The original adds these to
 * (master_seed + operator_seed); there is no hashing and no operator-id
 * salt, so master 5 / op 0 collides with master 0 / op 5. That collision
 * is reproduced deliberately. */
enum StreamK {
	K_EVENT_GRID		= 0,		// event placement, all operators
	K_SLIDE_AMP			= 8000,
	K_SLIDE_ANGLE		= 19000,
	K_SPLIT_AMP_1		= 23000,
	K_SPLIT_AMP_2		= 24000,
	K_SPLIT_AMP_3		= 25000,
	K_SPLIT_ANGLE_1		= 20000,
	K_SPLIT_ANGLE_2		= 21000,
	K_SPLIT_ANGLE_3		= 22000,
	K_BLUR_AMP			= 9000,
	K_TIME_AMP			= 10000,
	K_TIME_DIRECTION	= 10001,
	K_LIGHT_AMP			= 11000,
	K_COLOR_AMP			= 12000,
	K_COLOR_RAND_R		= 16000,
	K_COLOR_RAND_G		= 17000,
	K_COLOR_RAND_B		= 18000,
	K_SCALE_AMP			= 13000,
	K_SCALE_ORIGIN_X	= 26000,
	K_SCALE_ORIGIN_Y	= 27000
};

/* Draw the value for one event index off one stream. The event index is
 * not mixed into the seed — the stream is advanced instead, so index i
 * takes the (|i|+6)th draw. Negative indices below -4 are clamped, which
 * is why times before the layer start repeat a value. */
double DrawRaw(int32_t master_seed, int32_t op_seed, int32_t k, int64_t event_idx);

/* min + u*(1-min), the floor applied to five of the six operators. */
double ApplyRandomizeMin(double u, double randomize_min_pct);

/* Slide's base channel uses a centre-preserving variant instead. */
double ApplyRandomizeMinCentered(double u, double randomize_min_pct);

/* ---- event grid -------------------------------------------------
 * P is a whole number of frames, so the realised rate is fps/P and
 * changing the comp frame rate changes the animation. That is the
 * original's behaviour, faithfully reproduced. */
int EventPeriodFrames(double fps, double twitches_per_sec, double master_speed);

/* Event k does not sit at k*P; it sits at (k + u_k)*P, with u_k drawn
 * from the operator's K_EVENT_GRID stream. */
double EventTimeFrames(int32_t master_seed, int32_t op_seed, int64_t k, int period_frames);

/* Linear tent between the bracketing events. Ease In and Ease Out are
 * frame counts and the engine uses (ease + 1), so the shipped default of
 * 1.0 gives a two-frame ramp. */
double Envelope(double t_frames, double prev_event, double next_event,
                double ease_in_frames, double ease_out_frames);

/* ---- operator values --------------------------------------------- */

struct SlideParams {
	double master_amount;		// 0..500 in the original's UI
	double master_speed;		// multiplies twitches_per_sec; default 5.0
	double amount;				// per-operator amount
	double twitches_per_sec;
	double direction_deg;
	double spread;				// 0..1
	double tendency;			// bipolar, -1..+1
	double randomize_min_pct;
	double rgb_split;			// 0..100, a blend weight, not a pixel offset
	double ease_in_frames;
	double ease_out_frames;
	int32_t master_seed;
	int32_t op_seed;
};

struct SlideResult {
	double x, y;				// base slide, in pixels at the current resolution
	double r_x, r_y;			// per-channel positions when rgb_split > 0
	double g_x, g_y;
	double b_x, b_y;
};

/* width/height are the layer's dimensions at the CURRENT resolution: the
 * original derives them from in_data->width/height scaled by the
 * downsample factors, not from the checked-out world (which under
 * I_EXPAND_BUFFER is larger than the layer).
 *
 * Motion blur is deliberately not implemented here. The original emits
 * cur + (MB/2)*(alt - cur), and the alternate sample's derivation is not
 * yet recovered, so this returns the MB=0 position. */
SlideResult EvaluateSlide(const SlideParams &p, double frame, double fps,
                          double width, double height);

}	// namespace twitch
