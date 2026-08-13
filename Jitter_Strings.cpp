/*******************************************************************/
/*                                                                 */
/*                      Jitter Plugin                              */
/*                   String Resource Table                         */
/*                                                                 */
/*  Strings match the original Video Copilot Twitch UI exactly,    */
/*  verified via `strings` against twitch.plugin's Mach-O binary.  */
/*                                                                 */
/*******************************************************************/

#include "Jitter.h"

typedef struct {
	A_u_long	index;
	A_char		str[256];
} TableString;

TableString g_strs[StrID_NUMTYPES] = {
	{ StrID_NONE,                       "" },
	{ StrID_Name_Jitter,                "Jitter" },
	{ StrID_Name_Twitch,                "Twitch" },
	{ StrID_Description,                "Apple Silicon native port of Video Copilot Twitch.\rCopyright 2024" },

	{ StrID_Amount,                     "Amount" },
	{ StrID_Speed,                      "Speed" },
	{ StrID_Seed,                       "Seed" },

	{ StrID_Enable_Group,               "Enable" },
	{ StrID_Enable_Slide,               "Slide" },
	{ StrID_Enable_Scale,               "Scale" },
	{ StrID_Enable_Time,                "Time" },
	{ StrID_Enable_Color,               "Color" },
	{ StrID_Enable_Light,               "Light" },
	{ StrID_Enable_Blur,                "Blur" },

	{ StrID_Behaviour_Group,            "Behaviour" },
	{ StrID_Behaviour_Ease_In,          "Ease In" },
	{ StrID_Behaviour_Ease_Out,         "Ease Out" },
	{ StrID_Behaviour_Randomize_Minimum,"Randomize Minimum" },

	{ StrID_Operator_Controls_Group,    "Operator Controls" },

	{ StrID_Slide_Group,                "Slide" },
	{ StrID_Slide_Amount,               "Slide Amount" },
	{ StrID_Slide_Twitches,             "Slide Twitches [sec]" },
	{ StrID_Slide_Seed,                 "Unique Slide Seed" },
	{ StrID_Slide_Direction,            "Slide Direction" },
	{ StrID_Slide_Spread,               "Slide Spread" },
	{ StrID_Slide_Tendency,             "Slide Tendency" },
	{ StrID_Slide_Motion_Blur,          "Slide Motion Blur" },
	{ StrID_Slide_RGB_Split,            "Slide RGB Split" },

	{ StrID_Scale_Group,                "Scale" },
	{ StrID_Scale_Amount,               "Scale Amount" },
	{ StrID_Scale_Twitches,             "Scale Twitches [sec]" },
	{ StrID_Scale_Seed,                 "Unique Scale Seed" },
	{ StrID_Scale_Origin,               "Scale Origin" },
	{ StrID_Scale_Origin_Randomize,     "Scale Origin Randomize" },
	{ StrID_Scale_Motion_Blur,          "Scale Motion Blur" },
	{ StrID_Scale_Use_Lens_Blur,        "Use Lens Blur" },

	{ StrID_Time_Group,                 "Time" },
	{ StrID_Time_Amount,                "Time Amount" },
	{ StrID_Time_Twitches,              "Time Twitches [sec]" },
	{ StrID_Time_Seed,                  "Unique Time Seed" },
	{ StrID_Time_Direction,             "Time Direction" },
	{ StrID_Time_Direction_Choices,     "Forward Only|Backward Only|Both" },

	{ StrID_Color_Group,                "Color" },
	{ StrID_Color_Amount,               "Color Amount" },
	{ StrID_Color_Twitches,             "Color Twitches [sec]" },
	{ StrID_Color_Seed,                 "Unique Color Seed" },
	{ StrID_Color_Randomize,            "Color Randomize" },
	{ StrID_Color_Colorize,             "Colorize" },

	{ StrID_Light_Group,                "Light" },
	{ StrID_Light_Amount,               "Light Amount" },
	{ StrID_Light_Twitches,             "Light Twitches [sec]" },
	{ StrID_Light_Seed,                 "Unique Light Seed" },
	{ StrID_Light_Behaviour,            "Light Behaviour" },
	{ StrID_Light_Behaviour_Choices,    "Brighter|Darker|Both" },

	{ StrID_Blur_Group,                 "Blur" },
	{ StrID_Blur_Amount,                "Blur Amount" },
	{ StrID_Blur_Twitches,              "Blur Twitches [sec]" },
	{ StrID_Blur_Seed,                  "Unique Blur Seed" },
	{ StrID_Blur_Aspect,                "Blur Aspect" },
	{ StrID_Blur_Boost,                 "Blur Boost" },
	{ StrID_Blur_Holdout,               "Blur Holdout" },
	{ StrID_Blur_Holdout_Sharpness,     "Blur Holdout Sharpness" },
	{ StrID_Blur_Opacity,               "Blur Opacity" },
	{ StrID_Blur_Tint,                  "Blur Tint" },
	{ StrID_Blur_Transfer_Mode,         "Blur Transfer Mode" },
	{ StrID_Blur_Transfer_Mode_Choices, "Normal|Screen|Multiply|Overlay" },

	{ StrID_Show_Twitch_Only,           "Show Twitch Only" },
};

char *GetStringPtr(int strNum) {
	return g_strs[strNum].str;
}
