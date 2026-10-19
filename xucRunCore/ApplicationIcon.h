// Purpose: Defines shared color transformations for application icons.
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