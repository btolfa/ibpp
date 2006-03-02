///////////////////////////////////////////////////////////////////////////////
//
//	File    : $Id$
//	Subject : IBPP, Database class implementation
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

#include <algorithm>

using namespace ibpp_internals;

//	(((((((( OBJECT INTERFACE IMPLEMENTATION ))))))))

void TransactionImpl::AttachDatabase(IBPP::IDatabase* db,
	IBPP::TAM am, IBPP::TIL il, IBPP::TLR lr, IBPP::TFF flags)
{
	if (mHandle != 0)
		throw LogicExceptionImpl("Transaction::AttachDatabase",
				_("Can't attach a Database if Transaction started."));
	if (db == 0)
		throw LogicExceptionImpl("Transaction::AttachDatabase",
				_("Can't attach a null Database."));

	DatabaseImpl* dbi = dynamic_cast<DatabaseImpl*>(db);
	if (dbi == 0)
		throw LogicExceptionImpl("Transaction::AttachDatabase",
				_("Illegal parameter (database)"));

	mDatabases.push_back(dbi);

	// Prepare a new TPB
	TPB* tpb = new TPB;
    if (am == IBPP::amRead) tpb->Insert(isc_tpb_read);
    else tpb->Insert(isc_tpb_write);

	switch (il)
	{
		case IBPP::ilConsistency :		tpb->Insert(isc_tpb_consistency); break;
		case IBPP::ilReadDirty :		tpb->Insert(isc_tpb_read_committed);
						    	    	tpb->Insert(isc_tpb_rec_version); break;
		case IBPP::ilReadCommitted :	tpb->Insert(isc_tpb_read_committed);
										tpb->Insert(isc_tpb_no_rec_version); break;
		default :						tpb->Insert(isc_tpb_concurrency); break;
	}

    if (lr == IBPP::lrNoWait) tpb->Insert(isc_tpb_nowait);
    else tpb->Insert(isc_tpb_wait);

	if (flags & IBPP::tfIgnoreLimbo)	tpb->Insert(isc_tpb_ignore_limbo);
	if (flags & IBPP::tfAutoCommit)		tpb->Insert(isc_tpb_autocommit);
	if (flags & IBPP::tfNoAutoUndo)		tpb->Insert(isc_tpb_no_auto_undo);

	mTPBs.push_back(tpb);

	// Signals the Database object that it has been attached to the Transaction
	dbi->AttachTransaction(this);
}

void TransactionImpl::DetachDatabase(IBPP::IDatabase* db)
{
	if (mHandle != 0)
		throw LogicExceptionImpl("Transaction::DetachDatabase",
				_("Can't detach a Database if Transaction started."));
	if (db == 0)
		throw LogicExceptionImpl("Transaction::DetachDatabase",
				_("Can't detach a null Database."));

	DatabaseImpl* dbi = dynamic_cast<DatabaseImpl*>(db);
	if (dbi == 0)
		throw LogicExceptionImpl("Transaction::DetachDatabase",
				_("Illegal parameter (database)"));

	std::vector<DatabaseImpl*>::iterator pos =
		std::find(mDatabases.begin(), mDatabases.end(), dbi);
	if (pos != mDatabases.end())
	{
		size_t index = pos - mDatabases.begin();
		TPB* tpb = mTPBs[index];
		mDatabases.erase(pos);
		mTPBs.erase(mTPBs.begin()+index);
		delete tpb;
	}

	// Signals the Database object that it has been detached from the Transaction
	dbi->DetachTransaction(this);
}

