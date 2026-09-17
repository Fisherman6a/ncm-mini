#pragma once

#include "Host.h"
#include <oleacc.h>

namespace ncmmini
{
// The path is only a hint; the transport button group is validated on every read.
PlaybackState ReadPlaybackControls(IAccessible* root, std::vector<long>& path);
PlaybackState ReadAccessiblePlayback(HWND playerWindow, DWORD processId);
}
