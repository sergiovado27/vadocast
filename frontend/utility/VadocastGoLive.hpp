/******************************************************************************
    vadocast - going live is owned by the "Transmitir" panel of the
    vadocast-multistream module (horizontal, vertical or both, each
    destination with its own stream key). The frontend reaches it through
    global proc handlers so it keeps working if the module is missing.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#pragma once

#include <obs.h>

/* Calls a vadocast_golive_* proc handler. Returns false when the module is not
 * loaded, so callers fall back to the regular OBS behaviour. */
static inline bool VadocastGoLiveCall(const char *name, bool *active = nullptr)
{
	calldata_t cd;
	calldata_init(&cd);
	const bool handled = proc_handler_call(obs_get_proc_handler(), name, &cd);
	if (handled && active) {
		*active = calldata_bool(&cd, "active");
	}
	calldata_free(&cd);
	return handled;
}
