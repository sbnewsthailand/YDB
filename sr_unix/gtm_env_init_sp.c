/****************************************************************
 *								*
 *	Copyright 2004, 2009 Fidelity Information Services, Inc	*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#include "mdef.h"

#ifdef __MVS__
#include <env.h>
#endif
#include <errno.h>
#include "gtm_string.h"
#include "gtm_strings.h"
#include "gtm_ctype.h"
#include "gtm_unistd.h"
#include "gtm_stdio.h"

#include "gtmimagename.h"
#include "gtm_logicals.h"
#include "trans_numeric.h"
#include "trans_log_name.h"
#include "logical_truth_value.h"
#include "iosp.h"		/* for SS_ */
#include "nametabtyp.h"		/* for namelook */
#include "namelook.h"
#include "io.h"
#include "iottdef.h"
#include "gtm_env_init.h"	/* for gtm_env_init() and gtm_env_init_sp() prototype */
#include "gtm_utf8.h"		/* UTF8_NAME */
#include "gtm_zlib.h"
#include "error.h"
#include "gtm_limits.h"

#define	DEFAULT_NON_BLOCKED_WRITE_RETRIES	10	/* default number of retries */
#ifdef __MVS__
#  define PUTENV_BPXK_MDUMP_PREFIX "_BPXK_MDUMP="
#endif

GBLREF	int4			gtm_shmflags;			/* Shared memory flags for shmat() */
GBLREF	uint4			gtm_principal_editing_defaults;	/* ext_cap flags if tt */
GBLREF	boolean_t		is_gtm_chset_utf8;
GBLREF	boolean_t		utf8_patnumeric;
GBLREF	boolean_t		badchar_inhibit;
GBLREF	boolean_t		gtm_quiet_halt;
GBLREF	int			gtm_non_blocked_write_retries; /* number for retries for non_blocked write to pipe */
GBLREF	char			*gtm_core_file;
GBLREF	char			*gtm_core_putenv;
ZOS_ONLY(GBLREF	char		*gtm_utf8_locale_object;)
ZOS_ONLY(GBLREF	boolean_t	gtm_tag_utf8_as_ascii;)

static nametabent editing_params[] =
{
	{7, "EDITING"},
	{6, "INSERT"},
	{9, "NOEDITING"},
	{8, "NOINSERT"}
};

static unsigned char editing_index[27] =
{
	0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
	2, 2, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
	4, 4, 4
};

