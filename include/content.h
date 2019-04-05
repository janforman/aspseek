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

/*  $Id: content.h,v 1.35 2003/03/18 20:49:25 kir Exp $
    Author : Alexander F. Avdonkin
*/

#ifndef _CONTENT_H_
#define _CONTENT_H_

#include <string.h>
#include <string>
#include "aspseek-cfg.h"
#include "defines.h"
#include "charsets.h"
#include "buddy.h"
#include "my_hash_map"
#include "my_hash_set"
#ifdef UNICODE
#include "ucharset.h"
#endif

using std::string;

class CWordCache;
class CServer;

//	Inheritance tree of classes, used for URL content parsing
//
//	CParsedContent
//	|
//	+---CWordContent
//	+---CUrlContent

typedef CFixedString<3> CLangString;

namespace STL_EXT_NAMESPACE {
	template <> struct hash<CLangString>
	{
		size_t operator()(const CLangString& __s) const
		{
			return __stl_hash_string(__s.c_str());
		}
	};
}

// Class used for count stopwords in document to determine language.
class CLangContent: public hash_map<CLangString, int>
{
public:
	void addlang(const char *lang)
	{
		CLangString p(lang);
		(*this)[p]++;
	}

	const char *getlang()
	{
		int max = 0;
		const char *p=NULL;

		for (iterator it=begin(); it!=end(); it++)
		{
			if (it->second > max)
			{
				max = it->second;
				p = it->first.c_str();
			}
		}
		return p;
	}
};

/** Base class in tree of classes used for URL content parsing */
class CParsedContent
{
public:
	CWordCache* m_cache;	///< Pointer to the main indexing object
	int m_charset;		///< Charset of this content
	CLangContent *m_langmap;
#ifdef UNICODE
	CCharsetB* m_ucharset;
#endif
	string m_lastmod;	///< Value of Last-Modified META in HEAD

public:
	CParsedContent()
	{
		m_charset = CHARSET_USASCII;
		m_langmap = new CLangContent;
#ifdef UNICODE
		m_ucharset = &baseCharset;
#endif
	}
	virtual void RecodeContent(char *str, int from, int to )
	{
#ifndef UNICODE
		if (m_charset != CHARSET_USASCII)
			return;

		if (Recode(str, from , to))
		{
			m_charset = to;
		}
#endif
	}
#ifdef UNICODE
	int ParseText(WORD* content, WORD& pos, CServer* CurSrv, int fontsize=0);
	virtual void AddWord1(const WORD* word, WORD pos, int fontsize) = 0;
#else
	virtual void AddWord1(const char* word, WORD pos, int fontsize) = 0;		///< Must add specified word
#endif
	virtual void SetParsed() {};					///< Sets flag, when parsing occurs
	virtual void InsertHref(ULONG hrID) {};				///< Inserts outgoing hyperlink ID
//	virtual void SetCharset(char* charset, char* content) {};	///< Sets charset of URL
	/// extracts words from specified text and stores them
	int ParseText(char* content, WORD& pos, CServer* CurSrv, int fontsize = 0);
	/// Adds word if it is not in stopwords
#ifdef UNICODE
	inline void AddWord(const WORD* word, WORD pos, int fontsize);
#else
	inline void AddWord(const char* word, WORD pos, int fontsize);
#endif
	virtual int ParseHead(char* content);	///< Parses only HTML HEAD
	virtual ~CParsedContent()
	{
		delete m_langmap;
	}

	const char *GetLang()
	{
		return m_langmap->getlang();
	}
	virtual void SetOriginalCharset(const char *charset)
	{
	}
	virtual void ClearW() {};

};


/**
 * Class used for storing words from URL content, taken from database.
 * Instance of this class stores only set of words without their positions.
 */
#ifdef UNICODE
class CWordContent : public hash_set<CUWord>, public CParsedContent
#else
class CWordContent : public hash_set<CWord>, public CParsedContent
#endif
{
public:
	void Parse(BYTE* content, const char* content_type, CWordCache* cache, CServer* curSrv, const char* charset);
	virtual int ParseHead(char* content) {return 0;};
	/// Just inserts encountered word in the set
#ifdef UNICODE
	virtual void SetOriginalCharset(const char *charset)
	{
		m_ucharset = GetUCharset(charset);
	}
	virtual void AddWord1(const WORD* word, WORD pos, int fontsize)
#else
	virtual void AddWord1(const char* word, WORD pos, int fontsize)
#endif
	{
		insert(word);
	}
	virtual void RecodeContent(char *str, int from, int to)
	{
#ifndef UNICODE
		if (CanRecode(from, to))
		{
			m_charset = to;
		}
#endif
	}
	virtual ~CWordContent() {};
};

