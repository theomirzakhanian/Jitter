#include "AEConfig.h"
#include "AE_EffectVers.h"

#ifndef AE_OS_WIN
	#include <AE_General.r>
#endif
	
resource 'PiPL' (16000) {
	{	/* array properties: 12 elements */
		/* [1] */
		Kind {
			AEEffect
		},
		/* [2] */
		Name {
			"Jitter"
		},
		/* [3] */
		Category {
			"Video Copilot"
		},
#ifdef AE_OS_WIN
    #if defined(AE_PROC_INTELx64)
		CodeWin64X86 {"EffectMain"},
    #elif defined(AE_PROC_ARM64)
		CodeWinARM64 {"EffectMain"},
    #endif
#elif defined(AE_OS_MAC)
		CodeMacIntel64 {"EffectMain"},
		CodeMacARM64 {"EffectMain"},
#endif
		/* [6] */
		AE_PiPL_Version {
			2,
			0
		},
		/* [7] */
		AE_Effect_Spec_Version {
			PF_PLUG_IN_VERSION,
			PF_PLUG_IN_SUBVERS
		},
		/* [8] */
		AE_Effect_Version {
			558592	/* 1.1.0 release — matches Twitch (PF_VERSION(1, 1, 0, PF_Stage_RELEASE, 0)) and Jitter.h */
		},
		/* [9] */
		AE_Effect_Info_Flags {
			0
		},
		/* [10] */
		AE_Effect_Global_OutFlags {
		0x02000046 // DEEP_COLOR_AWARE (1<<25) | USE_OUTPUT_EXTENT (1<<6) | WIDE_TIME_INPUT (1<<1) | NON_PARAM_VARY (1<<2)
		},
		AE_Effect_Global_OutFlags_2 {
		0x08000488 // SUPPORTS_SMART_RENDER (1<<10) | SUPPORTS_THREADED_RENDERING (1<<27) | PARAM_GROUP_START_COLLAPSED_FLAG (1<<3) | REVEALS_ZERO_ALPHA (1<<7)
		},
		/* [11] */
		AE_Effect_Match_Name {
			"ADBE Jitter"
		},
		/* [12] */
		AE_Reserved_Info {
			0
		},
		/* [13] */
		AE_Effect_Support_URL {
			"https://www.adobe.com"
		}
	}
};

resource 'PiPL' (16001) {
	{	/* array properties: 12 elements */
		/* [1] */
		Kind {
			AEEffect
		},
		/* [2] */
		Name {
			"Twitch"
		},
		/* [3] */
		Category {
			"Video Copilot"
		},
#ifdef AE_OS_WIN
    #if defined(AE_PROC_INTELx64)
		CodeWin64X86 {"EffectMain"},
    #elif defined(AE_PROC_ARM64)
		CodeWinARM64 {"EffectMain"},
    #endif
#elif defined(AE_OS_MAC)
		CodeMacIntel64 {"EffectMain"},
		CodeMacARM64 {"EffectMain"},
#endif
		/* [6] */
		AE_PiPL_Version {
			2,
			0
		},
		/* [7] */
		AE_Effect_Spec_Version {
			PF_PLUG_IN_VERSION,
			PF_PLUG_IN_SUBVERS
		},
		/* [8] */
		AE_Effect_Version {
			558592	/* 1.1.0 release — matches Twitch (PF_VERSION(1, 1, 0, PF_Stage_RELEASE, 0)) and Jitter.h */
		},
		/* [9] */
		AE_Effect_Info_Flags {
			0
		},
		/* [10] */
		AE_Effect_Global_OutFlags {
		0x02000046 // DEEP_COLOR_AWARE (1<<25) | USE_OUTPUT_EXTENT (1<<6) | WIDE_TIME_INPUT (1<<1) | NON_PARAM_VARY (1<<2)
		},
		AE_Effect_Global_OutFlags_2 {
		0x08000488 // SUPPORTS_SMART_RENDER (1<<10) | SUPPORTS_THREADED_RENDERING (1<<27) | PARAM_GROUP_START_COLLAPSED_FLAG (1<<3) | REVEALS_ZERO_ALPHA (1<<7)
		},
		/* [11] */
		AE_Effect_Match_Name {
			"Videocopilot Twitch"  // Legacy match name for project / preset compatibility.
		},
		/* [12] */
		AE_Reserved_Info {
			0
		},
		/* [13] */
		AE_Effect_Support_URL {
			"https://www.adobe.com"
		}
	}
};
