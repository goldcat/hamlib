/**
 * \file src/rotator.c
 * \brief Ham Radio Control Libraries interface
 * \author Stephane Fillod
 * \date 2000-2001
 *
 * Hamlib interface is a frontend implementing rotator wrapper functions.
 */

/*
 *  Hamlib Interface - main file
 *  Copyright (c) 2000,2001 by Stephane Fillod and Frank Singleton
 *
 *		$Id: rotator.c,v 1.1 2001-12-27 21:46:25 fillods Exp $
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Library General Public License as
 *   published by the Free Software Foundation; either version 2 of
 *   the License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Library General Public License for more details.
 *
 *   You should have received a copy of the GNU Library General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


#include <hamlib/rotator.h>
#include <serial.h>
#include "rot_conf.h"


#define DEFAULT_SERIAL_PORT "/dev/rotator"

/*
 * Data structure to track the opened rot (by rot_open)
 */
struct opened_rot_l {
		ROT *rot;
		struct opened_rot_l *next;
};
static struct opened_rot_l *opened_rot_list = { NULL };

/*
 * track which rot is opened (with rot_open)
 * needed at least for transceive mode
 */
static int add_opened_rot(ROT *rot)
{
	struct opened_rot_l *p;
	p = (struct opened_rot_l *)malloc(sizeof(struct opened_rot_l));
	if (!p)
			return -RIG_ENOMEM;
	p->rot = rot;
	p->next = opened_rot_list;
	opened_rot_list = p;
	return RIG_OK;
}

static int remove_opened_rot(ROT *rot)
{	
	struct opened_rot_l *p,*q;
	q = NULL;

	for (p=opened_rot_list; p; p=p->next) {
			if (p->rot == rot) {
					if (q == NULL) {
							opened_rot_list = opened_rot_list->next;
					} else {
							q->next = p->next;
					}
					free(p);
					return RIG_OK;
			}
			q = p;
	}
	return -RIG_EINVAL;	/* Not found in list ! */
}

/**
 * \brief execs cfunc() on each opened rot
 * \param cfunc	The function to be executed on each rot
 * \param data	Data pointer to be passed to cfunc() 
 *
 *  Calls cfunc() function for each opened rot. 
 *  The contents of the opened rot table
 *  is processed in random order according to a function
 *  pointed to by \a cfunc, whic is called with two arguments,
 *  the first pointing to the #ROT handle, the second
 *  to a data pointer \a data.
 *  If \a data is not needed, then it can be set to NULL.
 *  The processing of the opened rot table is stopped
 *  when cfunc() returns 0.
 * \internal
 *
 * \return always RIG_OK.
 */

int foreach_opened_rot(int (*cfunc)(ROT *, rig_ptr_t), rig_ptr_t data)
{	
	struct opened_rot_l *p;

	for (p=opened_rot_list; p; p=p->next) {
			if ((*cfunc)(p->rot,data) == 0)
					return RIG_OK;
	}
	return RIG_OK;
}

/**
 * \brief allocate a new #ROT handle
 * \param rot_model	The rot model for this new handle
 *
 * Allocates a new #ROT handle and initializes the associated data 
 * for \a rot_model.
 *
 * \return a pointer to the #ROT handle otherwise NULL if memory allocation
 * failed or \a rot_model is unknown (e.g. backend autoload failed).
 *
 * \sa rot_cleanup(), rot_open()
 */

ROT *rot_init(rot_model_t rot_model)
{
		ROT *rot;
		const struct rot_caps *caps;
		struct rot_state *rs;
		int retcode;

		rot_debug(RIG_DEBUG_VERBOSE,"rot:rot_init called \n");

		rot_check_backend(rot_model);

		caps = rot_get_caps(rot_model);
		if (!caps)
				return NULL;

		/*
		 * okay, we've found it. Allocate some memory and set it to zeros,
		 * and especially the initialize the callbacks 
		 */ 
		rot = calloc(1, sizeof(ROT));
		if (rot == NULL) {
				/*
				 * FIXME: how can the caller know it's a memory shortage,
				 * 		  and not "rot not found" ?
				 */
				return NULL;
		}

		rot->caps = caps;

		/*
		 * populate the rot->state
		 * TODO: read the Preferences here! 
		 */

		rs = &rot->state;

		rs->comm_state = 0;
		rs->rotport.type.rig = caps->port_type; /* default from caps */
		strncpy(rs->rotport.pathname, DEFAULT_SERIAL_PORT, FILPATHLEN);
		rs->rotport.parm.serial.rate = caps->serial_rate_max;	/* fastest ! */
		rs->rotport.parm.serial.data_bits = caps->serial_data_bits;
		rs->rotport.parm.serial.stop_bits = caps->serial_stop_bits;
		rs->rotport.parm.serial.parity = caps->serial_parity;
		rs->rotport.parm.serial.handshake = caps->serial_handshake;
		rs->rotport.write_delay = caps->write_delay;
		rs->rotport.post_write_delay = caps->post_write_delay;

		rs->rotport.timeout = caps->timeout;
		rs->rotport.retry = caps->retry;

		rs->min_el = caps->min_el;
		rs->min_el = caps->min_el;
		rs->min_az = caps->min_az;
		rs->min_az = caps->min_az;

		rs->rotport.fd = -1;

		/* 
		 * let the backend a chance to setup his private data
		 * This must be done only once defaults are setup,
		 * so the backend init can override rot_state.
		 */
		if (caps->rot_init != NULL) {
				retcode = caps->rot_init(rot);
				if (retcode != RIG_OK) {
						rot_debug(RIG_DEBUG_VERBOSE,"rot:backend_init failed!\n");
						/* cleanup and exit */
						free(rot);
						return NULL;
				}
		}

		return rot;
}