/* Unix only environment initializations */
void	gtm_env_init_sp(void)
{
	mstr		val, trans;
	int4		status, index;
	size_t		cwdlen;
	boolean_t	ret, is_defined;
	char		buf[MAX_TRANS_NAME_LEN], *token, cwd[GTM_PATH_MAX];
	char		*cwdptr;

#ifdef __MVS__
	/* For now OS/390 only. Eventually, this will be added to all UNIX platforms along with the
	   capability to specify the desired directory to put a core file in.
	*/
	if (NULL == gtm_core_file)
	{
		token = getcwd(cwd, sizeof(cwd));
		if (NULL != token)
		{
			cwdlen = strlen(cwd);
			gtm_core_putenv = malloc(cwdlen + ('/' == cwd[cwdlen - 1] ? 0 : 1) + sizeof(GTMCORENAME)
						 + strlen(PUTENV_BPXK_MDUMP_PREFIX));
			MEMCPY_LIT(gtm_core_putenv, PUTENV_BPXK_MDUMP_PREFIX);
			gtm_core_file = cwdptr = gtm_core_putenv + strlen(PUTENV_BPXK_MDUMP_PREFIX);
			memcpy(cwdptr, &cwd, cwdlen);
			cwdptr += cwdlen;
			if ('/' != cwd[cwdlen - 1])
				*cwdptr++ = '/';
			memcpy(cwdptr, GTMCORENAME, sizeof(GTMCORENAME));       /* Also copys in trailing null */
		} /* else gtm_core_file/gtm_core_putenv remain null and we likely cannot generate proper core files */
	}
#endif
	val.addr = GTM_SHMFLAGS;
	val.len = sizeof(GTM_SHMFLAGS) - 1;
	gtm_shmflags = (int4)trans_numeric(&val, &is_defined, TRUE);	/* Flags vlaue (0 is undefined or bad) */

	val.addr = GTM_QUIET_HALT;
	val.len = sizeof(GTM_QUIET_HALT) - 1;
	ret = logical_truth_value(&val, FALSE, &is_defined);
	if (is_defined)
		gtm_quiet_halt = ret;

	/* ZLIB library compression level */
	val.addr = GTM_ZLIB_CMP_LEVEL;
	val.len = sizeof(GTM_ZLIB_CMP_LEVEL) - 1;
	gtm_zlib_cmp_level = trans_numeric(&val, &is_defined, TRUE);
	if (GTM_CMPLVL_OUT_OF_RANGE(gtm_zlib_cmp_level))
		gtm_zlib_cmp_level = ZLIB_CMPLVL_MIN;	/* no compression in this case */

	gtm_principal_editing_defaults = 0;
	val.addr = GTM_PRINCIPAL_EDITING;
	val.len = sizeof(GTM_PRINCIPAL_EDITING) - 1;
	if (SS_NORMAL == (status = TRANS_LOG_NAME(&val, &trans, buf, sizeof(buf), do_sendmsg_on_log2long)))
	{
		assert(trans.len < sizeof(buf));
		trans.addr[trans.len] = '\0';
		token = strtok(trans.addr, ":");
		while (NULL != token)
		{
			if (ISALPHA(token[0]))
				index = namelook(editing_index, editing_params, STR_AND_LEN(token));
			else
				index = -1;	/* ignore this token */
			if (0 <= index)
			{
				switch (index)
				{
				case 0:	/* EDITING */
					gtm_principal_editing_defaults |= TT_EDITING;
					break;
				case 1:	/* INSERT */
					gtm_principal_editing_defaults &= ~TT_NOINSERT;
					break;
				case 2:	/* NOEDITING */
					gtm_principal_editing_defaults &= ~TT_EDITING;
					break;
				case 3:	/* NOINSERT */
					gtm_principal_editing_defaults |= TT_NOINSERT;
					break;
				}
			}
			token = strtok(NULL, ":");
		}
	}
	val.addr = GTM_CHSET_ENV;
	val.len = STR_LIT_LEN(GTM_CHSET_ENV);
	if (SS_NORMAL == (status = TRANS_LOG_NAME(&val, &trans, buf, sizeof(buf), do_sendmsg_on_log2long))
		&& STR_LIT_LEN(UTF8_NAME) == trans.len)
	{
		if (!strncasecmp(buf, UTF8_NAME, STR_LIT_LEN(UTF8_NAME)))
		{
			is_gtm_chset_utf8 = TRUE;
#ifdef __MVS__
			val.addr = GTM_CHSET_LOCALE_ENV;
			val.len = STR_LIT_LEN(GTM_CHSET_LOCALE_ENV);
			if ((SS_NORMAL == (status = TRANS_LOG_NAME(&val, &trans, buf, sizeof(buf), do_sendmsg_on_log2long))) &&
				(0 < trans.len))
			{	/* full path to 64 bit ASCII UTF-8 locale object */
				gtm_utf8_locale_object = malloc(trans.len + 1);
				strcpy(gtm_utf8_locale_object, buf);
			}

			val.addr = GTM_TAG_UTF8_AS_ASCII;
			val.len = STR_LIT_LEN(GTM_TAG_UTF8_AS_ASCII);
			if (SS_NORMAL == (status = TRANS_LOG_NAME(&val, &trans, buf, sizeof(buf), do_sendmsg_on_log2long)))
			{	/* We to tag UTF8 files as ASCII so we can read them, this var disables that */
				if (status = logical_truth_value(&val, FALSE, &is_defined) && is_defined)
					gtm_tag_utf8_as_ascii = FALSE;
			}
#endif
			/* Initialize $ZPATNUMERIC only if $ZCHSET is "UTF-8" */
			val.addr = GTM_PATNUMERIC_ENV;
			val.len = STR_LIT_LEN(GTM_PATNUMERIC_ENV);
			if (SS_NORMAL == (status = TRANS_LOG_NAME(&val, &trans, buf, sizeof(buf), do_sendmsg_on_log2long))
				&& STR_LIT_LEN(UTF8_NAME) == trans.len
				&& !strncasecmp(buf, UTF8_NAME, STR_LIT_LEN(UTF8_NAME)))
			{
				utf8_patnumeric = TRUE;
			}
			val.addr = GTM_BADCHAR_ENV;
			val.len = STR_LIT_LEN(GTM_BADCHAR_ENV);
			status = logical_truth_value(&val, TRUE, &is_defined);
			if (is_defined)
				badchar_inhibit = status ? TRUE : FALSE;
		}
	}
	/* Initialize variable that controls number of retries for non-blocked writes to a pipe on unix */
	val.addr = GTM_NON_BLOCKED_WRITE_RETRIES;
	val.len = sizeof(GTM_NON_BLOCKED_WRITE_RETRIES) - 1;
	gtm_non_blocked_write_retries = trans_numeric(&val, &is_defined, TRUE);
	if (!is_defined)
		gtm_non_blocked_write_retries = DEFAULT_NON_BLOCKED_WRITE_RETRIES;
}
