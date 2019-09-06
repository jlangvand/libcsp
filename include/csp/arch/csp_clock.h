/*
Cubesat Space Protocol - A small network-layer protocol designed for Cubesats
Copyright (C) 2012 Gomspace ApS (http://www.gomspace.com)
Copyright (C) 2012 AAUSAT3 Project (http://aausat3.space.aau.dk) 

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef _CSP_CLOCK_H_
#define _CSP_CLOCK_H_

/**
   @file

   Clock API.
*/

#include <csp/csp_platform.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
   Cross-platform timestamp.
*/
typedef struct {
        //! Seconds
	uint32_t tv_sec;
        //! Nano-seconds.
	uint32_t tv_nsec;
} csp_timestamp_t;

/**
   Get time - must be implemented by the user.
*/
__attribute__((weak)) extern void clock_get_time(csp_timestamp_t * time);

/**
   Set time - must be implemented by the user.
*/
__attribute__((weak)) extern void clock_set_time(csp_timestamp_t * time);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // _CSP_CLOCK_H_