/**
 * \brief open the communication to the rot
 * \param rot	The #ROT handle of the radio to be opened
 *
 * Opens communication to a radio which \a ROT handle has been passed
 * by argument.
 *
 * \return RIG_OK if the operation has been sucessful, otherwise 
 * a negative value if an error occured (in which case, cause is 
 * set appropriately).
 *
 * \retval RIG_EINVAL	\a rot is NULL or unconsistent.
 * \retval RIG_ENIMPL	port type communication is not implemented yet.
 *
 * \sa rot_init(), rot_close()
 */

int rot_open(ROT *rot)
{
		const struct rot_caps *caps;
		struct rot_state *rs;
		int status;
		azimuth_t az;
		elevation_t el;

		rot_debug(RIG_DEBUG_VERBOSE,"rot:rot_open called \n");

		if (!rot || !rot->caps)
				return -RIG_EINVAL;

		caps = rot->caps;
		rs = &rot->state;

		if (rs->comm_state)
				return -RIG_EINVAL;

		rs->rotport.fd = -1;

		switch(rs->rotport.type.rig) {
		case RIG_PORT_SERIAL:
				status = serial_open(&rs->rotport);
				if (status != 0)
						return status;
				break;

		case RIG_PORT_DEVICE:
				status = open(rs->rotport.pathname, O_RDWR, 0);
				if (status < 0)
						return -RIG_EIO;
				rs->rotport.fd = status;
				break;

		case RIG_PORT_NONE:
		case RIG_PORT_RPC:
				break;	/* ez :) */

		case RIG_PORT_NETWORK:	/* not implemented yet! */
				return -RIG_ENIMPL;
		default:
				return -RIG_EINVAL;
		}


		add_opened_rot(rot);

		rs->comm_state = 1;

		/* 
		 * Maybe the backend has something to initialize
		 * In case of failure, just close down and report error code.
		 */
		if (caps->rot_open != NULL) {
				status = caps->rot_open(rot);	
				if (status != RIG_OK) {
						rot_close(rot);
						return status;
				}
		}

		/*
		 * trigger state->current_az/current_el first retrieval
		 */
		rot_get_position(rot, &az, &el);

		return RIG_OK;
}

/**
 * \brief close the communication to the rot
 * \param rot	The #ROT handle of the radio to be closed
 *
 * Closes communication to a radio which \a ROT handle has been passed
 * by argument that was previously open with rot_open().
 *
 * \return RIG_OK if the operation has been sucessful, otherwise 
 * a negative value if an error occured (in which case, cause is 
 * set appropriately).
 *
 * \sa rot_cleanup(), rot_open()
 */

int rot_close(ROT *rot)
{
		const struct rot_caps *caps;
		struct rot_state *rs;

		rot_debug(RIG_DEBUG_VERBOSE,"rot:rot_close called \n");

		if (!rot || !rot->caps)
				return -RIG_EINVAL;

		caps = rot->caps;
		rs = &rot->state;

		if (!rs->comm_state)
				return -RIG_EINVAL;

		/*
		 * Let the backend say 73s to the rot.
		 * and ignore the return code.
		 */
		if (caps->rot_close)
				caps->rot_close(rot);


		if (rs->rotport.fd != -1) {
				if (!rs->rotport.stream)
						fclose(rs->rotport.stream); /* this closes also fd */
				else
					close(rs->rotport.fd);
				rs->rotport.fd = -1;
				rs->rotport.stream = NULL;
		}

		remove_opened_rot(rot);

		rs->comm_state = 0;

		return RIG_OK;
}

