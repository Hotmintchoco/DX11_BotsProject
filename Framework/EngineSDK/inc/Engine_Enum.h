#ifndef Engine_Enum_h__
#define Engine_Enum_h__

namespace Engine
{
	// WINMODE::FULL 처럼 사용해야 하고, 상수로 사용하려면 명시적 형변환을 해줘야 함.
	enum class WINMODE { FULL, WIN, END };

	
	//enum COMPONENTID { ID_DYNAMIC, ID_STATIC, ID_END };

	//enum INFO {	INFO_RIGHT, INFO_UP, INFO_LOOK, INFO_POS, INFO_END };

	//enum ROTATION { ROT_X, ROT_Y, ROT_Z, ROT_END };

	//enum TEXTUREID { TEX_NORMAL, TEX_CUBE, TEX_END };

	//enum RENDERID { RENDER_PRIORITY, RENDER_NONALPHA, RENDER_ALPHA, RENDER_UI, RENDER_END };

	//enum MOUSEKEYSTATE { DIM_LB, DIM_RB, DIM_MB, DIM_END };

	//enum MOUSEMOVESTATE {	DIMS_X, DIMS_Y, DIMS_Z, DIMS_END };

}
#endif // Engine_Enum_h__
