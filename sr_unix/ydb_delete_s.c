/****************************************************************
 *								*
 * Copyright (c) 2017-2022 YottaDB LLC and/or its subsidiaries. *
 * All rights reserved.						*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#include "mdef.h"

#include "gtm_string.h"

#include "lv_val.h"
#include "hashtab_mname.h"
#include "callg.h"
#include "op.h"
#include "error.h"
#include "nametabtyp.h"
#include "namelook.h"
#include "stringpool.h"
#include "libyottadb_int.h"
#include "gdsroot.h"
#include "gtm_facility.h"
#include "fileinfo.h"
#include "gdsbt.h"
#include "gdsfhead.h"
#include "mvalconv.h"
#include "outofband.h"

GBLREF	volatile int4	outofband;

/* Routine to delete/kill an lvn/gvn.
 *
 * Parameters:
 *   varname	    - Gives name of local or global variable
 *   subs_used	    - Count of subscripts (if any else 0) in input node
 *   subsarray	    - an array of "subs_used" subscripts in input node (not looked at if "subs_used" is 0)
 *   delete_method  - Is one of the below choices
 *			YDB_DEL_TREE (kills the subtree underneath i.e. KILL command in M)
 *			YDB_DEL_NODE (kills only the node and not the subtree underneath i.e. ZKILL/ZWITHDRAW command in M)
 *
 * Note unlike "ydb_set_s", none of the input varname or subscripts need rebuffering in this routine
 * as they are not ever being used to create a new node or are otherwise kept for any reason by the
 * YottaDB runtime routines.
 */
int ydb_delete_s(const ydb_buffer_t *varname, int subs_used, const ydb_buffer_t *subsarray, int deltype)
{
	boolean_t	error_encountered;
	gparam_list	plist;
	ht_ent_mname	*tabent;
	int		delete_svn_index;
	lv_val		*lvvalp, *src_lv;
	mname_entry	var_mname;
	mval		gvname, plist_mvals[YDB_MAX_SUBS + 1];
	ydb_var_types	delete_type;
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	VERIFY_NON_THREADED_API;	/* clears a global variable "caller_func_is_stapi" set by SimpleThreadAPI caller
					 * so needs to be first invocation after SETUP_THREADGBL_ACCESS to avoid any error
					 * scenarios from not resetting this global variable even though this function returns.
					 */
	/* Verify entry conditions, make sure YDB CI environment is up etc. */
	LIBYOTTADB_INIT(LYDB_RTN_DELETE, (int));	/* Note: macro could return from this function in case of errors */
	assert(0 == TREF(sapi_mstrs_for_gc_indx));	/* previously unused entries should have been cleared by that
							 * corresponding ydb_*_s() call.
							 */
	ESTABLISH_NORET(ydb_simpleapi_ch, error_encountered);
	if (error_encountered)
	{
		assert(0 == TREF(sapi_mstrs_for_gc_indx));	/* Should have been cleared by "ydb_simpleapi_ch" */
		REVERT;
		return ((ERR_TPRETRY == SIGNAL) ? YDB_TP_RESTART : -(TREF(ydb_error_code)));
	}
	/* Check if an outofband action that might care about has popped up */
	if (outofband)
		outofband_action(FALSE);
	/* We should have a variable name - check it out and determine type */
	VALIDATE_VARNAME(varname, subs_used, FALSE, LYDB_RTN_DELETE, -1, delete_type, delete_svn_index);
	/* Separate actions depending on type of delete (KILL) being done */
	switch(delete_type)
	{
		case LYDB_VARREF_LOCAL:
			/* Find status of the given local variable. Locate base var lv_val in curr_symval */
			FIND_BASE_VAR_NOUPD(varname, &var_mname, tabent, lvvalp);
			if (NULL == lvvalp)
			{	/* Base local variable does not exist. Nothing to kill. Return right away. */
				break;
			}
			if (0 == subs_used)
				/* If no subscripts, this is the node we are interested in */
				src_lv = lvvalp;
			else
			{	/* We have some subscripts - load the varname lv_val and the subscripts into our array for callg
				 * so we can drive "op_srchindx" to locate the mval associated with those subscripts that need to
				 * be set.
				 */
				plist.arg[0] = lvvalp;				/* First arg is lv_val of the base var */
				/* Setup plist (which would point to plist_mvals[] array) for callg invocation of op_getindx */
				COPY_PARMS_TO_CALLG_BUFFER(subs_used, subsarray, plist, plist_mvals, FALSE, 1,
							LYDBRTNNAME(LYDB_RTN_DELETE));
				src_lv = (lv_val *)callg((callgfnptr)op_srchindx, &plist);	/* Locate node */
			}
			switch(deltype)
			{	/* Drive the appropriate deletion routine depending on whether we are deleting just a node or the
				 * node plus descendants (tree).
				 */
				case YDB_DEL_TREE:
					op_kill(src_lv);
					break;
				case YDB_DEL_NODE:
					op_lvzwithdraw(src_lv);
					break;
				default:
					rts_error_csa(CSA_ARG(NULL) VARLSTCNT(1) ERR_UNIMPLOP);
			}
			break;
		case LYDB_VARREF_GLOBAL:
			/* Find dstatus of the given global variable value. We do this by:
			 *   1. Drive "op_gvname" with the global name and all subscripts to setup the key we need to access
			 *      the global node.
			 *   2. Drive "op_gvdata" to fetch the information (status, descendants) we are interested in.
			 */
			gvname.mvtype = MV_STR;
			gvname.str.addr = varname->buf_addr + 1;	/* Point past '^' to var name */
			gvname.str.len = varname->len_used - 1;
			/* Setup plist (which would point to plist_mvals[] array) for callg invocation of op_gvname */
			if (0 < subs_used)
			{
				plist.arg[0] = &gvname;
				COPY_PARMS_TO_CALLG_BUFFER(subs_used, subsarray, plist, plist_mvals, FALSE, 1,
							LYDBRTNNAME(LYDB_RTN_DELETE));
				callg((callgfnptr)op_gvname, &plist);	/* Drive "op_gvname" to  key */
			} else
				op_gvname(1, &gvname);			/* Single parm call to get next global */
			switch(deltype)
			{	/* Drive the appropriate deletion routine depending on whether we are deleting just a node or the
				 * node plus descendants (tree).
				 */
				case YDB_DEL_TREE:
					op_gvkill();
					break;
				case YDB_DEL_NODE:
					op_gvzwithdraw();
					break;
				default:
					rts_error_csa(CSA_ARG(NULL) VARLSTCNT(1) ERR_UNIMPLOP);
			}
			break;
		case LYDB_VARREF_ISV:
			/* The VALIDATE_VARNAME macro call done above should have already issued an error in this case */
		default:
			assertpro(FALSE);
			break;
	}
	assert(0 == TREF(sapi_mstrs_for_gc_indx));	/* the counter should have never become non-zero in this function */
	LIBYOTTADB_DONE;
	REVERT;
	return YDB_OK;
}

