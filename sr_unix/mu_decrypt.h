/****************************************************************
 *								*
 *	Copyright 2009 Fidelity Information Services, Inc	*
 *								*
 *	This source code contains the intellectual property	*
 *	of its copyright holder(s), and is made available	*
 *	under a license.  If you do not know the terms of	*
 *	the license, please stop and do not read further.	*
 *								*
 ****************************************************************/

#ifndef MUPIP_CRYPT_INCLUDED
#define MUPIP_CRYPT_INCLUDED

#define GET_FD_HASH(FNAME, FLEN, FD, HASH, IS_ENCRYPTED)								\
{															\
	jnl_file_header		*jfh;											\
	sgmnt_data_ptr_t	csd;											\
	void			*header;										\
	int			save_errno, hdr_sz;									\
															\
	error_def(ERR_JNLBADLABEL);											\
	OPENFILE(FNAME, O_RDONLY, FD);											\
	if (-1 == FD)													\
	{														\
		save_errno = errno;											\
		GC_DISPLAY_FILE_ERROR_AND_RETURN("Error accessing file !AD.", FNAME, FLEN, save_errno);			\
	}														\
	hdr_sz = JNL_HDR_LEN;												\
	header = malloc(JNL_HDR_LEN);											\
	LSEEKREAD(FD, 0, header, hdr_sz, save_errno);									\
	if (0 != save_errno)												\
	{														\
		free(header);												\
		GC_DISPLAY_FILE_ERROR_AND_RETURN("Error reading file !AD.", FNAME, FLEN, save_errno);			\
	}														\
	jfh = (jnl_file_header *) header;										\
	if (0 != MEMCMP_LIT(jfh->label, JNL_LABEL_TEXT))								\
	{														\
		gtm_putmsg(VARLSTCNT(4) ERR_JNLBADLABEL, 2, LEN_AND_STR(FNAME));					\
		return FALSE;												\
	}														\
	memcpy(HASH, jfh->encryption_hash, GTMCRYPT_HASH_LEN);								\
	IS_ENCRYPTED = jfh->is_encrypted;										\
	free(header);													\
}


#define GC_DISPLAY_FILE_ERROR_AND_RETURN(MESSAGE, FNAME, FLEN, RC)	\
{									\
	char	*errptr;						\
									\
	util_out_print(MESSAGE, TRUE, FLEN, FNAME);			\
	errptr = (char *)STRERROR(RC);					\
	util_out_print("System Error: !AZ", TRUE, errptr);		\
	return RC;							\
}

int	mu_decrypt(char *, uint4, uint4);

#endif /* MUPIP_CRYPT_INCLUDED */
