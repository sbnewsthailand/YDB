/****************************************************************
 *								*
 * Copyright (c) 2012-2020 Fidelity National Information	*
 * Services, Inc. and/or its subsidiaries. All rights reserved.	*
 *								*
 * Copyright (c) 2018-2020 YottaDB LLC and/or its subsidiaries.	*
 * All rights reserved.						*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/
#ifndef ANTICIPATORY_FREEZE_H
#define ANTICIPATORY_FREEZE_H

#include "gtm_time.h"			/* needed for GET_CUR_TIME */
#include "gdsroot.h"
#include "gdsbt.h"
#include "gdsblk.h"
#include "gdsfhead.h"
#include "repl_msg.h"			/* needed for gtmsource.h */
#include "gtmsource.h"			/* needed for jnlpool_addrs typedef */
#include "sleep_cnt.h"			/* needed for SLEEP_INSTFREEZEWAIT macro */
#include "wait_for_disk_space.h"	/* needed by DB_LSEEKWRITE macro for prototype */
#include "gtmimagename.h"		/* needed for IS_GTM_IMAGE */
#include "jnl.h"			/* needed for REPL_ALLOWED */
#include "forced_exit_err_display.h"
#include "sig_init.h"
#include "sighnd_debug.h"

boolean_t		is_anticipatory_freeze_needed(sgmnt_addrs *csa, int msg_id);
void			set_anticipatory_freeze(sgmnt_addrs *csa, int msg_id);
boolean_t		init_anticipatory_freeze_errors(void);

/* Define function pointers to certain functions to avoid executables like gtmsecshr from unnecessarily
 * linking with these functions (which causes the database/replication stuff to be pulled in).
 */
typedef boolean_t	(*is_anticipatory_freeze_needed_t)(sgmnt_addrs *csa, int msgid);
typedef void		(*set_anticipatory_freeze_t)(sgmnt_addrs *csa, int msg_id);

GBLREF	is_anticipatory_freeze_needed_t		is_anticipatory_freeze_needed_fnptr;
GBLREF	set_anticipatory_freeze_t		set_anticipatory_freeze_fnptr;
GBLREF	int					pool_init;
GBLREF	boolean_t				mupip_jnl_recover;
#ifdef DEBUG
GBLREF	uint4					lseekwrite_target;
#endif

error_def(ERR_DSKNOSPCAVAIL);
error_def(ERR_MUINSTFROZEN);
error_def(ERR_MUINSTUNFROZEN);
error_def(ERR_MUNOACTION);
error_def(ERR_REPLINSTFREEZECOMMENT);
error_def(ERR_REPLINSTFROZEN);
error_def(ERR_REPLINSTUNFROZEN);
error_def(ERR_TEXT);

#define ENABLE_FREEZE_ON_ERROR											\
{														\
	if (!IS_GTMSECSHR_IMAGE)					\
	{	/* Set anticipatory freeze function pointers to be used later (in send_msg and rts_error) */	\
		is_anticipatory_freeze_needed_fnptr = &is_anticipatory_freeze_needed;				\
		set_anticipatory_freeze_fnptr = &set_anticipatory_freeze;					\
	}													\
}

#define CHECK_IF_FREEZE_ON_ERROR_NEEDED(CSA, MSG_ID, FREEZE_NEEDED, FREEZE_MSG_ID, LCL_JNLPOOL)				\
{															\
	GBLREF	jnlpool_addrs_ptr_t	jnlpool;									\
															\
	if (!FREEZE_NEEDED && CSA && CUSTOM_ERRORS_LOADED_CSA(CSA, LCL_JNLPOOL)						\
		&& (NULL != is_anticipatory_freeze_needed_fnptr))							\
	{	/* NOT gtmsecshr */											\
		if (IS_REPL_INST_UNFROZEN_JPL(LCL_JNLPOOL)								\
			&& (*is_anticipatory_freeze_needed_fnptr)((sgmnt_addrs *)CSA, MSG_ID))				\
		{													\
			FREEZE_NEEDED = TRUE;										\
			FREEZE_MSG_ID = MSG_ID;										\
		}													\
	}														\
}

