/****************************************************************
 *								*
 * Copyright (c) 2005-2020 Fidelity National Information	*
 * Services, Inc. and/or its subsidiaries. All rights reserved.	*
 *								*
 * Copyright (c) 2019-2022 YottaDB LLC and/or its subsidiaries.	*
 * All rights reserved.						*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

/* Based on generic_signal_handler without the db engine references like have_crit().
 *
 * If we are nesting our handlers in an improper way, this routine will
 * not return but will immediately invoke core/termination processing.
 *
 * Returns if some condition makes it inadvisable to exit now else invokes the system EXIT() system call.
 */

#include "mdef.h"

#include "gtm_string.h"
#include "gtm_unistd.h"
#include "gtm_stdlib.h"		/* for EXIT() */
#include "gtm_inet.h"
#include "gtm_stdio.h"
#include "gtm_signal.h"

#include "error.h"
#include "gtmsiginfo.h"
#include "gtmimagename.h"
#include "send_msg.h"
#include "gtmmsg.h"
#include "gdsroot.h"
#include "v15_gdsroot.h"
#include "gtm_facility.h"
#include "fileinfo.h"
#include "gdsbt.h"
#include "v15_gdsbt.h"
#include "gdsblk.h"
#include "gdsfhead.h"
#include "v15_gdsfhead.h"
#include "gdsblkops.h"
#include "gtmio.h"
#include "have_crit.h"
#include "dbcertify.h"
#include "forced_exit_err_display.h"
#include "sig_init.h"

/* These fields are defined as globals not because they are used globally but
 * so they will be easily retrievable even in 'pro' cores.
 */
GBLREF	int4			forced_exit_err;
GBLREF	int4			exi_condition;
GBLREF	enum gtmImageTypes	image_type;
GBLREF	boolean_t		dont_want_core;
GBLREF	boolean_t		created_core;
GBLREF	boolean_t		need_core;
GBLREF	uint4			process_id;
GBLREF	volatile int4		exit_state;
GBLREF	volatile unsigned int	core_in_progress;
GBLREF	gtmsiginfo_t		signal_info;
GBLREF	boolean_t		exit_handler_active;
GBLREF	phase_static_area	*psa_gbl;

LITREF	gtmImageName		gtmImageNames[];

error_def(ERR_FORCEDHALT);
error_def(ERR_KILLBYSIG);
error_def(ERR_KILLBYSIGUINFO);
error_def(ERR_KILLBYSIGSINFO1);
error_def(ERR_KILLBYSIGSINFO2);
error_def(ERR_KILLBYSIGSINFO3);
error_def(ERR_KRNLKILL);