/// Keeps some parameters of downloaded URL
class CUrlParam
{
public:
	ULONG m_totalCount;		///< Total count of words in the URL
	ULONG m_urlID;			///< URL ID of this URL
	ULONG m_siteID;			///< Site ID for this URL
	int m_parsed;			///< set to 1 if content was parsed since instaintiation
	ULONG m_redir;			///< URL ID where current URL is redirected or 0 if this URL is not redirected.
	ULONG m_oldRedir;
	ULONG m_new;
	ULONG m_lmod;
	int m_deleted;			///< set to 1 if server returned 404 error code
	int m_status;			///< HTTP status of this URL
	int m_period;			///< Next index time of this URL
	int m_tag;			///< Tag of URL, taken from config
	int m_docsize;			///< Total size of downloaded part of URL content, can be overwritten by Content-Length field of HTTP response header
	/** Document changing status.
	 * 0th bit is set to 1, if server returned any response
	 * 1st bit is set to 1 if server returned status other than 304
	 * 2nd bit is set to 1 if origin/clone changed
	 */
	int m_changed;
	string m_last_modified;		///< Last-Modified HTTP response field
	string m_etag;			///< ETag HTTP response field
	string m_txt;			///< Beginning of HTML body
	string m_title;			///< Beginning of HTML title
	string m_content_type;		///< Content-Type HTTP response field
	string m_charset;		///< Charset of this URL, taken from HTTP response or from HTML HEAD
	string m_keywords;		///< Beginning of HTML keywords
	string m_description;		///< Beginning of HTML description
	string m_crc;			///< MD5 checksum of URL content
	string m_lang;			///< Document language - not used now
	char* m_content;		///< Downloaded content of URL
	ULONG m_size;			///< Total size of downloaded part of URL content
#ifdef OPTIMIZE_CLONE
	int m_origin;			///< 0-clone, 1-origin
#endif
	CUrlParam()
	{
		m_totalCount = 0;
		m_changed = 0;
		m_deleted = 0;
		m_parsed = 0;
		m_redir = 0;
		m_content = NULL;
		m_size = 0;
#ifdef OPTIMIZE_CLONE
		m_origin = 0;
#endif
	}
};

typedef hash_set<ULONG> CULONGSet;

class CWordParam : public CWordBuddyVector
{
public:
	WORD m_fontsize;
public:
	CWordParam()
	{
		m_fontsize = 0;
	}
};

/** Class used for storing words and other information
 *  for downloaded URL. Instance of this class stores
 *  positions for each word.
 */

#ifdef UNICODE
class CUrlContent : public hash_map<CUWord, CWordParam>, public CParsedContent
#else
class CUrlContent : public hash_map<CWord, CWordParam>, public CParsedContent
#endif
{
public:
	CUrlParam m_url;	///< Different parameters of currently indexed URL
	CULONGSet m_hrefs;	///< Set of outgoing hyperlink IDs for this URL
	int m_follow;		///< Set to 0 if subsequent outgoing hyperlinks must not be followed
	CServer* m_curSrv;	///< "CServer" object for this URL
	CBuddyHeap m_heap;

public:
	CUrlContent()
	{
		m_follow = 1;
//		charset = NULL;
	}
	virtual ~CUrlContent()
	{
		// Save words and hyperlinks found in URL content
		Save();
	}
	virtual void SetParsed()
	{
		m_url.m_parsed = 1;
	}
#ifdef UNICODE
	virtual void AddWord1(const WORD* word, WORD pos, int fontsize)
#else
	virtual void AddWord1(const char* word, WORD pos, int fontsize)
#endif
	{
//		(*this)[word].push_back(pos);	// Add position for specified word
		CWordParam &wp = (*this)[word];
		wp.m_heap = &m_heap;
		wp.push_back(pos);
		wp.m_fontsize |= (fontsize & 0x3);
		m_url.m_totalCount++;
	}
	virtual void InsertHref(ULONG hrID)
	{
		m_hrefs.insert(hrID);	// Add outgoing hyperlink ID to the set
	}
	void Clear()
	{
		clear();
		m_url.m_totalCount = 0;
		m_url.m_parsed = 0;
		m_url.m_changed |= 8;
	}
	void ClearF()
	{
		clear();
		m_url.m_parsed = 1;
	}
	virtual void ClearW()
	{
		clear();
		m_url.m_totalCount = 0;
	}
//	virtual void SetCharset(char* charset, char* content);
	void Save();
	/// Stores specified parameters in the "m_url" member
	void UpdateLongUrl(int status, int period_, int changed, int size,
		int hint, char *last_modified, char *etag, char *text,
		char *title, char *content_type, char *charset, char *keywords,
		char *descript, char *digest, char *lang);
	void UpdateUrl(int status, int period_, int m_redir = 0);

	virtual void SetOriginalCharset(const char *charset)
	{
//		if(!m_url.m_charset.size())
			m_url.m_charset = charset;
#ifdef UNICODE
		m_ucharset = GetUCharset(charset);
#endif
	}
};

#endif