#define FREEZE_INSTANCE_IF_NEEDED(CSA, FREEZE_NEEDED, FREEZE_MSG_ID, LCL_JNLPOOL)						\
{																\
	if (FREEZE_NEEDED)													\
	{															\
		assert(NULL != set_anticipatory_freeze_fnptr);									\
		(*set_anticipatory_freeze_fnptr)((sgmnt_addrs *)CSA, FREEZE_MSG_ID);						\
		send_msg_csa(CSA_ARG(NULL) VARLSTCNT(3) ERR_REPLINSTFROZEN, 1,							\
				LCL_JNLPOOL->repl_inst_filehdr->inst_info.this_instname);					\
		send_msg_csa(CSA_ARG(NULL) VARLSTCNT(3) ERR_REPLINSTFREEZECOMMENT, 1,						\
				LCL_JNLPOOL->jnlpool_ctl->freeze_comment);							\
	}															\
}

#define CLEAR_ANTICIPATORY_FREEZE(FREEZE_CLEARED)									\
{															\
	GBLREF	jnlpool_addrs_ptr_t	jnlpool;									\
															\
	if (IS_REPL_INST_FROZEN)											\
	{														\
		jnlpool->jnlpool_ctl->freeze = 0;									\
		FREEZE_CLEARED = TRUE;											\
	} else														\
		FREEZE_CLEARED = FALSE;											\
}

#define REPORT_INSTANCE_UNFROZEN(FREEZE_CLEARED)										\
{																\
	GBLREF	jnlpool_addrs_ptr_t	jnlpool;										\
																\
	if (FREEZE_CLEARED)													\
		send_msg_csa(CSA_ARG(NULL) VARLSTCNT(3) ERR_REPLINSTUNFROZEN, 1,						\
				jnlpool->repl_inst_filehdr->inst_info.this_instname);						\
}

#define AFREEZE_MASK				0x01
/* Only use CUSTOM_ERRORS_AVAILABLE if you are specifically interested in whether the custom errors variable is set,
 * 		or if you know the journal pool isn't open (yet).
 * 	Otherwise, use INST_FREEZE_ON_ERROR_POLICY.
 * Only use CUSTOM_ERRORS_LOADED if you are in the code path towards checking a custom error.
 * 	Otherwise, use INST_FREEZE_ON_ERROR_POLICY.
 * Use INST_FREEZE_ON_ERROR_POLICY to select alternative journal pool attach/detach behavior.
 */
#define CUSTOM_ERRORS_AVAILABLE			(0 != (TREF(ydb_custom_errors)).len)
#define CUSTOM_ERRORS_LOADED			((NULL != jnlpool) && (NULL != jnlpool->jnlpool_ctl)				\
							&& jnlpool->jnlpool_ctl->instfreeze_environ_inited)
#define CUSTOM_ERRORS_LOADED_CSA(CSA, LCL_JNLPOOL)								\
(														\
					(LCL_JNLPOOL = JNLPOOL_FROM((sgmnt_addrs *)CSA))				\
						&& (NULL != LCL_JNLPOOL->jnlpool_ctl)				\
						&& LCL_JNLPOOL->jnlpool_ctl->instfreeze_environ_inited		\
)
#define INST_FREEZE_ON_ERROR_POLICY	(CUSTOM_ERRORS_AVAILABLE || CUSTOM_ERRORS_LOADED)
#define INST_FREEZE_ON_ERROR_POLICY_CSA(CSA, LCL_JNLPOOL)	(CUSTOM_ERRORS_AVAILABLE					\
									|| CUSTOM_ERRORS_LOADED_CSA(CSA, LCL_JNLPOOL))

/* INSTANCE_FREEZE_HONORED determines whether operations on a particular database can trigger an instance freeze.
 * INST_FREEZE_ON_ERROR_ENABLED() determines whether operations on a particular database can trigger an instance freeze in the
 * 	current operating environment.
 * INST_FREEZE_ON_MSG_ENABLED() determines whether the given message should trigger an instance freeze when associated with
 * 	the specified database.
 * INST_FREEZE_ON_NOSPC_ENABLED() determines whether an out-of-space condition associated with the specified database should
 * 	trigger an instance freeze.
 * Note that it is possible for these macros to be invoked while in "gvcst_cre_autoDB" in which case CSA->nl would be NULL
 *	hence the check for NULL before trying to access onln_rlbk_pid.
 *	These macros set LCL_JNLPOOL to the assocaited jnlpool if TRUE otherwise to NULL
 */
