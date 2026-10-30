// Purpose: Defines shared color transformations for application icons.
// Approach: Exposes the declarations, constants, and types consumed by the components that provide this behavior.
#pragma once

#include <Windows.h>

namespace xucrun::application_icon
{
	enum class Color
	{
		Grayscale,
		Red
	};

	HICON CreateColorized(HICON sourceIcon, Color color);
}