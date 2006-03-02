///////////////////////////////////////////////////////////////////////////////
//
//	File    : $Id$
//	Subject : IBPP, Initialization of the library
//
///////////////////////////////////////////////////////////////////////////////
//
//	The contents of this file are subject to the IBPP License Version 1.0
//	(the "License"); you may not use this file except in compliance with the
//	License.  You may obtain a copy of the License at http://www.ibpp.org or
//	in the 'license.txt' file which must have been distributed along with this
//	file.
//
//	This software, distributed under the License, is distributed on an "AS IS"
//	basis, WITHOUT WARRANTY OF ANY KIND, either express or implied.  See the
//	License for the specific language governing rights and limitations
//	under the License.
//
//	Contributor(s):
//
///////////////////////////////////////////////////////////////////////////////
//
//	COMMENTS
//	* Tabulations should be set every four characters when editing this file.
//
///////////////////////////////////////////////////////////////////////////////

#ifdef _MSC_VER
#pragma warning(disable: 4786 4996)
#ifndef _DEBUG
#pragma warning(disable: 4702)
#endif
#endif

#include "ibpp.h"
#include "_internals.h"

#ifdef HAS_HDRSTOP
#pragma hdrstop
#endif

#ifdef IBPP_WINDOWS
// New (optional) Registry Keys introduced by Firebird Server 1.5
#define REG_KEY_ROOT_INSTANCES	"SOFTWARE\\Firebird Project\\Firebird Server\\Instances"
#define FB_DEFAULT_INSTANCE	  	"DefaultInstance"
#endif

namespace ibpp_internals
{
	GDS gds;	// Global unique GDS instance

#ifdef _DEBUG
	std::ostream& operator<< (std::ostream& a, flush_debug_stream_type)
	{
		if (std::stringstream* p = dynamic_cast<std::stringstream*>(&a))
		{
#ifdef IBPP_WINDOWS
			::OutputDebugString(("IBPP: " + p->str() + "\n").c_str());
#endif
			p->str("");
		}
		return a;
	}
#endif	// _DEBUG

}

using namespace ibpp_internals;

GDS* GDS::Call(void)
{
	// Let's load the CLIENT library, if it is not already loaded.
	// The load is guaranteed to be done only once per application.

	if (! mReady)
	{
#ifdef IBPP_WINDOWS

		// Let's load the FBCLIENT.DLL or GDS32.DLL, we won't release it.
		// Windows will do that for us when the executable will terminate.

		char fbdll[MAX_PATH];
		HKEY hkey_instances;

		// Try to load FBCLIENT.DLL from each of the additional optional paths
		// that may have been specified through ClientLibSearchPaths().
		mHandle = 0;

		std::string::size_type pos = 0;
		while (pos < mSearchPaths.size())
		{
			std::string::size_type newpos = mSearchPaths.find(';', pos);

			std::string path;
			if (newpos == std::string::npos) path = mSearchPaths.substr(pos);
			else path = mSearchPaths.substr(pos, newpos-pos);
				
			if (path.size() >= 1)
			{
				if (path[path.size()-1] != '\\') path += '\\';
				path.append("fbclient.dll");
				mHandle = LoadLibrary(path.c_str());
				if (mHandle != 0 || newpos == std::string::npos) break;
			}
			pos = newpos + 1;
		}
		
		if (mHandle == 0)
		{
			// Try to load FBCLIENT.DLL from the current application location.  This
			// is a usefull step for applications using the embedded version of FB
			// or a local copy (for whatever reasons) of the dll.
			
			int len = GetModuleFileName(NULL, fbdll, sizeof(fbdll));
			if (len != 0)
			{
				// Get to the last '\' (this one precedes the filename part).
				// There is always one after a success call to GetModuleFileName().
				char* p = fbdll + len;
				do {--p;} while (*p != '\\');
				*p = '\0';
				lstrcat(fbdll, "\\fbembed.dll");// Local copy could be named fbembed.dll
				mHandle = LoadLibrary(fbdll);
				if (mHandle == 0)
				{
					*p = '\0';
					lstrcat(fbdll, "\\fbclient.dll");	// Or possibly renamed fbclient.dll
					mHandle = LoadLibrary(fbdll);
				}
			}
		}

		if (mHandle == 0)
		{
			// Try to locate FBCLIENT.DLL through the optional FB 1.5 registry
			// key. Note that this will have to be enhanced in a later version
			// of IBPP in order to be able to select among multiple instances
			// that Firebird Server version > 1.5 might introduce.

			if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, REG_KEY_ROOT_INSTANCES, 0,
				KEY_READ, &hkey_instances) == ERROR_SUCCESS)
			{
				DWORD keytype;
				DWORD buflen = sizeof(fbdll);
				if (RegQueryValueEx(hkey_instances, FB_DEFAULT_INSTANCE, 0,
						&keytype, reinterpret_cast<UCHAR*>(fbdll),
							&buflen) == ERROR_SUCCESS && keytype == REG_SZ)
				{
					lstrcat(fbdll, "bin\\fbclient.dll");
					mHandle = LoadLibrary(fbdll);
				}
				RegCloseKey(hkey_instances);
			}
		}
		
		if (mHandle == 0)
		{
			// Let's try from the PATH and System directories
			mHandle = LoadLibrary("fbclient.dll");
			if (mHandle == 0)
			{
				// Not found. Last try : attemps loading gds32.dll from PATH and
				// System directories
				mHandle = LoadLibrary("gds32.dll");
				if (mHandle == 0)
					throw LogicExceptionImpl("GDS::Call()",
						_("Can't find or load FBCLIENT.DLL or GDS32.DLL"));
			}
		}