#define INSTANCE_FREEZE_HONORED(CSA, LCL_JNLPOOL) instance_freeze_honored(CSA, &(LCL_JNLPOOL))

#define INST_FREEZE_ON_ERROR_ENABLED(CSA, LCL_JNLPOOL)	(INSTANCE_FREEZE_HONORED(CSA, LCL_JNLPOOL)				\
							&& CUSTOM_ERRORS_LOADED_CSA(CSA, LCL_JNLPOOL)				\
							&& (((sgmnt_addrs *)CSA)->hdr->freeze_on_fail))
#define INST_FREEZE_ON_MSG_ENABLED(CSA, MSG, LCL_JNLPOOL)	(INST_FREEZE_ON_ERROR_ENABLED(CSA, LCL_JNLPOOL)			\
							&& (NULL != is_anticipatory_freeze_needed_fnptr)			\
							&& (*is_anticipatory_freeze_needed_fnptr)(CSA, MSG))
#define INST_FREEZE_ON_NOSPC_ENABLED(CSA, LCL_JNLPOOL)	INST_FREEZE_ON_MSG_ENABLED(CSA, ERR_DSKNOSPCAVAIL, LCL_JNLPOOL)

/* IS_REPL_INST_FROZEN is TRUE if we know that the instance is frozen.
 * IS_REPL_INST_UNFROZEN is TRUE if we know that the instance is not frozen.
 */
#define IS_REPL_INST_FROZEN			IS_REPL_INST_FROZEN_JPL(jnlpool)
#define IS_REPL_INST_UNFROZEN			IS_REPL_INST_UNFROZEN_JPL(jnlpool)
#define IS_REPL_INST_FROZEN_JPL(JNLPOOL)	((NULL != JNLPOOL) && (NULL != JNLPOOL->jnlpool_ctl)				\
							&& JNLPOOL->jnlpool_ctl->freeze)
#define IS_REPL_INST_UNFROZEN_JPL(JNLPOOL)	((NULL != JNLPOOL) && (NULL != JNLPOOL->jnlpool_ctl)				\
							&& !JNLPOOL->jnlpool_ctl->freeze)

#define INST_FROZEN_COMMENT			"PID %d encountered %s; Instance frozen"

#define MSGID_TO_ERRMSG(MSG_ID, ERRMSG)				\
{								\
	const err_ctl		*ctl;				\
								\
	ctl = err_check(MSG_ID);				\
	assert(NULL != ctl);					\
	GET_MSG_INFO(MSG_ID, ctl, ERRMSG);			\
}

#define GENERATE_INST_FROZEN_COMMENT(BUF, BUF_LEN, MSG_ID)			\
{										\
	GBLREF uint4		process_id;					\
	const err_msg		*msginfo;					\
										\
	MSGID_TO_ERRMSG(MSG_ID, msginfo);					\
	SNPRINTF(BUF, BUF_LEN, INST_FROZEN_COMMENT, process_id, msginfo->tag);	\
}

#ifdef DEBUG
void clear_fake_enospc_if_master_dead(void);

#define CLEAR_FAKE_ENOSPC_IF_MASTER_DEAD	clear_fake_enospc_if_master_dead()
#endif

/* This is a version of the macro which waits for the instance freeze to be lifted off assuming the process has
 * already attached to the journal pool. We need to wait for the freeze only if the input database cares about
 * anticipatory freeze. Examples of those databases that don't care are non-replicated databases, databases with
 * "freeze_on_fail" field set to FALSE in the file header etc. Hence the use of INST_FREEZE_ON_ERROR_ENABLED below.
 * Note: Do not use "hiber_start" as that uses timers and if we are already in a timer handler now, nested timers
 * wont work.
 */
/* #GTM_THREAD_SAFE : The below macro (WAIT_FOR_REPL_INST_UNFREEZE) is thread-safe */
#define WAIT_FOR_REPL_INST_UNFREEZE(CSA)											\
		wait_for_repl_inst_unfreeze(CSA)

