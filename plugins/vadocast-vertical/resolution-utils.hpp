/******************************************************************************
    vadocast - helpers to keep per-output scaled resolutions inside the
    allowed lists from shared/vadocast/vadocast-limits.h.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "vadocast-limits.h"

/* Picks the allowed resolution closest (by pixel count) to cx x cy.
 * Zero/invalid sizes fall back to the 1080p default of the list. */
static inline void vadocast_clamp_output_resolution(bool vertical, uint32_t &cx, uint32_t &cy)
{
	const struct vadocast_resolution *list = vertical ? vadocast_vertical_resolutions
							  : vadocast_horizontal_resolutions;
	const size_t count = vertical ? VADOCAST_RESOLUTION_COUNT(vadocast_vertical_resolutions)
				      : VADOCAST_RESOLUTION_COUNT(vadocast_horizontal_resolutions);
	const uint32_t def_cx = vertical ? VADOCAST_DEFAULT_SHORT_SIDE : VADOCAST_DEFAULT_LONG_SIDE;
	const uint32_t def_cy = vertical ? VADOCAST_DEFAULT_LONG_SIDE : VADOCAST_DEFAULT_SHORT_SIDE;

	if (cx == 0 || cy == 0) {
		cx = def_cx;
		cy = def_cy;
		return;
	}
	if (vadocast_resolution_in_list(list, count, cx, cy))
		return;

	const uint64_t area = (uint64_t)cx * (uint64_t)cy;
	size_t best = count;
	uint64_t best_diff = UINT64_MAX;
	for (size_t i = 0; i < count; i++) {
		const uint64_t a = (uint64_t)list[i].cx * (uint64_t)list[i].cy;
		const uint64_t diff = a > area ? a - area : area - a;
		if (diff < best_diff) {
			best_diff = diff;
			best = i;
		}
	}
	if (best < count) {
		cx = list[best].cx;
		cy = list[best].cy;
	} else {
		cx = def_cx;
		cy = def_cy;
	}
}

/* The vertical canvas (base == output resolution) must be one of the vertical
 * resolutions; anything else is clamped, defaulting to 1080x1920. */
static inline void vadocast_clamp_vertical_resolution(uint32_t &cx, uint32_t &cy)
{
	vadocast_clamp_output_resolution(true, cx, cy);
}
