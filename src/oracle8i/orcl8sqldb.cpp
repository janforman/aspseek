/* Copyright (C) 2000, 2001, 2002 by SWsoft
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*  $Id: orcl8sqldb.cpp,v 1.5 2002/05/14 11:16:07 kir Exp $
    Author : Igor Sukhih
*/

#include "aspseek-cfg.h"
#include "orcl8sqldbi.h"
#include "index.h"

extern "C" {
	CLogger * plogger = NULL;
}


static sb2 null_ind = -1;
static ub4 bloblen = 86;
static ub4 ulen = sizeof(ULONG);
static ub2 csid = OCI_UCS2ID;
static ub2 csnchar = SQLCS_NCHAR;

char* memcpyq(char* dest, const char* src, int count)
{
	while (count-- > 0)
	{
		if (*src == '\'')
		{
			*dest++ = '\\';
		}
		else if (*src == '"')
		{
			*dest++ = '\\';
		}
		else if (*src == '\\')
		{
			*dest++ = '\\';
		}
		*dest++ = *src++;
	}
	return dest;
}

char *COrclSQLQuery::MakeQuery()
{
	char *sp, *pprev;
	ULONG i = 0, len = 0;

	sp = pprev = m_query;

	if (!m_param)
	{
		m_sqlquery = strdup(m_query);
		m_qlen = strlen(m_sqlquery);
		m_sqlquery[m_qlen] = 0;
		return m_sqlquery;
	}
	m_sqlquery = new char[SQL_LEN + 50];
	while(*sp++)
	{
		if (*sp == '%' || *sp == ':')
		{
			char *sp_t = sp++;
			while (*sp && ((*sp == 's') || (*sp >=0x30 && *sp<=0x39)))
				sp++;
			if (sp_t+1 == sp)
				continue;

			if (i > (m_param->size() - 1))
			{
				plogger->log(CAT_ALL, L_ERR, "Error in sql number of param = %d, query=%s\n", m_param->size(), m_query);
				return NULL;
			}
			memcpy(m_sqlquery + len, pprev, sp_t-pprev);
			len += sp_t-pprev;

			if ((*m_param)[i].m_len && *(sp_t+1) == 's')
			{
				char *ep;
				switch ((*m_param)[i].m_type)
				{
				case QTYPE_STRESC:
					ep = memcpyq(m_sqlquery + len, (*m_param)[i].m_param, (*m_param)[i].m_len);
					len += ep - (m_sqlquery + len);
					break;
				case QTYPE_STR:
					memcpy(m_sqlquery + len, (*m_param)[i].m_param, (*m_param)[i].m_len);
					len += (*m_param)[i].m_len;
					break;
				case QTYPE_INT:
					len += sprintf(m_sqlquery + len, "%d", *((int*)(*m_param)[i].m_param));
					break;
				case QTYPE_ULONG:
					len += sprintf(m_sqlquery + len, "%lu", *((ULONG*)(*m_param)[i].m_param));
					break;
				}
			}
			else
			{
				if (*(sp_t+1) != 's')
				{
					memcpy(m_sqlquery + len, sp_t, sp - sp_t);
					len += sp - sp_t;
				}
			}
			pprev = sp;
			i++;
		}
	}
	strcpy(m_sqlquery + len, pprev);
	len += sp-pprev - 1;
	m_sqlquery[len+1] = 0;
	m_qlen = len;
	return m_sqlquery;
}

sb4 cbf_no_data(dvoid *ctxp, OCIBind *bindp, ub4 iter, ub4 index,
		dvoid **bufpp, ub4 *alenp, ub1 *piecep, dvoid **indpp)
{
	*bufpp = (dvoid *) 0;
	*alenp = 0;
	null_ind = -1;
	*indpp = (dvoid *) &null_ind;
	*piecep = OCI_ONE_PIECE;
	return OCI_CONTINUE;
}