/* This is a safer version of the WAIT_FOR_REPL_INST_UNFREEZE macro, which waits for the instance freeze
 * to be lifted off but is not sure if the process has access to the journal pool yet.
 * If it does not, then it assumes the instance is not frozen.
 */
/* #GTM_THREAD_SAFE : The below macro (WAIT_FOR_REPL_INST_UNFREEZE_SAFE) is thread-safe */
#define WAIT_FOR_REPL_INST_UNFREEZE_SAFE(CSA)		\
{							\
	GBLREF	jnlpool_addrs_ptr_t	jnlpool;	\
	jnlpool_addrs_ptr_t	local_jnlpool;		\
							\
	assert(NULL != CSA);				\
	local_jnlpool = JNLPOOL_FROM(CSA);		\
	if (IS_REPL_INST_FROZEN_JPL(local_jnlpool))	\
		WAIT_FOR_REPL_INST_UNFREEZE(CSA);	\
}

/* Below are similar macros like the above but with no CSA to specifically check for */
/* #GTM_THREAD_SAFE : The below macro (WAIT_FOR_REPL_INST_UNFREEZE_NOCSA) is thread-safe */
#define	WAIT_FOR_REPL_INST_UNFREEZE_NOCSA						\
{											\
	GBLREF	jnlpool_addrs_ptr_t	jnlpool;					\
	WAIT_FOR_REPL_INST_UNFREEZE_NOCSA_JPL(jnlpool);					\
}
#define	WAIT_FOR_REPL_INST_UNFREEZE_NOCSA_JPL(JPL)					\
		wait_for_repl_inst_unfreeze_nocsa_jpl(JPL)

#define WAIT_FOR_REPL_INST_UNFREEZE_NOCSA_SAFE		\
{							\
	GBLREF	jnlpool_addrs_ptr_t	jnlpool;	\
							\
	if (IS_REPL_INST_FROZEN)			\
		WAIT_FOR_REPL_INST_UNFREEZE_NOCSA;	\
}

/* GTM_DB_FSYNC/GTM_JNL_FSYNC are similar to GTM_FSYNC except that we don't do the fsync
 * (but instead hang) if we detect the instance is frozen. We proceed with the fsync once the freeze clears.
 * CSA is a parameter indicating which database it is that we want to fsync.
 * GTM_REPL_INST_FSYNC is different in that we currently don't care about instance freeze for replication
 * instance file writes.
 */
#define GTM_DB_FSYNC(CSA, FD, RC)							\
{											\
	GBLREF	jnlpool_addrs_ptr_t	jnlpool;					\
	jnlpool_addrs_ptr_t		local_jnlpool;					\
	node_local_ptr_t		cnl;						\
	sgmnt_addrs			*local_csa;					\
											\
	local_csa = CSA;								\
	local_jnlpool = JNLPOOL_FROM(local_csa);					\
	assert(local_csa || (!local_jnlpool || !local_jnlpool->jnlpool_ctl));		\
	if (NULL != local_csa)								\
	{										\
		WAIT_FOR_REPL_INST_UNFREEZE_SAFE(local_csa);				\
		cnl = local_csa->nl;							\
		if (NULL != cnl)							\
			INCR_GVSTATS_COUNTER(local_csa, cnl, n_db_fsync, 1);		\
	}										\
	GTM_FSYNC(FD, RC);								\
}

#define GTM_JNL_FSYNC(CSA, FD, RC)							\
{											\
	GBLREF	jnlpool_addrs_ptr_t	jnlpool;					\
		jnlpool_addrs_ptr_t	local_jnlpool;					\
	node_local_ptr_t		cnl;						\
											\
	local_jnlpool = JNLPOOL_FROM(CSA);						\
	assert(CSA || (!local_jnlpool || !local_jnlpool->jnlpool_ctl));			\
	if (NULL != CSA)								\
	{										\
		WAIT_FOR_REPL_INST_UNFREEZE_SAFE(CSA);					\
		cnl = (CSA)->nl;							\
		if (NULL != cnl)							\
			INCR_GVSTATS_COUNTER((CSA), cnl, n_jnl_fsync, 1);		\
	}										\
	GTM_FSYNC(FD, RC);								\
}