#endif

		mGDSVersion = 60;

		// Get the entry points that we need

#ifdef IBPP_WINDOWS
#define IB_ENTRYPOINT(X) \
			if ((m_##X = (proto_##X*)GetProcAddress(mHandle, "isc_"#X)) == 0) \
				throw LogicExceptionImpl("GDS:gds()", _("Entry-point isc_"#X" not found"))
#endif
#ifdef IBPP_UNIX
/* TODO : perform a late-bind on unix --- not so important, well I think (OM) */
#define IB_ENTRYPOINT(X) m_##X = (proto_##X*)isc_##X 
#endif

		IB_ENTRYPOINT(create_database);
		IB_ENTRYPOINT(attach_database);
		IB_ENTRYPOINT(detach_database);
		IB_ENTRYPOINT(drop_database);
		IB_ENTRYPOINT(database_info);
		IB_ENTRYPOINT(open_blob2);
		IB_ENTRYPOINT(create_blob2);
		IB_ENTRYPOINT(close_blob);
		IB_ENTRYPOINT(cancel_blob);
		IB_ENTRYPOINT(get_segment);
		IB_ENTRYPOINT(put_segment);
		IB_ENTRYPOINT(blob_info);
		IB_ENTRYPOINT(array_lookup_bounds);
		IB_ENTRYPOINT(array_get_slice);
		IB_ENTRYPOINT(array_put_slice);
		IB_ENTRYPOINT(vax_integer);
		IB_ENTRYPOINT(sqlcode);
		IB_ENTRYPOINT(sql_interprete);
		IB_ENTRYPOINT(interprete);
		IB_ENTRYPOINT(que_events);
		IB_ENTRYPOINT(cancel_events);
		IB_ENTRYPOINT(start_multiple);
		IB_ENTRYPOINT(commit_transaction);
		IB_ENTRYPOINT(commit_retaining);
		IB_ENTRYPOINT(rollback_transaction);
		IB_ENTRYPOINT(rollback_retaining);
		IB_ENTRYPOINT(dsql_execute_immediate);
		IB_ENTRYPOINT(dsql_allocate_statement);
		IB_ENTRYPOINT(dsql_describe);
		IB_ENTRYPOINT(dsql_describe_bind);
		IB_ENTRYPOINT(dsql_prepare);
		IB_ENTRYPOINT(dsql_execute);
		IB_ENTRYPOINT(dsql_execute2);
		IB_ENTRYPOINT(dsql_fetch);
		IB_ENTRYPOINT(dsql_free_statement);
		IB_ENTRYPOINT(dsql_set_cursor_name);
		IB_ENTRYPOINT(dsql_sql_info);
		
		IB_ENTRYPOINT(service_attach);
		IB_ENTRYPOINT(service_detach);
		IB_ENTRYPOINT(service_start);
		IB_ENTRYPOINT(service_query);

		//IB_ENTRYPOINT(add_user);
		//IB_ENTRYPOINT(modify_user);
		//IB_ENTRYPOINT(delete_user);
		//IB_ENTRYPOINT(decode_date);
		//IB_ENTRYPOINT(encode_date);
		//IB_ENTRYPOINT(decode_sql_date);
		//IB_ENTRYPOINT(decode_sql_time);
		//IB_ENTRYPOINT(decode_timestamp);
		//IB_ENTRYPOINT(encode_sql_date);
		//IB_ENTRYPOINT(encode_sql_time);
		//IB_ENTRYPOINT(encode_timestamp);

		mReady = true;
	}

	return this;
}

namespace IBPP
{

	bool CheckVersion(uint32_t AppVersion)
	{
		//(void)gds.Call(); 		// Just call it to trigger the initialization
		return (AppVersion & 0xFFFFFF00) ==
				(IBPP::Version & 0xFFFFFF00) ? true : false;
	}

	int GDSVersion(void)
	{
		return gds.Call()->mGDSVersion;
	}

#ifdef IBPP_WINDOWS
	void ClientLibSearchPaths(const std::string& paths)
	{
		gds.mSearchPaths.assign(paths);
	}
#else
	void ClientLibSearchPaths(const std::string&)
	{
	}
#endif
	
	//	Factories for our Interface objects

	IService* ServiceFactory(const std::string& ServerName,
				const std::string& UserName, const std::string& UserPassword)
	{
		(void)gds.Call();			// Triggers the initialization, if needed
		return new ServiceImpl(ServerName, UserName, UserPassword);
	}

	IDatabase* DatabaseFactory(const std::string& ServerName,
		const std::string& DatabaseName, const std::string& UserName,
		const std::string& UserPassword, const std::string& RoleName,
		const std::string& CharSet, const std::string& CreateParams)
	{
		(void)gds.Call();			// Triggers the initialization, if needed
		return new DatabaseImpl(ServerName, DatabaseName, UserName,
					UserPassword, RoleName, CharSet, CreateParams);
	}

	ITransaction* TransactionFactory(IDatabase* db, TAM am,
					TIL il, TLR lr, TFF flags)
	{
		(void)gds.Call();			// Triggers the initialization, if needed
		DatabaseImpl* dbimpl = dynamic_cast<DatabaseImpl*>(db);
		return new TransactionImpl(dbimpl, am, il, lr, flags);
	}

	/*
	IRow* RowFactory(int dialect)
	{
		(void)gds.Call();			// Triggers the initialization, if needed
		return new RowImpl(dialect);
	}
	*/

	IStatement* StatementFactory(IDatabase* db, ITransaction* tr,
		const std::string& sql)
	{
		(void)gds.Call();			// Triggers the initialization, if needed
		DatabaseImpl* dbimpl = dynamic_cast<DatabaseImpl*>(db);
		TransactionImpl* trimpl = dynamic_cast<TransactionImpl*>(tr);
		return new StatementImpl(dbimpl, trimpl, sql);
	}

	IBlob* BlobFactory(IDatabase* db, ITransaction* tr)
	{
		(void)gds.Call();			// Triggers the initialization, if needed
		DatabaseImpl* dbimpl = dynamic_cast<DatabaseImpl*>(db);
		TransactionImpl* trimpl = dynamic_cast<TransactionImpl*>(tr);
		return new BlobImpl(dbimpl, trimpl);
	}

	IArray* ArrayFactory(IDatabase* db, ITransaction* tr)
	{
		(void)gds.Call();			// Triggers the initialization, if needed
		DatabaseImpl* dbimpl = dynamic_cast<DatabaseImpl*>(db);
		TransactionImpl* trimpl = dynamic_cast<TransactionImpl*>(tr);
		return new ArrayImpl(dbimpl, trimpl);
	}

}

//
//	EOF
//