sb4 cbf_get_data(dvoid *ctxp, OCIBind *bindp, ub4 iter, ub4 index,
		dvoid **bufpp, ub4 **alenp, ub1 *piecep,
		dvoid **indpp, ub2 **rcodepp)
{
	COutData *out = (COutData *) ctxp;
	*bufpp = (dvoid *) &out->m_out;
	*indpp = (dvoid *) &out->m_ind;
	*rcodepp = &out->m_rc;
	*alenp = &ulen;

	return OCI_CONTINUE;
}

sb4 cbf_get_datab(dvoid *ctxp, OCIBind *bindp, ub4 iter, ub4 index,
		dvoid **bufpp, ub4 **alenp, ub1 *piecep,
		dvoid **indpp, ub2 **rcodepp)
{

	COutData *out = (COutData *) ctxp;
	*bufpp = (dvoid *) out->m_lobout;
	*indpp = (dvoid *) &out->m_ind;
	*rcodepp = &out->m_rc;
	*alenp = &bloblen;
	*piecep = OCI_ONE_PIECE;

	return OCI_CONTINUE;
}

int COrclSQLQuery::WriteBlob(OCISvcCtx *svchp, OCIError *errhp)
{
	ULONG i, k,j = 0;
	if (!m_param)
	{
		return 0;
	}
	for(i = 0; i< DB_MAXBLOB; i++)
	{
		for(k = j; k < m_param->size(); k++)
		{
			if ((*m_param)[k].m_type == 4)
			{
				if ((*m_param)[k].m_len)
				{
					if (OCILobWrite(svchp, errhp, m_lobout[i].m_lobout, (ub4*) &(*m_param)[k].m_len, 1, (dvoid *) (*m_param)[k].m_param,
						(ub4)(*m_param)[k].m_len, OCI_ONE_PIECE, (dvoid *)0,
						(sb4 (*)(dvoid *, dvoid *, ub4 *, ub1 *)) 0,
						(ub2) 0, (ub1) SQLCS_IMPLICIT))
					{
						plogger->log(CAT_ALL, L_ERR, "Error OCILobWrite\n");
						return 1;
					}
				}
				j = k + 1;
				break;
			}
		}
		if (m_lobout[i].m_lobout)
		{
			if (OCIDescriptorFree((dvoid *) m_lobout[i].m_lobout, (ub4) OCI_DTYPE_LOB))
			{
				plogger->log(CAT_ALL, L_ERR, "Error in OCIDescriptorFree\n");
			}
			m_lobout[i].m_lobout = NULL;
		}
	}
	return 0;
}

