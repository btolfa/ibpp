///////////////////////////////////////////////////////////////////////////////
//
//	File    : $Id$
//	Subject : IBPP, internal EPB class implementation
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
//	* EPB == Event Parameter Block, see Interbase jrd/alt.c and 6.0 C-API
//	* Tabulations should be set every four characters when editing this file.
//
//	SPECIAL WARNING COMMENT (by Olivier Mascia, 2000 Nov 12)
//	This Event Parameter Block handling is not publicly documented, per se, in
//	the ibase.h header file or in the IB 6.0 documentation. This documentation
//	suggests to use the API isc_event_block to construct such EPBs.
//	Unfortunately, this API takes a variable number of parameters to specify
//	the list of event names. In addition, the documentation warn on not using
//	more than 15 names. This is a sad limitation, partly because the maximum
//	number of parameters safely processed in such an API is very compiler
//	dependant and also because isc_event_counts() is designed to return counts
//	through the IB status vector which is a vector of 20 32-bits integers !
//	From reverse engineering of the isc_event_block() API in
//	source file jrd/alt.c (as available on fourceforge.net/project/InterBase as
//	of 2000 Nov 12), it looks like the internal format of those EPB is simple.
//	An EPB starts by a byte with value 1. A version identifier of some sort.
//	Then a small cluster is used for each event name. The cluster starts with
//	a byte for the length of the event name (no final '\0'). Followed by the N
//	characters of the name itself (no final '\0'). The cluster ends with 4 bytes
//	preset to 0.
//
//	SPECIAL CREDIT (July 2004) : this is a complete re-implementation of this
//	class, directly based on work by Val Samko.
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

using namespace ibpp_internals;

namespace { const size_t MAXEVENTNAMELEN = 127; };

void EPB::Define(const std::string& eventname, IBPP::EventInterface* objref)
{
	if (eventname.size() == 0)
		throw LogicExceptionImpl("EPB::Define", _("Zero length event names not permitted"));
	if (eventname.size() > MAXEVENTNAMELEN)
		throw LogicExceptionImpl("EPB::Define", _("Event name is too long"));

	// 1) Alloc or grow the buffers
	size_t prev_buffer_size = mEventBuffer.size();
	size_t needed = ((prev_buffer_size==0) ? 1 : 0) + eventname.length() + 5;
	// Initial alloc will require one more byte, we need 4 more bytes for
	// the count itself, and one byte for the string length prefix

	mEventBuffer.resize(mEventBuffer.size() + needed);
	mResultsBuffer.resize(mResultsBuffer.size() + needed);
	if (prev_buffer_size == 0)
		mEventBuffer[0] = mResultsBuffer[0] = 1; // First byte is a 'one'. Documentation ??

	// 2) Update the buffers (append)
	{
		Buffer::iterator it = mEventBuffer.begin() +
				((prev_buffer_size==0) ? 1 : prev_buffer_size); // Byte after current content
		*(it++) = static_cast<char>(eventname.length());
		it = std::copy(eventname.begin(), eventname.end(), it);
		*(it++) = 1; *(it++) = 0; *(it++) = 0; *it = 0; // We initialize the counts to 1
	}

	// copying new event to the results buffer to keep event_buffer_ and results_buffer_ consistant,
	// otherwise we might get a problem in `FireActions`
	// Val Samko, val@digiways.com
	std::copy(mEventBuffer.begin() + prev_buffer_size,
		mEventBuffer.end(), mResultsBuffer.begin() + prev_buffer_size);

	// 3) Alloc or grow the objref array and update the objref array (append)
	mObjectReferences.push_back(objref);
}

void EPB::Drop(const std::string& eventname)
{
	if (eventname.size() == 0)
		throw LogicExceptionImpl("EPB::Drop", _("Zero length event names not permitted"));
	if (eventname.size() > MAXEVENTNAMELEN)
		throw LogicExceptionImpl("EPB::Drop", _("Event name is too long"));

	if (mEventBuffer.size() <= 1) return;	// Nothing to do, but not an error

	// 1) Find the event in the buffers
	typedef EventBufferIterator<Buffer::iterator> EventIterator;
	EventIterator eit(mEventBuffer.begin()+1);
	EventIterator rit(mResultsBuffer.begin()+1);

	for (ObjRefs::iterator oit = mObjectReferences.begin();
			oit != mObjectReferences.end();
				++oit, ++eit, ++rit)
	{
		if (eventname != eit.get_name()) continue;
		
		// 2) Event found, remove it
		mEventBuffer.erase(eit.begin(), eit.end());
		mResultsBuffer.erase(rit.begin(), rit.end());
		mObjectReferences.erase(oit);
		break;
	}

	return;
}

void EPB::FireActions(IBPP::IDatabase* db)
{
	typedef EventBufferIterator<Buffer::iterator> EventIterator;
	EventIterator eit(mEventBuffer.begin()+1);
	EventIterator rit(mResultsBuffer.begin()+1);

	for (ObjRefs::iterator oit = mObjectReferences.begin();
		 oit != mObjectReferences.end();
			 ++oit, ++eit, ++rit)
	{
		if (eit == EventIterator(mEventBuffer.end())
			  || rit == EventIterator(mResultsBuffer.end()))
			throw LogicExceptionImpl("EPB::FireActions", _("Internal buffer size error"));
		uint32_t vnew = rit.get_count();
		uint32_t vold = eit.get_count();
		if (vnew > vold)
		{ // Fire the action
			try
			{
				(*oit)->ibppEventHandler(db, eit.get_name(), (int)(vnew - vold));
				std::copy(rit.begin(), rit.end(), eit.begin());
			} catch (...) { }
		}
	}
}

//
//	EOF
//