#define GTM_REPL_INST_FSYNC(FD, RC)	GTM_FSYNC(FD, RC)

#define	LSEEKWRITE_IS_TO_NONE			0
#define	LSEEKWRITE_IS_TO_DB			1
#define	LSEEKWRITE_IS_TO_JNL			2
#define	LSEEKWRITE_IS_TO_DB_ASYNC		3
#define	LSEEKWRITE_IS_TO_DB_ASYNC_RESTART	4

#ifdef DEBUG
#define	FAKE_ENOSPC(CSA, FAKE_WHICH_ENOSPC, LSEEKWRITE_TARGET, LCL_STATUS)							\
MBSTART {															\
	GBLREF	jnlpool_addrs_ptr_t	jnlpool;										\
	GBLREF	boolean_t		multi_thread_in_use;									\
																\
	if ((NULL != CSA) && !multi_thread_in_use) /* Do not manipulate fake-enospc (global variable) while in threaded code */	\
	{															\
		if (WBTEST_ENABLED(WBTEST_RECOVER_ENOSPC))									\
		{	/* This test case is only used by mupip */								\
			gtm_wbox_input_test_case_count++;									\
			if ((0 != ydb_white_box_test_case_count)								\
			    && (ydb_white_box_test_case_count <= gtm_wbox_input_test_case_count))				\
			{													\
				LCL_STATUS = ENOSPC;										\
				if (ydb_white_box_test_case_count == gtm_wbox_input_test_case_count)				\
					send_msg_csa(CSA_ARG(NULL) VARLSTCNT(4) ERR_TEXT, 2,					\
					     LEN_AND_LIT("Turning on fake ENOSPC for exit status test"));			\
			}													\
		} else if (!IS_DSE_IMAGE /*DSE does not freeze so let it work as normal */					\
			   && (!CSA->jnlpool || (CSA->jnlpool == jnlpool))							\
			   && (jnlpool && jnlpool->jnlpool_ctl) && (NULL != ((sgmnt_addrs *)CSA)->nl)				\
			   && ((sgmnt_addrs *)CSA)->nl->FAKE_WHICH_ENOSPC)							\
		{														\
			LCL_STATUS = ENOSPC;											\
			lseekwrite_target = LSEEKWRITE_TARGET;									\
		}														\
	}															\
} MBEND

#else
#define	FAKE_ENOSPC(CSA, FAKE_ENOSPC, LSEEKWRITE_TARGET, LCL_STATUS)
#endif

#ifdef DEBUG
#define DB_HANG_TRIGGER		75
#define DB_LSEEKWRITE_HANG(CSA)	(WBTEST_ENABLED(WBTEST_DB_WRITE_HANG)								\
					&& (CSA) && ((struct sgmnt_addrs_struct *)(CSA))->nl					\
					&& (DB_HANG_TRIGGER									\
						== INTERLOCK_ADD(&(((struct sgmnt_addrs_struct *)(CSA))->nl->db_writes), 1)))
#else
#define DB_LSEEKWRITE_HANG(CSA)	FALSE
#endif

/* #GTM_THREAD_SAFE : The below macro (DB_LSEEKWRITE) is thread-safe */
#define	DB_LSEEKWRITE(CSA, UDI, DB_FN, FD, OFFSET, BUFF, SIZE, STATUS)							\
MBSTART {														\
	GBLREF	uint4	process_id;											\
	sgmnt_addrs	*CSA_LOCAL = CSA;										\
															\
	assert(!CSA_LOCAL || !CSA_LOCAL->region || FILE_INFO(CSA_LOCAL->region)->grabbed_access_sem			\
			|| !(CSA_LOCAL)->nl || !FROZEN_CHILLED(CSA_LOCAL) || FREEZE_LATCH_HELD(CSA_LOCAL));		\
	DBG_CHECK_DIO_ALIGNMENT(UDI, OFFSET, BUFF, SIZE);								\
	/* We should never write to a READ_ONLY db file header unless we hold standalone access on the db */		\
	assert((0 != OFFSET) || !((sgmnt_data_ptr_t)BUFF)->read_only || (NULL == UDI) || UDI->grabbed_access_sem);	\
	DO_LSEEKWRITE(CSA_LOCAL, DB_FN, FD, OFFSET, BUFF, SIZE, STATUS, fake_db_enospc, DB_LSEEKWRITE_HANG(CSA),	\
			LSEEKWRITE_IS_TO_DB);										\
} MBEND