void TransactionImpl::AddReservation(IBPP::IDatabase* db,
	const std::string& table, IBPP::TTR tr)
{
	if (mHandle != 0)
		throw LogicExceptionImpl("Transaction::AddReservation",
				_("Can't add table reservation if Transaction started."));
	if (db == 0)
		throw LogicExceptionImpl("Transaction::AddReservation",
				_("Null IDatabase pointer detected."));

	// Find the TPB associated with this database
	std::vector<DatabaseImpl*>::iterator pos =
		std::find(mDatabases.begin(), mDatabases.end(), dynamic_cast<DatabaseImpl*>(db));
	if (pos != mDatabases.end())
	{
		size_t index = pos - mDatabases.begin();
		TPB* tpb = mTPBs[index];
		
		// Now add the reservations to the TPB
		switch (tr)
		{
			case IBPP::trSharedWrite :
					tpb->Insert(isc_tpb_lock_write);
					tpb->Insert(table);
					tpb->Insert(isc_tpb_shared);
					break;
			case IBPP::trSharedRead :
					tpb->Insert(isc_tpb_lock_read);
					tpb->Insert(table);
					tpb->Insert(isc_tpb_shared);
					break;
			case IBPP::trProtectedWrite :
					tpb->Insert(isc_tpb_lock_write);
					tpb->Insert(table);
					tpb->Insert(isc_tpb_protected);
					break;
			case IBPP::trProtectedRead :
					tpb->Insert(isc_tpb_lock_read);
					tpb->Insert(table);
					tpb->Insert(isc_tpb_protected);
					break;
			default :
					throw LogicExceptionImpl("Transaction::AddReservation",
						_("Illegal TTR value detected."));
		}
	}
	else throw LogicExceptionImpl("Transaction::AddReservation",
			_("The database connection you specified is not attached to this transaction."));
}

void TransactionImpl::Start(void)
{
	if (mHandle != 0) return;	// Already started anyway

	if (mDatabases.empty())
		throw LogicExceptionImpl("Transaction::Start", _("No Database is attached."));

	struct ISC_TEB
	{
		ISC_LONG* db_ptr;
		ISC_LONG tpb_len;
		char* tpb_ptr;
	} * teb = new ISC_TEB[mDatabases.size()];

	unsigned i;
	for (i = 0; i < mDatabases.size(); i++)
	{
		if (mDatabases[i]->GetHandle() == 0)
		{
			// All Databases must be connected to Start the transaction !
			delete [] teb;
			throw LogicExceptionImpl("Transaction::Start",
					_("All attached Database should have been connected."));
		}
		teb[i].db_ptr = (ISC_LONG*) mDatabases[i]->GetHandlePtr();
		teb[i].tpb_len = mTPBs[i]->Size();
		teb[i].tpb_ptr = mTPBs[i]->Self();
	}

	IBS status;
	(*gds.Call()->m_start_multiple)(status.Self(), &mHandle, (short)mDatabases.size(), teb);
	delete [] teb;
	if (status.Errors())
	{
		mHandle = 0;	// Should be, but better be sure...
		throw SQLExceptionImpl(status, "Transaction::Start");
	}
}

void TransactionImpl::Commit(void)
{
	if (mHandle == 0)
		throw LogicExceptionImpl("Transaction::Commit", _("Transaction is not started."));
		
	IBS status;

	(*gds.Call()->m_commit_transaction)(status.Self(), &mHandle);
	if (status.Errors())
		throw SQLExceptionImpl(status, "Transaction::Commit");
	mHandle = 0;	// Should be, better be sure

	size_t i;
	for (i = mStatements.size(); i != 0; i--)
		try { mStatements[i-1]->CursorFree(); }
			catch (IBPP::Exception&) { }
}

void TransactionImpl::CommitRetain(void)
{
	if (mHandle == 0)
		throw LogicExceptionImpl("Transaction::CommitRetain", _("Transaction is not started."));

	IBS status;

	(*gds.Call()->m_commit_retaining)(status.Self(), &mHandle);
	if (status.Errors())
		throw SQLExceptionImpl(status, "Transaction::CommitRetain");
}

void TransactionImpl::Rollback(void)
{
	if (mHandle == 0) return;	// Transaction not started anyway

	IBS status;

	(*gds.Call()->m_rollback_transaction)(status.Self(), &mHandle);
	if (status.Errors())
		throw SQLExceptionImpl(status, "Transaction::Rollback");
	mHandle = 0;	// Should be, better be sure

	size_t i;
	for (i = mStatements.size(); i != 0; i--)
		try { mStatements[i-1]->CursorFree(); }
			catch (IBPP::Exception&) { }
}

void TransactionImpl::RollbackRetain(void)
{
	if (mHandle == 0)
		throw LogicExceptionImpl("Transaction::RollbackRetain", _("Transaction is not started."));

	IBS status;

	(*gds.Call()->m_rollback_retaining)(status.Self(), &mHandle);
	if (status.Errors())
		throw SQLExceptionImpl(status, "Transaction::RollbackRetain");
}

IBPP::ITransaction* TransactionImpl::AddRef(void)
{
	ASSERTION(mRefCount >= 0);
	++mRefCount;
	return this;
}

