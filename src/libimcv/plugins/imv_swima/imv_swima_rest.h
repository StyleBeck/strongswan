/*
 * Copyright (C) 2017 Andreas Steffen
 * HSR Hochschule fuer Technik Rapperswil
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <http://www.fsf.org/copyleft/gpl.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

/**
 * @defgroup imv_swima imv_swima
 * @ingroup libimcv_plugins
 *
 * @defgroup imv_swima_rest_t imv_swima_rest
 * @{ @ingroup imv_swima
 */

#ifndef IMV_SWIMA_REST_H_
#define IMV_SWIMA_REST_H_

#include <library.h>

#include <json.h>

typedef struct imv_swima_rest_t imv_swima_rest_t;

/**
 * Public REST interface
 */
struct imv_swima_rest_t {

	/**
	 * Post a HTTP request including a JSON object
	 *
	 * @param jreq		JSON object in HTTP request
	 * @param jresp		JSON object in HTTP response if NEED_MORE
	 * @return			Status (SUCCESS, NEED_MORE or FAILED)
	 */
	status_t (*post)(imv_swima_rest_t *this, char *command, json_object *jreq,
					 json_object **jresp);

	/**
	 * Destroy imv_swima_rest_t object
	 */
	void (*destroy)(imv_swima_rest_t *this);

};

/**
 * Create an imv_swima_rest_t instance
 *
 * @param uri			REST URI (http://username:password@hostname[:port]/api/)
 * @param timeout		Timeout of the REST connection
 */
imv_swima_rest_t* imv_swima_rest_create(char *uri, u_int timeout);

#endif /** IMV_SWIMA_REST_H_ @}*/
