/****************************************************************
 *								*
 * Copyright 2001, 2006 Fidelity Information Services, Inc	*
 *								*
 * Copyright (c) 2019 YottaDB LLC and/or its subsidiaries.	*
 * All rights reserved.						*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#include "mdef.h"
#include "io.h"
#include "iousdef.h"

GBLREF io_pair		io_curr_device;

int ious_rdone(mint *v, uint8 t)
{
	*v = -1;
	return TRUE;
}
