#pragma once

class StackTrace
{
public:
	static int Capture(int max_frames, void** out_frames);
};