/**
 * \brief release a rot handle and free associated memory
 * \param rot	The #ROT handle of the radio to be closed
 *
 * Releases a rot struct which port has eventualy been closed already 
 * with rot_close().
 *
 * \return RIG_OK if the operation has been sucessful, otherwise 
 * a negative value if an error occured (in which case, cause is 
 * set appropriately).
 *
 * \sa rot_init(), rot_close()
 */

int rot_cleanup(ROT *rot)
{
		rot_debug(RIG_DEBUG_VERBOSE,"rot:rot_cleanup called \n");

		if (!rot || !rot->caps)
				return -RIG_EINVAL;

		/*
		 * check if they forgot to close the rot
		 */
		if (rot->state.comm_state)
				rot_close(rot);

		/*
		 * basically free up the priv struct 
		 */
		if (rot->caps->rot_cleanup)
				rot->caps->rot_cleanup(rot);

		free(rot);

		return RIG_OK;
}


/**
 * \brief set a rotator configuration parameter
 * \param rot	The rot handle
 * \param token	The parameter
 * \param val	The value to set the parameter to
 *
 *  Sets a configuration parameter. 
 *
 * \return RIG_OK if the operation has been sucessful, otherwise 
 * a negative value if an error occured (in which case, cause is 
 * set appropriately).
 *
 * \sa rot_get_conf()
 */
int rot_set_conf(ROT *rot, token_t token, const char *val)
{
		if (!rot || !rot->caps)
			return -RIG_EINVAL;

		if (RIG_IS_TOKEN_FRONTEND(token))
				return frontrot_set_conf(rot, token, val);

		if (rot->caps->set_conf == NULL)
			return -RIG_ENAVAIL;

		return rot->caps->set_conf(rot, token, val);
}

/**
 * \brief get the value of a configuration parameter
 * \param rot	The rot handle
 * \param token	The parameter
 * \param val	The location where to store the value of config \a token
 *
 *  Retrieves the value of a configuration paramter associated with \a token.
 *
 * \return RIG_OK if the operation has been sucessful, otherwise 
 * a negative value if an error occured (in which case, cause is 
 * set appropriately).
 *
 * \sa rot_set_conf()
 */
int rot_get_conf(ROT *rot, token_t token, char *val)
{
		if (!rot || !rot->caps || !val)
			return -RIG_EINVAL;

		if (RIG_IS_TOKEN_FRONTEND(token))
				return frontrot_get_conf(rot, token, val);

		if (rot->caps->get_conf == NULL)
			return -RIG_ENAVAIL;

		return rot->caps->get_conf(rot, token, val);
}

/**
 * \brief set the azimuth and elevation of the rotator
 * \param rot	The rot handle
 * \param azimuth	The azimuth to set to
 * \param elevation	The elevation to set to
 *
 * Sets the azimuth and elevation of the rotator.
 *
 * \return RIG_OK if the operation has been sucessful, otherwise 
 * a negative value if an error occured (in which case, cause is 
 * set appropriately).
 *
 * \sa rot_get_position()
 */

int rot_set_position (ROT *rot, azimuth_t azimuth, elevation_t elevation)
{
		const struct rot_caps *caps;

		if (!rot || !rot->caps)
			return -RIG_EINVAL;

		caps = rot->caps;

		if (caps->set_position == NULL)
			return -RIG_ENAVAIL;

		return caps->set_position(rot, azimuth, elevation);
}

/**
 * \brief get the azimuth and elevation of the rotator
 * \param rot	The rot handle
 * \param azimuth	The location where to store the current azimuth
 * \param elevation	The location where to store the current elevation
 *
 *  Retrieves the current azimuth and elevation of the rotator.
 *
 * \return RIG_OK if the operation has been sucessful, otherwise 
 * a negative value if an error occured (in which case, cause is 
 * set appropriately).
 *
 * \sa rot_set_position()
 */

int rot_get_position (ROT *rot, azimuth_t *azimuth, elevation_t *elevation)
{
		const struct rot_caps *caps;

		if (!rot || !rot->caps || !azimuth || !elevation)
			return -RIG_EINVAL;

		caps = rot->caps;

		if (caps->get_position == NULL)
			return -RIG_ENAVAIL;

		return caps->get_position(rot, azimuth, elevation);
}

/**
 * \brief get general information from the rotator
 * \param rot	The rot handle
 *
 * Retrieves some general information from the rotator.
 * This can include firmware revision, exact model name, or just nothing. 
 *
 * \return a pointer to static memory containing the ASCIIZ string 
 * if the operation has been sucessful, otherwise NULL if an error occured
 * or get_info not part of capabilities.
 */
const char* rot_get_info(ROT *rot)
{
		if (!rot || !rot->caps)
			return NULL;

		if (rot->caps->get_info == NULL)
			return NULL;

		return rot->caps->get_info(rot);
}
