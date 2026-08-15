/*******************************************************************/
/*                                                                 */
/*                      Jitter Plugin                              */
/*                Apple Silicon native port of                     */
/*                Video Copilot Twitch                             */
/*                                                                 */
/*  Param order EXACTLY matches Twitch — extracted from the        */
/*  physical layout of UI label strings in __DATA __data of        */
/*  twitch.plugin's binary. Disk IDs assigned sequentially starting */
/*  at 1, in the same order Twitch's PARAMS_SETUP adds them, so    */
/*  saved Twitch projects load with correct param values.          */
/*                                                                 */
/*  Implementation status:                                         */
/*    Slide  — XY motion, Direction/Spread/Tendency, RGB Split    */
/*    Scale  — zoom around Scale Origin point                      */
/*    Time   — frame offset with Direction popup                   */
/*    Color  — HSL hue rotate + Colorize                          */
/*    Light  — luma-gated brightness shift                         */
/*    Blur   — directional gaussian (FFT version is M5+)           */
/*    Behaviour — Ease In, Ease Out, Randomize Minimum             */
/*    Show Twitch Only — debug fill                                */
/*                                                                 */
/*    UI present, render no-op (M4+):                              */
/*    Slide/Scale Motion Blur, Use Lens Blur, Crash Zoom,          */
/*    deeper Color Randomize beyond Colorize.                      */
/*                                                                 */
/*******************************************************************/

#pragma once

#ifndef JITTER_H
#define JITTER_H

#define PF_DEEP_COLOR_AWARE 1

#define PF_TABLE_BITS	12
#define PF_TABLE_SZ_16	4096

#include "AEConfig.h"
#include "entry.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_Macros.h"
#include "Param_Utils.h"
#include "AE_EffectCBSuites.h"
#include "String_Utils.h"
#include "AE_GeneralPlug.h"
#include "AEFX_ChannelDepthTpl.h"
#include "AEGP_SuiteHandler.h"
#include "Smart_Utils.h"

#include "Jitter_Strings.h"

/* Versioning. Matches Twitch's PiPL exactly: PF_VERSION(1, 1, 0, RELEASE, 0). */
#define	MAJOR_VERSION	1
#define	MINOR_VERSION	1
#define	BUG_VERSION		0
#define	STAGE_VERSION	PF_Stage_RELEASE
#define	BUILD_VERSION	0

/* ---- Parameter ranges ---- */
#define	AMOUNT_MIN			0.0
#define	AMOUNT_MAX			100.0
#define	AMOUNT_DFLT			100.0

#define	SEED_MIN			0
#define	SEED_MAX			10000
#define	SEED_DFLT			0

/* Per-operator standard ranges */
#define	OP_AMOUNT_MIN		0.0
#define	OP_AMOUNT_MAX		100.0
/* Stays 0.0 for now, though the original defaults to 50.0.
 *
 * At 0.0 a freshly applied effect does nothing, because Slide is enabled by
 * default but the engine short-circuits any operator whose amount is 0. That
 * is a poor first impression and worth fixing — but not before the slide
 * magnitude does. MAX_SLIDE_PIXELS is a fixed 500px regardless of layer size,
 * so raising this default to 50.0 renders a small layer FULLY TRANSPARENT at
 * defaults (test_full_render catches exactly this) and a 1080p layer about 20%
 * transparent. The original avoids it by making slide a fraction of the frame.
 * Raise this together with that change, not before it. */
#define	OP_AMOUNT_DFLT		0.0
#define	OP_TWITCHES_MIN		0.0
#define	OP_TWITCHES_MAX		100.0
#define	OP_TWITCHES_DFLT	10.0
#define	OP_SEED_MIN			0
#define	OP_SEED_MAX			10000
#define	OP_SEED_DFLT		0

/* Behaviour group */
#define	EASE_IN_DFLT		50.0
#define	EASE_OUT_DFLT		50.0
#define	RANDOMIZE_MIN_DFLT	0.0

/* Slide sub-params */
#define	SLIDE_DIR_DFLT			0.0
#define	SLIDE_SPREAD_DFLT		100.0
#define	SLIDE_TENDENCY_DFLT		0.0
#define	SLIDE_MOBLUR_DFLT		0.0
#define	SLIDE_RGB_SPLIT_DFLT	0.0

/* Time Direction popup choices (1-based per AE convention) */
#define	TIME_DIR_FORWARD_ONLY	1
#define	TIME_DIR_BACKWARD_ONLY	2
#define	TIME_DIR_BOTH			3

/* Light Behaviour popup choices */
#define	LIGHT_BEH_BRIGHTER		1
#define	LIGHT_BEH_DARKER		2
#define	LIGHT_BEH_BOTH			3

