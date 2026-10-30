// Purpose: Implements shared color transformations for application icons.
// Approach: Implements the behavior with focused helpers built on shared xucRun interfaces and Windows APIs.
#include "ApplicationIcon.h"

#include <algorithm>
#include <vector>

namespace xucrun::application_icon
{
	HICON CreateColorized(HICON sourceIcon, Color color)
	{
		ICONINFO iconInfo = {};
		if (!sourceIcon || !GetIconInfo(sourceIcon, &iconInfo))
			return nullptr;
		if (!iconInfo.hbmColor)
		{
			DeleteObject(iconInfo.hbmMask);
			return nullptr;
		}

		BITMAP bitmap = {};
		if (GetObjectW(iconInfo.hbmColor, sizeof(bitmap), &bitmap) != sizeof(bitmap))
		{
			DeleteObject(iconInfo.hbmColor);
			DeleteObject(iconInfo.hbmMask);
			return nullptr;
		}
		BITMAPINFO bitmapInfo = {};
		bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bitmapInfo.bmiHeader.biWidth = bitmap.bmWidth;
		bitmapInfo.bmiHeader.biHeight = -bitmap.bmHeight;
		bitmapInfo.bmiHeader.biPlanes = 1;
		bitmapInfo.bmiHeader.biBitCount = 32;
		bitmapInfo.bmiHeader.biCompression = BI_RGB;
		std::vector<DWORD> pixels(
			static_cast<size_t>(bitmap.bmWidth) * static_cast<size_t>(bitmap.bmHeight));
		HDC screen = GetDC(nullptr);
		const bool read = screen && GetDIBits(screen, iconInfo.hbmColor, 0,
			static_cast<UINT>(bitmap.bmHeight), pixels.data(), &bitmapInfo,
			DIB_RGB_COLORS) != 0;
		if (read)
		{
			for (DWORD& pixel : pixels)
			{
				BYTE* channels = reinterpret_cast<BYTE*>(&pixel);
				const bool white = channels[0] >= 224 &&
					channels[1] >= 224 && channels[2] >= 224;
				const bool yellow = channels[2] >= 160 &&
					channels[1] >= 120 && channels[0] < 128;
				const bool preserveColor = white || yellow;
				const BYTE gray = static_cast<BYTE>((channels[2] * 30 +
					channels[1] * 59 + channels[0] * 11) / 100);
				if (color == Color::Grayscale)
				{
					const BYTE disabledGray = gray < 90 ? 155 : (gray < 180 ? 200 : 240);
					channels[0] = disabledGray;
					channels[1] = disabledGray;
					channels[2] = disabledGray;
				}
				else if (!preserveColor)
				{
					constexpr unsigned ClientBackgroundGray = 88;
					constexpr BYTE ApprovalBlue = 61;
					constexpr BYTE ApprovalGreen = 52;
					constexpr BYTE ApprovalRed = 112;
					channels[0] = static_cast<BYTE>(std::min<unsigned>(
						255, gray * ApprovalBlue / ClientBackgroundGray));
					channels[1] = static_cast<BYTE>(std::min<unsigned>(
						255, gray * ApprovalGreen / ClientBackgroundGray));
					channels[2] = static_cast<BYTE>(std::min<unsigned>(
						255, gray * ApprovalRed / ClientBackgroundGray));
				}
			}
			SetDIBits(screen, iconInfo.hbmColor, 0, static_cast<UINT>(bitmap.bmHeight),
				pixels.data(), &bitmapInfo, DIB_RGB_COLORS);
		}
		if (screen)
			ReleaseDC(nullptr, screen);
		HICON colorizedIcon = read ? CreateIconIndirect(&iconInfo) : nullptr;
		DeleteObject(iconInfo.hbmColor);
		DeleteObject(iconInfo.hbmMask);
		return colorizedIcon;
	}
}