void TransactionImpl::Release(IBPP::ITransaction*& Self)
{
	if (this != dynamic_cast<TransactionImpl*>(Self))
		throw LogicExceptionImpl("Transaction::Release", _("Invalid Release()"));

	ASSERTION(mRefCount >= 0);

	--mRefCount;
	if (mRefCount <= 0) delete this;
	Self = 0;
}

//	(((((((( OBJECT INTERNAL METHODS ))))))))

void TransactionImpl::Init(void)
{
	mHandle = 0;
	mDatabases.clear();
	mTPBs.clear();
	mStatements.clear();
 	mBlobs.clear();
	mArrays.clear();
}

void TransactionImpl::AttachStatement(StatementImpl* st)
{
	if (st == 0)
		throw LogicExceptionImpl("Transaction::AttachStatement",
					_("Can't attach a 0 Statement object."));

	mStatements.push_back(st);
}

void TransactionImpl::DetachStatement(StatementImpl* st)
{
	if (st == 0)
		throw LogicExceptionImpl("Transaction::DetachStatement",
				_("Can't detach a 0 Statement object."));

	mStatements.erase(std::find(mStatements.begin(), mStatements.end(), st));
}

void TransactionImpl::AttachBlob(BlobImpl* bb)
{
	if (bb == 0)
		throw LogicExceptionImpl("Transaction::AttachBlob",
					_("Can't attach a 0 BlobImpl object."));

	mBlobs.push_back(bb);
}

void TransactionImpl::DetachBlob(BlobImpl* bb)
{
	if (bb == 0)
		throw LogicExceptionImpl("Transaction::DetachBlob",
				_("Can't detach a 0 BlobImpl object."));

	mBlobs.erase(std::find(mBlobs.begin(), mBlobs.end(), bb));
}

void TransactionImpl::AttachArray(ArrayImpl* ar)
{
	if (ar == 0)
		throw LogicExceptionImpl("Transaction::AttachArray",
					_("Can't attach a 0 ArrayImpl object."));

	mArrays.push_back(ar);
}

void TransactionImpl::DetachArray(ArrayImpl* ar)
{
	if (ar == 0)
		throw LogicExceptionImpl("Transaction::DetachArray",
				_("Can't detach a 0 ArrayImpl object."));

	mArrays.erase(std::find(mArrays.begin(), mArrays.end(), ar));
}

///////////////////////////////////////////////////////////////////////////////
//	INTERNAL (HIDDEN) IMPLEMENTATION
//	PUBLIC METHODS
///////////////////////////////////////////////////////////////////////////////

TransactionImpl::TransactionImpl(DatabaseImpl* db,
	IBPP::TAM am, IBPP::TIL il, IBPP::TLR lr, IBPP::TFF flags)
	: mRefCount(0)
{
	Init();
	AttachDatabase(db, am, il, lr, flags);
}

TransactionImpl::~TransactionImpl()
{
	// Rollback the transaction if it was Started
	if (Started()) Rollback();

	// Let's detach cleanly all Blobs from this Transaction.
	// No Blob object can still maintain pointers to this
	// Transaction which is disappearing.
	//
	// We use a reverse traversal of the array to avoid loops.
	// The array shrinks on each loop (mBbCount decreases).
	// And during the deletion, there is a packing of the array through a
	// copy of elements from the end to the beginning of the array.
	while (mBlobs.size() > 0)
		mBlobs.back()->DetachTransaction();

	// Let's detach cleanly all Arrays from this Transaction.
	// No Array object can still maintain pointers to this
	// Transaction which is disappearing.
	while (mArrays.size() > 0)
		mArrays.back()->DetachTransaction();

	// Let's detach cleanly all Statements from this Transaction.
	// No Statement object can still maintain pointers to this
	// Transaction which is disappearing.
	while (mStatements.size() > 0)
		mStatements.back()->DetachTransaction();

	// Very important : let's detach cleanly all Databases from this
	// Transaction. No Database object can still maintain pointers to this
	// Transaction which is disappearing.
	while (mDatabases.size() > 0)
	{
		size_t i = mDatabases.size()-1;
		DetachDatabase(mDatabases[i]);	// <-- remove link to database from mTPBs
										// array and destroy TPB object
										// Fixed : Maxim Abrashkin on 12 Jun 2002
		//mDatabases.back()->DetachTransaction(this);
	}
}

//
//	EOF
//
