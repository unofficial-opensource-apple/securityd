/*
 * Copyright (c) 2000-2001 Apple Computer, Inc. All Rights Reserved.
 * 
 * @APPLE_LICENSE_HEADER_START@
 * 
 * Copyright (c) 1999-2003 Apple Computer, Inc.  All Rights Reserved.
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */


//
// database - database session management
//
#include "database.h"
#include "agentquery.h"
#include "key.h"
#include "server.h"
#include "session.h"
#include <security_agent_client/agentclient.h>
#include <security_cdsa_utilities/acl_any.h>	// for default owner ACLs
#include <security_cdsa_client/wrapkey.h>
#include <security_utilities/endian.h>


//
// DbCommon basics
//
DbCommon::DbCommon(Session &session)
{
	referent(session);
}

Session &DbCommon::session() const
{
	return referent<Session>();
}


//
// Database basics
//
Database::Database(Process &proc)
	: SecurityServerAcl(dbAcl, Allocator::standard())
{
	referent(proc);
}


Process& Database::process() const
{
	return referent<Process>();
}


//
// Default behaviors
//
void DbCommon::sleepProcessing()
{
	// nothing
}


void Database::releaseKey(Key &key)
{
	removeReference(key);
}


//
// Implementation of a "system keychain unlock key store"
//
SystemKeychainKey::SystemKeychainKey(const char *path)
	: mPath(path)
{
	// explicitly set up a key header for a raw 3DES key
	CssmKey::Header &hdr = mKey.header();
	hdr.blobType(CSSM_KEYBLOB_RAW);
	hdr.blobFormat(CSSM_KEYBLOB_RAW_FORMAT_OCTET_STRING);
	hdr.keyClass(CSSM_KEYCLASS_SESSION_KEY);
	hdr.algorithm(CSSM_ALGID_3DES_3KEY_EDE);
	hdr.KeyAttr = 0;
	hdr.KeyUsage = CSSM_KEYUSE_ANY;
	mKey = CssmData::wrap(mBlob.masterKey);
}

SystemKeychainKey::~SystemKeychainKey()
{
}

bool SystemKeychainKey::matches(const DbBlob::Signature &signature)
{
	return update() && signature == mBlob.signature;
}

bool SystemKeychainKey::update()
{
	// if we checked recently, just assume it's okay
	if (mUpdateThreshold > Time::now())
		return mValid;
		
	// check the file
	struct stat st;
	if (::stat(mPath.c_str(), &st)) {
		// something wrong with the file; can't use it
		mUpdateThreshold = Time::now() + Time::Interval(checkDelay);
		return mValid = false;
	}
	if (mValid && Time::Absolute(st.st_mtimespec) == mCachedDate)
		return true;
	mUpdateThreshold = Time::now() + Time::Interval(checkDelay);
	
	try {
		secdebug("syskc", "reading system unlock record from %s", mPath.c_str());
		AutoFileDesc fd(mPath, O_RDONLY);
		if (fd.read(mBlob) != sizeof(mBlob))
			return false;
		if (mBlob.isValid()) {
			mCachedDate = st.st_mtimespec;
			return mValid = true;
		} else
			return mValid = false;
	} catch (...) {
		secdebug("syskc", "system unlock record not available");
		return false;
	}
}
