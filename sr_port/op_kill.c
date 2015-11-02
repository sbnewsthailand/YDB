/****************************************************************
 *								*
 *	Copyright 2001, 2009 Fidelity Information Services, Inc	*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#include "mdef.h"

#include "gtm_stdio.h"

#include "hashtab_mname.h"	/* needed for lv_val.h */
#include "lv_val.h"


void	op_kill(lv_val *lv)
{
	lv_kill(lv, TRUE);	/* Do kill with TP var saving active */
}