/* Blur Transfer Mode popup choices — order matches Twitch's binary string layout */
#define	BLUR_XFER_NORMAL		1
#define	BLUR_XFER_SCREEN		2
#define	BLUR_XFER_MULTIPLY		3
#define	BLUR_XFER_OVERLAY		4

/* Parameter indices — order MUST match the ParamsSetup add sequence below.
 * The order is taken from Twitch's __DATA __data physical layout. */
enum {
	JITTER_INPUT = 0,

	AMOUNT_PARAM,
	SPEED_PARAM,	// Twitch has a master Speed slider at disk ID 1; we declare it for save/load compat (engine currently ignores)

	/* Enable group — alphabetical: Blur, Color, Light, Scale, Slide, Time */
	ENABLE_GROUP_START,
	ENABLE_BLUR,
	ENABLE_COLOR,
	ENABLE_LIGHT,
	ENABLE_SCALE,
	ENABLE_SLIDE,
	ENABLE_TIME,
	ENABLE_GROUP_END,

	/* Behaviour group — INCLUDES Master Seed and Show Twitch Only inside. */
	BEHAVIOUR_GROUP_START,
	BEHAVIOUR_EASE_IN,
	BEHAVIOUR_EASE_OUT,
	SHOW_TWITCH_ONLY,
	SEED_PARAM,
	BEHAVIOUR_RANDOMIZE_MIN,
	BEHAVIOUR_GROUP_END,

	/* Operator Controls — alphabetical sub-groups */
	OPERATOR_CONTROLS_GROUP_START,

	/* Blur — Amount, Twitches, Tint, Holdout, Sharpness, Boost, Opacity,
	 * Transfer Mode, Aspect, Use Lens Blur, Seed */
	BLUR_GROUP_START,
	BLUR_AMOUNT,
	BLUR_TWITCHES,
	BLUR_TINT,
	BLUR_HOLDOUT,
	BLUR_HOLDOUT_SHARPNESS,
	BLUR_BOOST,
	BLUR_OPACITY,
	BLUR_TRANSFER_MODE,
	BLUR_ASPECT,
	BLUR_USE_LENS_BLUR,
	BLUR_SEED,
	BLUR_GROUP_END,

	/* Color — Amount, Twitches, Colorize, Randomize, Seed */
	COLOR_GROUP_START,
	COLOR_AMOUNT,
	COLOR_TWITCHES,
	COLOR_COLORIZE,
	COLOR_RANDOMIZE,
	COLOR_SEED,
	COLOR_GROUP_END,

	/* Light — Amount, Twitches, Behaviour, Seed */
	LIGHT_GROUP_START,
	LIGHT_AMOUNT,
	LIGHT_TWITCHES,
	LIGHT_BEHAVIOUR,
	LIGHT_SEED,
	LIGHT_GROUP_END,

	/* Scale — Amount, Twitches, Origin, Origin Randomize, Motion Blur, Seed */
	SCALE_GROUP_START,
	SCALE_AMOUNT,
	SCALE_TWITCHES,
	SCALE_ORIGIN,
	SCALE_ORIGIN_RANDOMIZE,
	SCALE_MOTION_BLUR,
	SCALE_SEED,
	SCALE_GROUP_END,

	/* Slide — Amount, Twitches, Direction, Spread, Tendency, RGB Split,
	 * Motion Blur, Seed */
	SLIDE_GROUP_START,
	SLIDE_AMOUNT,
	SLIDE_TWITCHES,
	SLIDE_DIRECTION,
	SLIDE_SPREAD,
	SLIDE_TENDENCY,
	SLIDE_RGB_SPLIT,
	SLIDE_MOTION_BLUR,
	SLIDE_SEED,
	SLIDE_GROUP_END,

	/* Time — Amount, Twitches, Direction, Seed */
	TIME_GROUP_START,
	TIME_AMOUNT,
	TIME_TWITCHES,
	TIME_DIRECTION,
	TIME_SEED,
	TIME_GROUP_END,

	OPERATOR_CONTROLS_GROUP_END,

	JITTER_NUM_PARAMS	// must equal the count of all entries above
};

/* Disk IDs — non-sequential, assigned chronologically rather than by UI
 * order. The specific values matter for saved-project compatibility. */