int COrclSQLQuery::BindParam(OCIStmt *stmthp, OCIError *errhp, OCIEnv *envhp)
{
	char *sp = m_query;
	ULONG i = 0, bndpos = 0;
	char pos[4];
	char bndnm[7];

	while(*sp++)
	{
		if (*sp == '%' && *(sp+1) == 's')
		{
			i++;
			continue;
		}
		else if (*sp == ':')
		{
			char *sp_t = sp++;
			while (*sp && (*sp >=0x30 && *sp<=0x39))
				sp++;

			if (sp_t == sp)
				continue;

			if (!strncasecmp(sp_t, ":out", 4))
			{
				if (OCIBindByName(stmthp, &m_bndout[0], errhp,
					(text *) ":out", (sb4) 4,
					(dvoid *) 0, (sb4) sizeof(int), SQLT_INT,
					(dvoid *) 0, (ub2 *) 0, (ub2 *) 0,
					(ub4) 0, (ub4 *) 0, (ub4) OCI_DATA_AT_EXEC))
				{
					plogger->log(CAT_ALL, L_ERR, "Error OCIBindByName in query=%s\n", m_query);
					return 1;
				}
				if (OCIBindDynamic(m_bndout[0], errhp, (dvoid *) &m_out, cbf_no_data,
					(dvoid *) &m_out, cbf_get_data))
				{
					plogger->log(CAT_ALL, L_ERR, "Error OCIBindDynamic\n");
					return 1;
				}
				continue;
			}
			if (!strncasecmp(sp_t, ":bind", 5))
			{
				memcpy(bndnm, sp_t, 6);
				bndnm[6] = 0;
				int bndout = atoi(sp_t + 5);
				if (OCIBindByName(stmthp, &m_bndout[bndout], errhp,
					(text *) bndnm, (sb4) 6,
					(dvoid *) 0, (sb4) -1, SQLT_BLOB,
					(dvoid *) 0, (ub2 *)0, (ub2 *)0,
					(ub4) 0, (ub4 *) 0, (ub4) OCI_DATA_AT_EXEC))
				{
					plogger->log(CAT_ALL, L_ERR, "Error OCIBindByName in query=%s\n", m_query);
					return 1;
				}
				if (OCIDescriptorAlloc((dvoid *) envhp, (dvoid **) &m_lobout[bndout - 1].m_lobout,
					(ub4) OCI_DTYPE_LOB, (size_t) 0, (dvoid **) 0))
				{
					plogger->log(CAT_ALL, L_ERR, "Error OCIDescriptorAlloc %s\n", m_query);
					return 1;
				}

				if (OCIBindDynamic(m_bndout[bndout], errhp, (dvoid *) &m_lobout[bndout-1], cbf_no_data,
					(dvoid *) &m_lobout[bndout-1], cbf_get_datab))
				{
					plogger->log(CAT_ALL, L_ERR, "Error OCIBindDynamic\n");
					return 1;
				}

				continue;
			}

			memset(pos, 0, 4);
			memcpy(pos, sp_t, sp - sp_t);

			if (i > (m_param->size() - 1))
			{
				plogger->log(CAT_ALL, L_ERR, "Error in sql number of param = %d, query=%s\n", m_param->size(), m_query);
				return 1;
			}
lab:
			switch ((*m_param)[i].m_type)
			{
			case QTYPE_STRESC:
			case QTYPE_STR:
				if (OCIBindByName(stmthp, &m_bndin[bndpos], errhp, (text *) pos, (sb4) strlen(pos),
					(dvoid *) ((*m_param)[i].m_param), (sb4) ((*m_param)[i].m_len),	SQLT_CHR,
					(dvoid *) 0, (ub2 *) 0, (ub2 *) 0, (ub4) 0, (ub4 *) 0, (ub4) OCI_DEFAULT))
				{
					plogger->log(CAT_ALL, L_ERR, "Error OCIBindByPos  SQLT_CHR \n");
					return 1;
				}
				break;
			case QTYPE_INT:
				if (OCIBindByName(stmthp, &m_bndin[bndpos], errhp, (text *) pos, (sb4) strlen(pos),
					(dvoid *)(*m_param)[i].m_param, (*m_param)[i].m_len, SQLT_INT,
					0, 0, 0, 0, 0,  OCI_DEFAULT))
				{
					plogger->log(CAT_ALL, L_ERR, "Error OCIBindByPos SQLT_INT\n");
					return 1;
				}
				break;
			case QTYPE_ULONG:
				if (OCIBindByName(stmthp, &m_bndin[bndpos], errhp, (text *) pos, (sb4) strlen(pos),
					(dvoid *)(*m_param)[i].m_param, (*m_param)[i].m_len, SQLT_UIN,
					0, 0, 0, 0, 0,  OCI_DEFAULT))
				{
					plogger->log(CAT_ALL, L_ERR, "Error OCIBindByPos SQLT_UIN\n");
					return 1;
				}
				break;
			case QTYPE_BLOB:
				i++;
				goto lab;
			case QTYPE_STR2:

				if (OCIBindByName(stmthp, &m_bndin[bndpos], errhp, (text *) pos, (sb4) strlen(pos),
					(dvoid *) ((*m_param)[i].m_param), (sb4) ((*m_param)[i].m_len << 1), SQLT_CHR,
					(dvoid *) 0, (ub2 *) 0, (ub2 *) 0, (ub4) 0, (ub4 *) 0, (ub4) OCI_DEFAULT))
				{
					plogger->log(CAT_ALL, L_ERR, "Error OCIBindByPos  SQLT_STR \n");
					return 1;
				}
				if (OCIAttrSet(m_bndin[bndpos], OCI_HTYPE_BIND, (dvoid*) &csnchar, (ub4) 0,
					(ub4) OCI_ATTR_CHARSET_FORM, errhp))
				{
					plogger->log(CAT_ALL, L_ERR, "Error OCIAttrSet\n");
				}
				if (OCIAttrSet(m_bndin[bndpos], OCI_HTYPE_BIND, (dvoid*) &csid, (ub4) 0,
					(ub4) OCI_ATTR_CHARSET_ID, errhp))
				{
					plogger->log(CAT_ALL, L_ERR, "Error OCIAttrSet\n");
				}
/*				if (OCIAttrSet(m_bndin[bndpos], OCI_HTYPE_BIND, (dvoid*) ((*m_param)[i].m_len), 0,
					OCI_ATTR_MAXDATA_SIZE, errhp))
				{
					plogger->log(CAT_ALL, L_ERR, "Error OCIAttrSet\n");
				}
*/
				break;
			case QTYPE_DBL:
				if (OCIBindByName(stmthp, &m_bndin[bndpos], errhp, (text *) pos, (sb4) strlen(pos),
					(dvoid *)(*m_param)[i].m_param, (*m_param)[i].m_len, SQLT_FLT,
					0, 0, 0, 0, 0,  OCI_DEFAULT))
				{
					plogger->log(CAT_ALL, L_ERR, "Error OCIBindByPos SQLT_UIN\n");
					return 1;
				}
			}
			bndpos++;
			i++;
		}
	}
	return 0;
}