/* This is similar to DB_LSEEKWRITE except that this is used by GTMSECSHR and since that is root-owned we do not want
 * to pull in a lot of unnecessary things from the instance-freeze scheme so we directly invoke LSEEKWRITE instead of
 * going through DO_LSEEKWRITE.
 */
#define	GTMSECSHR_DB_LSEEKWRITE(UDI, FD, OFFSET, BUFF, SIZE, STATUS)			\
MBSTART {										\
	DBG_CHECK_DIO_ALIGNMENT(UDI, OFFSET, BUFF, SIZE);				\
	/* We should never write to a READ_ONLY db file header */			\
	assert((0 == OFFSET) && !((sgmnt_data_ptr_t)BUFF)->read_only);			\
	LSEEKWRITE(FD, OFFSET, BUFF, SIZE, STATUS);					\
} MBEND

/* #GTM_THREAD_SAFE : The below macro (DB_LSEEKWRITEASYNCSTART) is thread-safe */
#define	DB_LSEEKWRITEASYNCSTART(CSA, UDI, DB_FN, FD, OFFSET, BUFF, SIZE, CR, STATUS)					\
MBSTART {														\
	DBG_CHECK_DIO_ALIGNMENT(UDI, OFFSET, BUFF, SIZE);								\
	DO_LSEEKWRITEASYNC(CSA, DB_FN, FD, OFFSET, BUFF, SIZE, CR, STATUS, fake_db_enospc, LSEEKWRITE_IS_TO_DB_ASYNC);	\
} MBEND

/* #GTM_THREAD_SAFE : The below macro (DB_LSEEKWRITEASYNCRESTART) is thread-safe */
#define	DB_LSEEKWRITEASYNCRESTART(CSA, UDI, DB_FN, FD, BUFF, CR, STATUS)						\
MBSTART {														\
	DBG_CHECK_DIO_ALIGNMENT(UDI, SYS_IOCB(CR).aio_offset, BUFF, SYS_IOCB(CR).aio_nbytes);				\
	DO_LSEEKWRITEASYNC(CSA, DB_FN, FD, 0, BUFF, 0, CR, STATUS, fake_db_enospc, LSEEKWRITE_IS_TO_DB_ASYNC_RESTART);	\
} MBEND

#ifdef DEBUG
#define JNL_HANG_TRIGGER		500
#define JNL_LSEEKWRITE_HANG(CSA)	jnl_lseekwrite_hang(CSA)

static inline boolean_t jnl_lseekwrite_hang(struct sgmnt_addrs_struct *csa)
{
	return (WBTEST_ENABLED(WBTEST_JNL_WRITE_HANG)
			&& csa && csa->nl
			&& (JNL_HANG_TRIGGER == INTERLOCK_ADD(&csa->nl->jnl_writes, 1)));
}

#define LSEEKWRITE_HANG_SLEEP()		lseekwrite_hang_sleep()

static inline void lseekwrite_hang_sleep(void)
{
	send_msg_csa(CSA_ARG(NULL) VARLSTCNT(3) ERR_TEXT, 2, LEN_AND_LIT("TEST-I-LSEEKWRITEHANGSTART"));			\
	SLEEP_USEC(180ULL * E_6, TRUE);		/* Fixed 3 minutes, with restart. */
	send_msg_csa(CSA_ARG(NULL) VARLSTCNT(3) ERR_TEXT, 2, LEN_AND_LIT("TEST-I-LSEEKWRITEHANGEND"));				\
}
#else
#define JNL_LSEEKWRITE_HANG(CSA)	FALSE
#define LSEEKWRITE_HANG_SLEEP()		/**/
#endif