enum {
	SPEED_DISK_ID                        = 1,
	AMOUNT_DISK_ID                       = 2,
	COLOR_COLORIZE_DISK_ID               = 3,
	ENABLE_COLOR_DISK_ID                 = 4,
	ENABLE_LIGHT_DISK_ID                 = 5,
	ENABLE_SCALE_DISK_ID                 = 6,
	ENABLE_BLUR_DISK_ID                  = 7,
	ENABLE_SLIDE_DISK_ID                 = 8,
	ENABLE_TIME_DISK_ID                  = 9,
	BEHAVIOUR_EASE_IN_DISK_ID            = 10,
	BEHAVIOUR_EASE_OUT_DISK_ID           = 11,
	OPERATOR_CONTROLS_GROUP_START_DISK_ID = 12,
	SHOW_TWITCH_ONLY_DISK_ID             = 13,
	SLIDE_MOTION_BLUR_DISK_ID            = 14,
	/* disk 15 = Twitch's "Border" popup (Slide edge mode) — unimplemented */
	SEED_DISK_ID                         = 16,
	COLOR_GROUP_START_DISK_ID            = 17,
	COLOR_AMOUNT_DISK_ID                 = 18,
	COLOR_TWITCHES_DISK_ID               = 19,
	COLOR_RANDOMIZE_DISK_ID              = 20,
	COLOR_SEED_DISK_ID                   = 21,
	COLOR_GROUP_END_DISK_ID              = 22,
	LIGHT_GROUP_START_DISK_ID            = 23,
	LIGHT_AMOUNT_DISK_ID                 = 24,
	LIGHT_TWITCHES_DISK_ID               = 25,
	LIGHT_SEED_DISK_ID                   = 26,
	LIGHT_GROUP_END_DISK_ID              = 27,
	SCALE_GROUP_START_DISK_ID            = 28,
	SCALE_AMOUNT_DISK_ID                 = 29,
	SCALE_TWITCHES_DISK_ID               = 30,
	SCALE_SEED_DISK_ID                   = 31,
	SCALE_GROUP_END_DISK_ID              = 32,
	BLUR_GROUP_START_DISK_ID             = 33,
	BLUR_AMOUNT_DISK_ID                  = 34,
	BLUR_TWITCHES_DISK_ID                = 35,
	BLUR_OPACITY_DISK_ID                 = 36,
	BLUR_TRANSFER_MODE_DISK_ID           = 37,
	BLUR_ASPECT_DISK_ID                  = 38,
	BLUR_TINT_DISK_ID                    = 39,
	BLUR_HOLDOUT_DISK_ID                 = 40,
	BLUR_BOOST_DISK_ID                   = 41,
	BLUR_SEED_DISK_ID                    = 42,
	BLUR_GROUP_END_DISK_ID               = 43,
	SLIDE_GROUP_START_DISK_ID            = 44,
	SLIDE_AMOUNT_DISK_ID                 = 45,
	SLIDE_TWITCHES_DISK_ID               = 46,
	SLIDE_DIRECTION_DISK_ID              = 49,
	SLIDE_SEED_DISK_ID                   = 50,
	SLIDE_GROUP_END_DISK_ID              = 51,
	TIME_GROUP_START_DISK_ID             = 52,
	TIME_SEED_DISK_ID                    = 53,
	TIME_TWITCHES_DISK_ID                = 54,
	TIME_DIRECTION_DISK_ID               = 55,
	TIME_AMOUNT_DISK_ID                  = 56,
	TIME_GROUP_END_DISK_ID               = 57,
	OPERATOR_CONTROLS_GROUP_END_DISK_ID  = 58,
	BLUR_HOLDOUT_SHARPNESS_DISK_ID       = 59,
	BLUR_USE_LENS_BLUR_DISK_ID           = 60,
	SLIDE_SPREAD_DISK_ID                 = 61,
	SLIDE_TENDENCY_DISK_ID               = 62,
	BEHAVIOUR_RANDOMIZE_MIN_DISK_ID      = 63,
	ENABLE_GROUP_START_DISK_ID           = 64,
	ENABLE_GROUP_END_DISK_ID             = 65,
	BEHAVIOUR_GROUP_START_DISK_ID        = 66,
	BEHAVIOUR_GROUP_END_DISK_ID          = 67,
	LIGHT_BEHAVIOUR_DISK_ID              = 68,
	SLIDE_RGB_SPLIT_DISK_ID              = 77,
	SCALE_ORIGIN_DISK_ID                 = 78,
	SCALE_ORIGIN_RANDOMIZE_DISK_ID       = 79,
	SCALE_MOTION_BLUR_DISK_ID            = 82
};

extern "C" {
	DllExport
	PF_Err
	EffectMain(
		PF_Cmd			cmd,
		PF_InData		*in_data,
		PF_OutData		*out_data,
		PF_ParamDef		*params[],
		PF_LayerDef		*output,
		void			*extra);
}

#endif // JITTER_H