/****************************************/
char *COrclSQLAnswer::ReadBlob(OCILobLocator *lob)
{
	char *buf = NULL;
	ub4   loblen = 0;
	OCILobGetLength(m_svchp, m_errhp, lob, &loblen);
	if (loblen)
	{
		buf = new char[loblen+1];
		buf[loblen] = 0;
		if (OCILobRead(m_svchp, m_errhp, lob, &loblen, 1, (dvoid *) buf,
			loblen, (dvoid *)0,
			(sb4 (*)(dvoid *, dvoid *, ub4, ub1)) 0,
			(ub2) 0, (ub1) SQLCS_IMPLICIT))
		{
			plogger->log(CAT_ALL, L_ERR, "Error in OCILobRead\n");
			return NULL;
		}
	}
	return buf;
}

CSQLAnswer *COrclSQLDatabase::sql_queryw(CSQLQuery *query)
{
	char *qbuf = query->MakeQuery();
// plogger->log(CAT_SQL, L_DEBUG, "w->%s\n", qbuf);

	ub2 stmt_type;
	sword status;
	OCIStmt	*stmthp = NULL;
	OCIDefine *defb[DB_MAXCOLS];
	memset(defb, 0, sizeof(defb));
	if ((status = OCIHandleAlloc((dvoid *)m_envhp, (dvoid **) &stmthp, OCI_HTYPE_STMT, 0, NULL)) && status != OCI_SUCCESS_WITH_INFO)
	{
		plogger->log(CAT_ALL, L_ERR, "Error OCIHandleAlloc %s\n", DispError());
		return NULL;
	}
	if ((status = OCIStmtPrepare(stmthp, m_errhp, (text *)qbuf, (ub4) strlen(qbuf), OCI_NTV_SYNTAX, OCI_DEFAULT)) && status != OCI_SUCCESS_WITH_INFO)
	{
		plogger->log(CAT_ALL, L_ERR, "Error OCIStmtPrepare %s\n", DispError());
		return NULL;
	}
	if (OCIAttrGet(stmthp, OCI_HTYPE_STMT, &stmt_type, 0, OCI_ATTR_STMT_TYPE, m_errhp))
	{
		plogger->log (CAT_ALL, L_ERR, "Error OCIAttrGet %s\n", DispError());
		return NULL;
	}
	if (((COrclSQLQuery*)query)->BindParam(stmthp, m_errhp, m_envhp))
	{
		plogger->log(CAT_ALL, L_ERR, "%s\n", DispError());
	}
	if (stmt_type != OCI_STMT_SELECT)
	{
		plogger->log(CAT_ALL, L_ERR, "Not select query in sql_querywcode = %d in %s \n", stmt_type, query);
		return NULL;
	}
	if ((status = OCIStmtExecute(m_svchp, stmthp, m_errhp, 0, 0,(OCISnapshot *) NULL, (OCISnapshot *) NULL, OCI_DEFAULT)) && status != OCI_SUCCESS_WITH_INFO)
	{
		plogger->log(CAT_ALL, L_ERR, "Error OCIStmtExecute: %s %s", DispError(), qbuf);
		return NULL;
	}

	ub2 coltype, colsize;
	OCIParam *param;
	int colcnt = 0;
	OrclRes *res = GetRes();
	while(OCIParamGet(stmthp, OCI_HTYPE_STMT, m_errhp, (dvoid **) &param, colcnt+1) == OCI_SUCCESS)
	{
		if (OCIAttrGet(param, OCI_DTYPE_PARAM, &coltype, 0, OCI_ATTR_DATA_TYPE, m_errhp))
		{
			DispError();
			return NULL;
		}
		if (OCIAttrGet(param, OCI_DTYPE_PARAM, &colsize, 0, OCI_ATTR_DATA_SIZE, m_errhp))
		{
			DispError();
			return NULL;
		}
		switch (coltype)
		{
		case SQLT_CHR: /* variable length string */
		case SQLT_AFC: /* fixed length string */
			res->m_colsize[colcnt] = colsize;
			res->m_cdefbuff[colcnt] = new char[colsize+1];
			res->m_cdefbuff[colcnt][colsize] = 0;
			if (OCIDefineByPos(stmthp, &(defb[colcnt]), m_errhp, colcnt+1,
				res->m_cdefbuff[colcnt], res->m_colsize[colcnt], SQLT_CHR,
				&res->m_indbuff[colcnt], 0, 0, OCI_DEFAULT))
			{
				plogger->log(CAT_ALL, L_ERR, "Error in OCIDefineByPos SQLT_CHR\n", DispError());
				return NULL;
			}
			break;
		case SQLT_BLOB:
			if (OCIDescriptorAlloc((dvoid *) m_envhp, (dvoid **) &res->m_lob[colcnt],
				(ub4) OCI_DTYPE_LOB, (size_t) 0, (dvoid **) 0))
			{
				plogger->log(CAT_ALL, L_ERR, "Error OCIDescriptorAlloc %s\n", DispError());
				return NULL;
			}
			if (OCIDefineByPos(stmthp, &(defb[colcnt]), m_errhp, colcnt+1,
				&res->m_lob[colcnt], (sb4)-1, SQLT_BLOB,
				&res->m_indbuff[colcnt], 0, 0, OCI_DEFAULT))
			{
				plogger->log(CAT_ALL, L_ERR, "Error in OCIDefineByPos SQLT_BLOB\n", DispError());
				return NULL;
			}
			break;
		case SQLT_NUM:
			if (OCIDefineByPos(stmthp, &(defb[colcnt]), m_errhp, colcnt+1,
				&res->m_idefbuff[colcnt], sizeof(res->m_idefbuff[0]), SQLT_INT,
				&res->m_indbuff[colcnt], 0, 0, OCI_DEFAULT))
			{
				plogger->log(CAT_ALL, L_ERR, "Error in OCIDefineByPos SQLT_INT\n", DispError());
				return NULL;
			}
			break;
		case SQLT_STR:
			res->m_colsize[colcnt] = colsize;
			res->m_cdefbuff[colcnt] = new char[colsize+1];
			res->m_cdefbuff[colcnt][colsize] = 0;
			if (OCIDefineByPos(stmthp, &(defb[colcnt]), m_errhp, colcnt+1,
				res->m_cdefbuff[colcnt], res->m_colsize[colcnt], SQLT_STR,
				&res->m_indbuff[colcnt], 0, 0, OCI_DEFAULT))
			{
				plogger->log(CAT_ALL, L_ERR, "Error in OCIDefineByPos SQLT_CHR\n", DispError());
				return NULL;
			}
			OCIAttrSet(&(defb[colcnt]), OCI_HTYPE_BIND, (dvoid*) &csid, 0,
				OCI_ATTR_CHARSET_ID, m_errhp);
			break;
		default:
			plogger->log(CAT_ALL, L_ERR, "Unknown column type: %d\n", coltype);
			return NULL;
		}
/*		if (OCIDefineArrayOfStruct(m_defb[colcnt], m_errhp,
			res->m_colsize[colcnt], sizeof(res->m_indbuff[0][0]), 0, 0));
		{
		}
*/
		colcnt++;
	}
	res->m_ncols = colcnt;
	res->m_limit = query->m_limit;
	delete query;
	return new COrclSQLAnswer(m_svchp, stmthp, m_errhp, res);
}

