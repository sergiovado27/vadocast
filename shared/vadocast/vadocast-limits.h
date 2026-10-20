/******************************************************************************
    vadocast - resolution limits shared by the main (horizontal) canvas and
    the integrated vertical canvas.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Supported resolutions: 720p and 1080p, in horizontal (16:9) and
 * vertical (9:16) orientation.
 *
 * 1440p (2K) is reserved for the future. Configure with
 * -DVADOCAST_ENABLE_1440P=ON to turn it on; nothing else needs to change.
 */

struct vadocast_resolution {
	uint32_t cx;
	uint32_t cy;
};

static const struct vadocast_resolution vadocast_horizontal_resolutions[] = {
	{1280, 720},
	{1920, 1080},
#ifdef VADOCAST_ENABLE_1440P
	{2560, 1440},
#endif
};

static const struct vadocast_resolution vadocast_vertical_resolutions[] = {
	{720, 1280},
	{1080, 1920},
#ifdef VADOCAST_ENABLE_1440P
	{1440, 2560},
#endif
};

#define VADOCAST_RESOLUTION_COUNT(list) (sizeof(list) / sizeof((list)[0]))

/* Default resolution for a fresh profile: 1080p. */
#define VADOCAST_DEFAULT_LONG_SIDE 1920
#define VADOCAST_DEFAULT_SHORT_SIDE 1080

static inline bool vadocast_resolution_in_list(const struct vadocast_resolution *list, size_t count, uint32_t cx,
					       uint32_t cy)
{
	for (size_t i = 0; i < count; i++) {
		if (list[i].cx == cx && list[i].cy == cy)
			return true;
	}
	return false;
}

static inline bool vadocast_horizontal_allowed(uint32_t cx, uint32_t cy)
{
	return vadocast_resolution_in_list(vadocast_horizontal_resolutions,
					   VADOCAST_RESOLUTION_COUNT(vadocast_horizontal_resolutions), cx, cy);
}

static inline bool vadocast_vertical_allowed(uint32_t cx, uint32_t cy)
{
	return vadocast_resolution_in_list(vadocast_vertical_resolutions,
					   VADOCAST_RESOLUTION_COUNT(vadocast_vertical_resolutions), cx, cy);
}
