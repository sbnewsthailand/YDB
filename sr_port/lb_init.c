/****************************************************************
 *								*
 * Copyright (c) 2001-2017 Fidelity National Information	*
 * Services, Inc. and/or its subsidiaries. All rights reserved.	*
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

#include "gtm_string.h"

#include "compiler.h"
#include "toktyp.h"
#include "comp_esc.h"
#include "advancewindow.h"
#include "lb_init.h"

GBLREF	struct ce_sentinel_desc		*ce_def_list;

error_def(ERR_CETOOMANY);

LITREF char	ctypetab[NUM_CHARS];

#define MAX_SUBSTITUTIONS	1024

void lb_init(void)
{
	int				num_subs, y;
	short int			sav_last_src_col, source_col;
	int4				skip_count = 0;
	unsigned char			*cp, *cp1;
	boolean_t			possible_sentinel;
	struct ce_sentinel_desc		*shp;
#	ifdef DEBUG
	unsigned char			original_source[MAX_SRCLINE + 1];
#	endif
	DCL_THREADGBL_ACCESS;

	SETUP_THREADGBL_ACCESS;
	if (NULL != ce_def_list)
	{
#		ifdef DEBUG
		memcpy(original_source, (TREF(source_buffer)).addr, (TREF(source_buffer)).len + 1);	/* include NUL term chars */
#		endif
		source_col = 1;
		num_subs = 0;
		possible_sentinel = TRUE;
		while (possible_sentinel)
		{
			possible_sentinel = FALSE;
			cp = (unsigned char *)((TREF(source_buffer)).addr + source_col - 1);
			if (('\0' == *cp) || (DEL < *cp))
				break;
			if ('\"' == *cp)
			{
				for (cp1 = cp + 1; ; )
				{
					if (SP > *cp1)
						break;
					if ('\"' == *cp1++)
					{
						if ('\"' == *cp1)
							cp1++;			/* escaped quotation mark inside string */
						else
							break;			/* end of string */
					}
				}
				source_col += (cp1 - cp);
				cp = cp1;
				if ('\0' == *cp)
					break;
			} else if ('?' == *cp)
			{
				for (cp1 = cp + 1; ; )
				{
					if (NUM_ASCII_CHARS <= *cp1)
						break;
					y = ctypetab[*cp1];
					if ((TK_UPPER == y) || (TK_LOWER == y) || (TK_DIGIT == y) || (TK_PERIOD == y)
					  || (TK_ATSIGN == y))
						cp1++;
					else if ('\"' == *cp1)	/* quoted string in pattern */
					{
						for (cp1++; ; )
						{
							if (SP > *cp1)
								break;
							if ('\"' == *cp1)
							{
								cp1++;
								if ('\"' == *cp1)	/* escaped quotation mark in string */
									cp1++;
								else			/* character following string */
									break;
							} else
								cp1++;
						}
					} else
						break;
				}
				source_col += (cp1 - cp);
				cp = cp1;
				if ('\0' == *cp)
					break;
			}
			for (shp = ce_def_list; NULL != shp;  shp = shp->next)
			{
				if (0 == memcmp(cp, shp->escape_sentinel, shp->escape_length))
				{
					ce_substitute (shp, source_col, &skip_count);
					num_subs++;
					if (MAX_SUBSTITUTIONS >= num_subs)
					{
						if (0 < skip_count)		/* a substitution occurred */
							possible_sentinel = TRUE;
					} else
					{
						sav_last_src_col = TREF(last_source_column);
						TREF(last_source_column) = source_col;
						stx_error (ERR_CETOOMANY);
						TREF(last_source_column) = sav_last_src_col;
					}
					break;
				}
			}
			if (!possible_sentinel)
			{	/* in this column */
				source_col++;
				possible_sentinel = TRUE;	/* next column may have sentinel */
			}
		}
	}
	TREF(lexical_ptr) = (TREF(source_buffer)).addr;
	advancewindow();
	advancewindow();
	return;
}
