/*******************************************************************/
/*                                                                 */
/*                      Jitter Engine                              */
/*                                                                 */
/*  Pure C++, no AE SDK types. Stateless, deterministic, MFR-safe. */
/*  Given the same (config, t) produces the same Output, on any    */
/*  thread, in any order, regardless of when called.               */
/*                                                                 */
/*  Operator model matches Video Copilot Twitch:                   */
/*    Slide  — slide_x, slide_y, rgb_split_x, rgb_split_y          */
/*    Scale  — scale deviation                                     */
/*    Time   — frame offset in seconds                             */
/*    Color  — placeholder (HSL filter, v3)                        */
/*    Light  — placeholder (glow filter, v3)                       */
/*    Blur   — placeholder (FFT filter, v3)                        */
/*                                                                 */
/*  No standalone Rotation, no standalone RGB Split — those don't  */
/*  exist in the original Twitch UI.                               */
/*                                                                 */
/*******************************************************************/

#pragma once

#include <cstdint>

namespace jitter {

// Per-operator parameters. Mirrors the AE param block for one operator group.
struct OperatorParams {
	double		amount;				// 0..100, intensity of this operator
	double		twitches_per_sec;	// 0..100, event frequency for this operator
	uint32_t	seed;				// 0..10000, per-operator seed
	bool		enabled;			// from the Enable group checkbox
};

// Master parameters — apply across all operators.
// Behaviour controls (ease_in, ease_out, randomize_min) shape the
// inter-event interpolation curve and the random-target distribution.
struct MasterParams {
	double		amount;			// 0..100, scales every operator's output
	double		ease_in;		// 0..100, curve shape near event start
	double		ease_out;		// 0..100, curve shape near event end
	double		randomize_min;	// 0..100, lower bound on random magnitude (0 = full range, 100 = max only)
	uint32_t	seed;			// 0..10000, master seed combined with each op seed
};

// Full config: master + six operators (matches Twitch's UI).
struct Config {
	MasterParams	master;
	OperatorParams	slide;
	OperatorParams	scale;
	OperatorParams	time_op;
	OperatorParams	color;	// v3 placeholder, evaluated for engine completeness
	OperatorParams	light;	// v3 placeholder
	OperatorParams	blur;	// v3 placeholder
};

// Engine output. Each field is in its operator's natural unit so the render
// path can use it directly without further scaling.
//
// Slide produces four independent channels driven by the same event sequence:
// XY translation plus an RGB-split offset vector. RGB Split is a sub-param
// of Slide in Twitch, not its own operator — when slide_amount=0 there is
// no RGB split either, by design.
struct Output {
	double	slide_x;		// pixels  (positive = right)
	double	slide_y;		// pixels  (positive = down)
	double	rgb_split_x;	// pixels  (per-channel chromatic aberration, x)
	double	rgb_split_y;	// pixels  (per-channel chromatic aberration, y)
	double	scale;			// multiplier (1.0 = no change)
	double	time_offset;	// seconds
	double	color;			// (v3) raw [-1,1]
	double	light;			// (v3) raw [-1,1]
	double	blur;			// (v3) raw [-1,1]
};

// Evaluate the engine at time t (seconds). Pure function; thread-safe.
Output Evaluate(const Config& cfg, double t_seconds);

} // namespace jitter