void dbcertify_signal_handler(int sig, siginfo_t *info, void *context)
{
	boolean_t		dbc_critical, exit_now;
	void			(*signal_routine)();

	FORWARD_SIG_TO_MAIN_THREAD_IF_NEEDED(sig_hndlr_dbcertify_signal_handler, sig, IS_EXI_SIGNAL_FALSE, info, context);
	/* Save parameter value in global variables for easy access in core */
	dont_want_core = FALSE;		/* (re)set in case we recurse */
	created_core = FALSE;		/* we can deal with a second core if needbe */
	exi_condition = sig;
	/* Check if we are fielding nested immediate shutdown signals */
	if (EXIT_IMMED <= exit_state)
	{
		switch(sig)
		{	/* If we are dealing with one of these three dangerous signals which we have
			 * already hit while attempting to shutdown once, die with core now.
			 */
			case SIGSEGV:
			case SIGBUS:
			case SIGILL:
				if (core_in_progress)
				{
					if (exit_handler_active)
					{
						UNDERSCORE_EXIT(sig);
					} else
						EXIT(sig);
				}
				++core_in_progress;
				DUMP_CORE;
				assertpro(FALSE);
			default:
				;
		}
	}
	dbc_critical = ((NULL != psa_gbl) && psa_gbl->dbc_critical);
	switch(sig)
	{
		case SIGINT:
		case SIGTERM:
			forced_exit_err = ERR_FORCEDHALT;
			/* If nothing pending AND we have crit or already in exit processing, wait to
			 * invoke shutdown.
			 */
			if ((EXIT_PENDING_TOLERANT >= exit_state) && (dbc_critical || exit_handler_active))
			{
				SET_FORCED_EXIT_STATE(sig);
				exit_state++;		/* Make exit pending, may still be tolerant though */
				return;
			}
			exit_state = EXIT_IMMED;
			forced_exit_err_display();
			dont_want_core = TRUE;
			break;
		case SIGQUIT:	/* Handle SIGQUIT specially which we ALWAYS want to defer if possible as it is always sent */
			dont_want_core = TRUE;
			extract_signal_info(sig, info, context, &signal_info);
			switch(signal_info.infotype)
			{
				case GTMSIGINFO_NONE:
					forced_exit_err = ERR_KILLBYSIG;
					break;
				case GTMSIGINFO_USER:
					forced_exit_err = ERR_KILLBYSIGUINFO;
					break;
				case GTMSIGINFO_ILOC + GTMSIGINFO_BADR:
					forced_exit_err = ERR_KILLBYSIGSINFO1;
					break;
				case GTMSIGINFO_ILOC:
					forced_exit_err = ERR_KILLBYSIGSINFO2;
					break;
				case GTMSIGINFO_BADR:
					forced_exit_err = ERR_KILLBYSIGSINFO3;
					break;
				default:
					exit_state = EXIT_IMMED;
					assertpro(FALSE);
			}
			/* If nothing pending AND we have crit or already in exit processing, wait to invoke shutdown */
			if ((EXIT_PENDING_TOLERANT >= exit_state) && (dbc_critical || exit_handler_active))
			{
				SET_FORCED_EXIT_STATE(sig);
				exit_state++;		/* Make exit pending, may still be tolerant though */
				return;
			}
			exit_state = EXIT_IMMED;
			switch(signal_info.infotype)
			{
				case GTMSIGINFO_NONE:
					send_msg_csa(CSA_ARG(NULL) VARLSTCNT(6) ERR_KILLBYSIG, 4,
						GTMIMAGENAMETXT(image_type), process_id, sig);
					gtm_putmsg_csa(CSA_ARG(NULL) VARLSTCNT(6) ERR_KILLBYSIG, 4,
						GTMIMAGENAMETXT(image_type), process_id, sig);
					break;
				case GTMSIGINFO_USER:
					send_msg_csa(CSA_ARG(NULL) VARLSTCNT(8) ERR_KILLBYSIGUINFO, 6, GTMIMAGENAMETXT(image_type),
							process_id, sig, signal_info.send_pid, signal_info.send_uid);
					gtm_putmsg_csa(CSA_ARG(NULL) VARLSTCNT(8) ERR_KILLBYSIGUINFO, 6,
						GTMIMAGENAMETXT(image_type), process_id, sig,
						signal_info.send_pid, signal_info.send_uid);
					break;
				case GTMSIGINFO_ILOC + GTMSIGINFO_BADR:
					send_msg_csa(CSA_ARG(NULL) VARLSTCNT(8) ERR_KILLBYSIGSINFO1, 6, GTMIMAGENAMETXT(image_type),
							process_id, sig, signal_info.int_iadr, signal_info.bad_vadr);
					gtm_putmsg_csa(CSA_ARG(NULL) VARLSTCNT(8) ERR_KILLBYSIGSINFO1, 6,
						GTMIMAGENAMETXT(image_type), process_id, sig,
						signal_info.int_iadr, signal_info.bad_vadr);
					break;
				case GTMSIGINFO_ILOC:
					send_msg_csa(CSA_ARG(NULL) VARLSTCNT(7) ERR_KILLBYSIGSINFO2, 5, GTMIMAGENAMETXT(image_type),
							process_id, sig, signal_info.int_iadr);
					gtm_putmsg_csa(CSA_ARG(NULL) VARLSTCNT(7) ERR_KILLBYSIGSINFO2, 5,
						GTMIMAGENAMETXT(image_type), process_id, sig, signal_info.int_iadr);
					break;
				case GTMSIGINFO_BADR:
					send_msg_csa(CSA_ARG(NULL) VARLSTCNT(7) ERR_KILLBYSIGSINFO3, 5, GTMIMAGENAMETXT(image_type),
							process_id, sig, signal_info.bad_vadr);
					gtm_putmsg_csa(CSA_ARG(NULL) VARLSTCNT(7) ERR_KILLBYSIGSINFO3, 5,
						GTMIMAGENAMETXT(image_type), process_id, sig, signal_info.bad_vadr);
					break;
			}
			break;
#ifdef _AIX
		case SIGDANGER:
			forced_exit_err = ERR_KRNLKILL;
			/* If nothing pending AND we have crit or already in exit processing, wait to invoke shutdown */
			if ((EXIT_PENDING_TOLERANT >= exit_state) && (dbc_critical || exit_handler_active))
			{
				SET_FORCED_EXIT_STATE(sig);
				exit_state++;		/* Make exit pending, may still be tolerant though */
				return;
			}
			exit_state = EXIT_IMMED;
			forced_exit_err_display();
			dont_want_core = TRUE;
			break;
#endif
		default:
			extract_signal_info(sig, info, context, &signal_info);
			switch(signal_info.infotype)
			{
				case GTMSIGINFO_NONE:
					exit_state = EXIT_IMMED;
					send_msg_csa(CSA_ARG(NULL) VARLSTCNT(6) ERR_KILLBYSIG, 4,
						GTMIMAGENAMETXT(image_type), process_id, sig);
					gtm_putmsg_csa(CSA_ARG(NULL) VARLSTCNT(6) ERR_KILLBYSIG, 4,
						GTMIMAGENAMETXT(image_type), process_id, sig);
					break;
				case GTMSIGINFO_USER:
					/* This signal was SENT to us so it can wait until we are out of crit to cause an exit */
					forced_exit_err = ERR_KILLBYSIGUINFO;
					/* If nothing pending AND we have crit or already exiting, wait to invoke shutdown */
					if ((EXIT_PENDING_TOLERANT >= exit_state) && (dbc_critical || exit_handler_active))
					{
						SET_FORCED_EXIT_STATE(sig);
						exit_state++;		/* Make exit pending, may still be tolerant though */
						need_core = TRUE;
						gtm_fork_n_core();	/* Generate "virgin" core while we can */
						return;
					}
					exit_state = EXIT_IMMED;
					send_msg_csa(CSA_ARG(NULL) VARLSTCNT(8) ERR_KILLBYSIGUINFO, 6, GTMIMAGENAMETXT(image_type),
							process_id, sig, signal_info.send_pid, signal_info.send_uid);
					gtm_putmsg_csa(CSA_ARG(NULL) VARLSTCNT(8) ERR_KILLBYSIGUINFO, 6,
						GTMIMAGENAMETXT(image_type), process_id, sig,
						signal_info.send_pid, signal_info.send_uid);
					break;
				case GTMSIGINFO_ILOC + GTMSIGINFO_BADR:
					exit_state = EXIT_IMMED;
					send_msg_csa(CSA_ARG(NULL) VARLSTCNT(8) ERR_KILLBYSIGSINFO1, 6, GTMIMAGENAMETXT(image_type),
							process_id, sig, signal_info.int_iadr, signal_info.bad_vadr);
					gtm_putmsg_csa(CSA_ARG(NULL) VARLSTCNT(8) ERR_KILLBYSIGSINFO1, 6,
						GTMIMAGENAMETXT(image_type), process_id, sig,
						signal_info.int_iadr, signal_info.bad_vadr);
					break;
				case GTMSIGINFO_ILOC:
					exit_state = EXIT_IMMED;
					send_msg_csa(CSA_ARG(NULL) VARLSTCNT(7) ERR_KILLBYSIGSINFO2, 5, GTMIMAGENAMETXT(image_type),
							process_id, sig, signal_info.int_iadr);
					gtm_putmsg_csa(CSA_ARG(NULL) VARLSTCNT(7) ERR_KILLBYSIGSINFO2, 5,
						GTMIMAGENAMETXT(image_type), process_id, sig, signal_info.int_iadr);
					break;
				case GTMSIGINFO_BADR:
					exit_state = EXIT_IMMED;
					send_msg_csa(CSA_ARG(NULL) VARLSTCNT(7) ERR_KILLBYSIGSINFO3, 5, GTMIMAGENAMETXT(image_type),
							process_id, sig, signal_info.bad_vadr);
					gtm_putmsg_csa(CSA_ARG(NULL) VARLSTCNT(7) ERR_KILLBYSIGSINFO3, 5,
						GTMIMAGENAMETXT(image_type), process_id, sig, signal_info.bad_vadr);
					break;
				default:
					exit_state = EXIT_IMMED;
					assertpro(FALSE);
			}
			if (0 != signal_info.sig_err)
			{
				send_msg_csa(CSA_ARG(NULL) VARLSTCNT(1) signal_info.sig_err);
				gtm_putmsg_csa(CSA_ARG(NULL) VARLSTCNT(1) signal_info.sig_err);
			}
			break;
	} /* switch (sig) */
	FFLUSH(stdout);
	if (!dont_want_core)
	{
		need_core = TRUE;
		gtm_fork_n_core();
	}
	/* As on VMS, a mupip stop does not drive the condition handlers unless we are in crit */
	if ((dbc_critical || (SIGTERM != exi_condition)) && CHANDLER_EXISTS)
		DRIVECH(exi_condition);

	assert((EXIT_IMMED <= exit_state) || !exit_handler_active);
	EXIT(-exi_condition);
}
