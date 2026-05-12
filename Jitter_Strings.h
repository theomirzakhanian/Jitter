/*******************************************************************/
/*                                                                 */
/*                      Jitter Plugin                              */
/*                   String Resource IDs                           */
/*                                                                 */
/*  All UI labels match the original Video Copilot Twitch plugin   */
/*  (extracted via `strings` from twitch.plugin's Mach-O binary).  */
/*                                                                 */
/*******************************************************************/

#pragma once

typedef enum {
	StrID_NONE,
	StrID_Name_Jitter,
	StrID_Name_Twitch,
	StrID_Description,

	// Master
	StrID_Amount,
	StrID_Seed,

	// Enable group
	StrID_Enable_Group,
	StrID_Enable_Slide,
	StrID_Enable_Scale,
	StrID_Enable_Time,
	StrID_Enable_Color,
	StrID_Enable_Light,
	StrID_Enable_Blur,

	// Behaviour group
	StrID_Behaviour_Group,
	StrID_Behaviour_Ease_In,
	StrID_Behaviour_Ease_Out,
	StrID_Behaviour_Randomize_Minimum,

	// Operator Controls outer group
	StrID_Operator_Controls_Group,

	// Slide operator
	StrID_Slide_Group,
	StrID_Slide_Amount,
	StrID_Slide_Twitches,
	StrID_Slide_Seed,
	StrID_Slide_Direction,
	StrID_Slide_Spread,
	StrID_Slide_Tendency,
	StrID_Slide_Motion_Blur,
	StrID_Slide_RGB_Split,

	// Scale operator
	StrID_Scale_Group,
	StrID_Scale_Amount,
	StrID_Scale_Twitches,
	StrID_Scale_Seed,
	StrID_Scale_Origin,
	StrID_Scale_Origin_Randomize,
	StrID_Scale_Motion_Blur,
	StrID_Scale_Use_Lens_Blur,

	// Time operator
	StrID_Time_Group,
	StrID_Time_Amount,
	StrID_Time_Twitches,
	StrID_Time_Seed,
	StrID_Time_Direction,
	StrID_Time_Direction_Choices,	// "Forward Only|Backward Only|Both"

	// Color operator
	StrID_Color_Group,
	StrID_Color_Amount,
	StrID_Color_Twitches,
	StrID_Color_Seed,
	StrID_Color_Randomize,
	StrID_Color_Colorize,

	// Light operator
	StrID_Light_Group,
	StrID_Light_Amount,
	StrID_Light_Twitches,
	StrID_Light_Seed,
	StrID_Light_Behaviour,
	StrID_Light_Behaviour_Choices,	// "Brighter|Darker|Both"

	// Blur operator
	StrID_Blur_Group,
	StrID_Blur_Amount,
	StrID_Blur_Twitches,
	StrID_Blur_Seed,
	StrID_Blur_Aspect,
	StrID_Blur_Boost,
	StrID_Blur_Holdout,
	StrID_Blur_Holdout_Sharpness,
	StrID_Blur_Opacity,
	StrID_Blur_Tint,
	StrID_Blur_Transfer_Mode,
	StrID_Blur_Transfer_Mode_Choices,	// "Normal|Multiply|Screen|Overlay"

	StrID_Show_Twitch_Only,

	StrID_NUMTYPES
} StrIDType;