ULONG COrclSQLDatabase::sql_queryw1(CSQLQuery *query, int* err)
{
	sword status;
	char *qbuf = query->MakeQuery();
//	plogger->log(CAT_SQL, L_DEBUG, "w1->%s\n", qbuf);
	ub2 stmt_type;
	OCIStmt	*stmthp;
	if ((status = OCIHandleAlloc((dvoid *)m_envhp, (dvoid **) &stmthp, OCI_HTYPE_STMT, 0, NULL)) && status != OCI_SUCCESS_WITH_INFO)
	{
		plogger->log(CAT_ALL, L_ERR, "Error OCIHandleAlloc %s\n", DispError());
		return 0;
	}
	if ((status = OCIStmtPrepare(stmthp, m_errhp, (text *)qbuf, strlen(qbuf), OCI_NTV_SYNTAX, OCI_DEFAULT)) && status != OCI_SUCCESS_WITH_INFO)
	{
		plogger->log(CAT_ALL, L_ERR, "Error OCIStmtPrepare %s\n", DispError());
		return 0;
	}
	if (((COrclSQLQuery*)query)->BindParam(stmthp, m_errhp, m_envhp))
	{
		plogger->log(CAT_ALL, L_ERR, "%s\n", DispError());
	}
	if (OCIAttrGet(stmthp, OCI_HTYPE_STMT, &stmt_type, 0, OCI_ATTR_STMT_TYPE, m_errhp))
	{
		plogger->log(CAT_ALL, L_ERR, "Error OCIAttrGet %s\n", DispError());
		return 0;
	}
	if ((status = OCIStmtExecute(m_svchp, stmthp, m_errhp, 1, 0, NULL, NULL, OCI_DEFAULT)) && status != OCI_SUCCESS_WITH_INFO)
	{
		sb4 errcode;
		OCIErrorGet(m_errhp, 1, NULL,  &errcode,  (text *)NULL, 0, OCI_HTYPE_ERROR);

		if (errcode != 1)
		{
			plogger->log(CAT_ALL, L_ERR, "OCIStmtExecute: %s %s\n", DispError(), qbuf);
			return 0;
		}
		else
		{
			if (err)
			{
				*err = GetError(1);
			}
		}
		if (OCIHandleFree((dvoid *) stmthp, (ub4) OCI_HTYPE_STMT))
		{
			plogger->log(CAT_ALL, L_ERR, "Error OCIAttrGet %s\n", DispError());
		}

		delete query;
		return 0;
	}
	if (err) *err = 0;
	if (((COrclSQLQuery*)query)->WriteBlob(m_svchp, m_errhp))
	{
		plogger->log(CAT_ALL, L_ERR, "%s\n", DispError());
	}
	OCITransCommit(m_svchp, m_errhp, (ub4) 0);
	if (OCIHandleFree((dvoid *) stmthp, (ub4) OCI_HTYPE_STMT))
	{
		plogger->log(CAT_ALL, L_ERR, "Error OCIAttrGet %s\n", DispError());
	}
	ULONG id = ((COrclSQLQuery*)query)->GetOutvalue();
	delete query;
	return id;
}
int COrclSQLDatabase::sql_queryw2(CSQLQuery *query, int* err)
{
	sword status;
	ub2 stmt_type;
	OCIStmt	*stmthp;
	if (!query)
	{
		return 0;
	}
	char *qbuf = query->MakeQuery();
//	plogger->log(CAT_SQL, L_DEBUG, "w2->%s\n", qbuf);
	if ((status = OCIHandleAlloc((dvoid *)m_envhp, (dvoid **) &stmthp, OCI_HTYPE_STMT, 0, NULL)) && status != OCI_SUCCESS_WITH_INFO)
	{
		plogger->log(CAT_ALL, L_ERR, "Error OCIHandleAlloc %s\n", DispError());
		return 1;
	}
	if ((status = OCIStmtPrepare(stmthp, m_errhp, (text *)qbuf, strlen(qbuf), OCI_NTV_SYNTAX, OCI_DEFAULT)) && status != OCI_SUCCESS_WITH_INFO)
	{
		plogger->log(CAT_ALL, L_ERR, "Error OCIStmtPrepare %s\n", DispError());
		return 1;
	}
	if (((COrclSQLQuery*)query)->BindParam(stmthp, m_errhp, m_envhp))
	{
		plogger->log(CAT_ALL, L_ERR, "%s\n", DispError());
	}
	if (OCIAttrGet(stmthp, OCI_HTYPE_STMT, &stmt_type, 0, OCI_ATTR_STMT_TYPE, m_errhp))
	{
		plogger->log(CAT_ALL, L_ERR, "Error OCIAttrGet %s\n", DispError());
		return 1;
	}
	if ((status = OCIStmtExecute(m_svchp, stmthp, m_errhp, 1, 0, NULL, NULL, OCI_DEFAULT)) && status != OCI_SUCCESS_WITH_INFO)
	{
		sb4 errcode;
		OCIErrorGet(m_errhp, 1, NULL,  &errcode,  (text *)NULL, 0, OCI_HTYPE_ERROR);
		plogger->log(CAT_ALL, L_ERR, "Error OCIStmtExecute %s %s\n", DispError(), qbuf);
		if (err)
		{
			*err = GetError(errcode);
		}
		if (OCIHandleFree((dvoid *) stmthp, (ub4) OCI_HTYPE_STMT))
		{
			plogger->log(CAT_ALL, L_ERR, "Error OCIAttrGet %s\n", DispError());
		}
		delete query;
		return 1;
	}
	if (err) err = 0;
	if (((COrclSQLQuery*)query)->WriteBlob(m_svchp, m_errhp))
	{
		plogger->log(CAT_ALL, L_ERR, "%s\n", DispError());
	}
	OCITransCommit(m_svchp, m_errhp, (ub4) 0);
	if (OCIHandleFree((dvoid *) stmthp, (ub4) OCI_HTYPE_STMT))
	{
		plogger->log(CAT_ALL, L_ERR, "Error OCIAttrGet %s\n", DispError());
	}
	delete query;
	return 0;
}

int COrclSQLDatabase::Connect(const char* host, const char* user, const char* password, const char* dbname, int port=0)
{
	// Create single connection
	return OCILogon(m_envhp, m_errhp, &m_svchp, (text *)user, strlen(user), (text *)password, strlen(password), (text *)dbname, strlen(dbname)) == OCI_SUCCESS;
/*	COrclSQLQuery sqlquery;
	CSQLQuery *query = sqlquery.SetQuery("ALTER SESSION SET SQL_TRACE = TRUE");
	sql_query1(query);
	return 1;
*/
}

char* COrclSQLDatabase::DispError()
{
	sb4 errcode;
	m_str_error[0] = 0;
	OCIErrorGet(m_errhp, 1, NULL,  &errcode,  (text *)m_str_error, 256, OCI_HTYPE_ERROR);
	return m_str_error;
}

void COrclSQLDatabase::CloseDb()
{
	OCILogoff(m_svchp, m_errhp);
}


// Functions for use after dlopen()ing
PSQLDatabase newDataBase(void)
{
	return new COrclSQLDatabase;
}

PSQLDatabaseI newDataBaseI(void)
{
	return new COrclSQLDatabaseI;
}