/* #GTM_THREAD_SAFE : The below macro (JNL_LSEEKWRITE) is thread-safe */
#define	JNL_LSEEKWRITE(CSA, JNL_FN, FD, OFFSET, BUFF, SIZE, STATUS)						\
	DO_LSEEKWRITE(CSA, JNL_FN, FD, OFFSET, BUFF, SIZE, STATUS, fake_jnl_enospc, JNL_LSEEKWRITE_HANG(CSA),	\
			LSEEKWRITE_IS_TO_JNL)

/* #GTM_THREAD_SAFE : The below macro (DO_LSEEKWRITE) is thread-safe */
#define DO_LSEEKWRITE(CSA, FNPTR, FD, OFFSET, BUFF, SIZE, STATUS, FAKE_WHICH_ENOSPC, HANG, LSEEKWRITE_TARGET)			\
MBSTART {															\
	GBLREF uint4	process_id;												\
	int		lcl_status;												\
	sgmnt_addrs	*local_csa = CSA;											\
	intrpt_state_t	prev_intrpt_state;											\
																\
	if (NULL != local_csa)													\
		WAIT_FOR_REPL_INST_UNFREEZE_SAFE(local_csa);									\
	DEFER_INTERRUPTS(INTRPT_IN_DB_JNL_LSEEKWRITE, prev_intrpt_state);							\
	if (HANG)														\
		LSEEKWRITE_HANG_SLEEP();											\
	LSEEKWRITE(FD, OFFSET, BUFF, SIZE, lcl_status);										\
	FAKE_ENOSPC(local_csa, FAKE_WHICH_ENOSPC, LSEEKWRITE_TARGET, lcl_status);						\
	if ((ENOSPC == lcl_status) && (NULL != local_csa))									\
	{															\
		wait_for_disk_space(local_csa, (char *)FNPTR, FD, (off_t)OFFSET, (char *)BUFF, (size_t)SIZE, &lcl_status);	\
		assert((NULL == local_csa) || (NULL == local_csa->nl) || !local_csa->nl->FAKE_WHICH_ENOSPC			\
		       || (ENOSPC != lcl_status));										\
	}															\
	STATUS = lcl_status;													\
	ENABLE_INTERRUPTS(INTRPT_IN_DB_JNL_LSEEKWRITE, prev_intrpt_state);							\
} MBEND

/* #GTM_THREAD_SAFE : The below macro (DO_LSEEKWRITEASYNC) is thread-safe */
#define DO_LSEEKWRITEASYNC(CSA, FNPTR, FD, OFFSET, BUFF, SIZE, CR, STATUS, FAKE_WHICH_ENOSPC, LSEEKWRITE_TARGET)		\
MBSTART {															\
	if (NULL != CSA)													\
		WAIT_FOR_REPL_INST_UNFREEZE_SAFE(CSA);										\
	switch (LSEEKWRITE_TARGET)												\
	{															\
		case LSEEKWRITE_IS_TO_DB_ASYNC:											\
			LSEEKWRITEASYNCSTART(CSA, FD, OFFSET, BUFF, SIZE, CR, STATUS);						\
			break;													\
		case LSEEKWRITE_IS_TO_DB_ASYNC_RESTART:										\
			LSEEKWRITEASYNCRESTART(CSA, FD, BUFF, CR, STATUS);							\
			break;													\
		default:													\
			assert(FALSE);												\
			break;													\
	}															\
} MBEND

/* Currently, writes to replication instance files do NOT trigger instance freeze behavior.
 * Neither does a pre-existing instance freeze affect replication instance file writes.
 * Hence this is defined as simple LSEEKWRITE.
 */
#define	REPL_INST_LSEEKWRITE	LSEEKWRITE

#define REPL_INST_AVAILABLE(GD_PTR)	(repl_inst_get_name((char *)replpool_id.instfilename, &full_len, \
						SIZEOF(replpool_id.instfilename), return_on_error, GD_PTR))

/* Inline Functions */

static inline boolean_t instance_freeze_honored(sgmnt_addrs *csa, jnlpool_addrs_ptr_t *lcl_jnlpool_out)
{
	jnlpool_addrs_ptr_t		lcl_jnlpool;
	GBLREF jnlpool_addrs_ptr_t	jnlpool;

	assert(NULL != csa);
	*lcl_jnlpool_out = lcl_jnlpool = JNLPOOL_FROM(csa);
	return ((NULL != lcl_jnlpool)
			&& (NULL != lcl_jnlpool->jnlpool_ctl)
			&& (REPL_ALLOWED(csa->hdr)
				|| mupip_jnl_recover	/* recover or rollback */
				|| ((NULL != csa->nl) && (csa->nl->onln_rlbk_pid))));
}

static inline void wait_for_repl_inst_unfreeze_nocsa_jpl(jnlpool_addrs_ptr_t jpl)
{
	GBLREF	int4			exit_state;
	GBLREF	int4			exi_condition;
	GBLREF	sig_pending		sigPendingQue;		/* Queue of pending signals handled in alt signal handling mode */

	assert((NULL != jpl) && (NULL != jpl->jnlpool_ctl));
	/* If this region is not replicated, do not care for instance freezes */
	while (jpl->jnlpool_ctl->freeze)
	{
		if (exit_state != 0)
		{
			forced_exit_err_display();
			EXIT(-exi_condition);
		}
		/* Within this polling "sleep loop" waiting for the freeze to be lifted, we need to also periodically check if
		 * there are any pending interrupts if we are doing alternate signal handling.
		 */
		PROCESS_PENDING_ALTERNATE_SIGNALS;
		/* Invoke this AFTER the "exit_state" check that way if we are already in the exit handler
		 * when we get a process-terminating signal (e.g. SIGTERM), we avoid double invocation of
		 * the exit handler but instead exit immediately (which is what EXIT will do if we are
		 * already in an exit handler).
		 */
		STAPI_INVOKE_DEFERRED_SIGNAL_HANDLER_IF_NEEDED(OK_TO_NEST_TRUE);
		/* Note that SHORT_SLEEP restarts the sleep in case of an EINTR whereas we want to stop the
		 * sleep below in case of an interrupt (e.g. a MUPIP STOP that asks us to terminate).
		 * Therefore do not use SHORT_SLEEP here. Instead use "SLEEP_USEC" with FALSE as the second parameter.
		 * SLEEP_USEC allows a max of 1000, so assert that SLEEP_INSTFREEZEWAIT is within that range.
		 */
		assert(1000 > SLEEP_INSTFREEZEWAIT);
		SLEEP_USEC((SLEEP_INSTFREEZEWAIT * 1000), FALSE);
		DEBUG_ONLY(CLEAR_FAKE_ENOSPC_IF_MASTER_DEAD);
	}
}

static inline void wait_for_repl_inst_unfreeze(sgmnt_addrs *csa)
{
	gd_region		*reg;
	jnlpool_addrs_ptr_t	local_jnlpool;	/* needed by INSTANCE_FREEZE_HONORED */
	char			time_str[CTIME_BEFORE_NL + 2]; /* for GET_CUR_TIME macro */
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	assert(NULL != csa);
	if (INSTANCE_FREEZE_HONORED(csa, local_jnlpool))
	{
#ifdef underconstruction
		if (TREF(in_mupip_integ) && !TREF(integ_cannotskip_crit))
		{
			TREF(instance_frozen_crit_skipped) = TRUE;
			return;		/* MUPIP INTEG. Don't wait any further for the freeze to be lifted. */
		}
#endif
		reg = csa->region;
		if (!IS_GTM_IMAGE)
		{
			GET_CUR_TIME(time_str);
			gtm_putmsg_csa(CSA_ARG(NULL) VARLSTCNT(7) ERR_MUINSTFROZEN, 5, CTIME_BEFORE_NL, &time_str[0],
					local_jnlpool->repl_inst_filehdr->inst_info.this_instname, DB_LEN_STR(reg));
		}
		WAIT_FOR_REPL_INST_UNFREEZE_NOCSA_JPL(local_jnlpool);
		if (!IS_GTM_IMAGE)
		{
			GET_CUR_TIME(time_str);
			gtm_putmsg_csa(CSA_ARG(NULL) VARLSTCNT(7) ERR_MUINSTUNFROZEN, 5, CTIME_BEFORE_NL, &time_str[0],
					local_jnlpool->repl_inst_filehdr->inst_info.this_instname, DB_LEN_STR(reg));
		}
	}
}

#endif	/* #ifndef ANTICIPATORY_FREEZE_H */
