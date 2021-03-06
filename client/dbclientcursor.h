// file dbclientcursor.h 

/*    Copyright 2009 10gen Inc.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#pragma once

#include "../pch.h"
#include "../util/message.h"
#include "../db/jsobj.h"
#include "../db/json.h"
#include <stack>

namespace mongo {
    
    class AScopedConnection;
    
	/** Queries return a cursor object */
    class DBClientCursor : boost::noncopyable {
    public:
		/** If true, safe to call next().  Requests more from server if necessary. */
        bool more();

        /** If true, there is more in our local buffers to be fetched via next(). Returns 
            false when a getMore request back to server would be required.  You can use this 
            if you want to exhaust whatever data has been fetched to the client already but 
            then perhaps stop.
        */
        int objsLeftInBatch() const { _assertIfNull(); return _putBack.size() + nReturned - pos; }
        bool moreInCurrentBatch() { return objsLeftInBatch() > 0; }

        /** next
		   @return next object in the result cursor.
           on an error at the remote server, you will get back:
             { $err: <string> }
           if you do not want to handle that yourself, call nextSafe().
        */
        BSONObj next();
        
        /** 
            restore an object previously returned by next() to the cursor
         */
        void putBack( const BSONObj &o ) { _putBack.push( o.getOwned() ); }

		/** throws AssertionException if get back { $err : ... } */
        BSONObj nextSafe() {
            BSONObj o = next();
            BSONElement e = o.firstElement();
            if( strcmp(e.fieldName(), "$err") == 0 ) { 
                if( logLevel >= 5 )
                    log() << "nextSafe() error " << o.toString() << endl;
                uassert(13106, "nextSafe(): " + o.toString(), false);
            }
            return o;
        }

        /** peek ahead at items buffered for future next() calls.
            never requests new data from the server.  so peek only effective 
            with what is already buffered.
            WARNING: no support for _putBack yet!
        */
        void peek(vector<BSONObj>&, int atMost);

        /**
           iterate the rest of the cursor and return the number if items
         */
        int itcount(){
            int c = 0;
            while ( more() ){
                next();
                c++;
            }
            return c;
        }

        /** cursor no longer valid -- use with tailable cursors.
           note you should only rely on this once more() returns false;
           'dead' may be preset yet some data still queued and locally
           available from the dbclientcursor.
        */
        bool isDead() const {
            return  !this || cursorId == 0;
        }

        bool tailable() const {
            return (opts & QueryOption_CursorTailable) != 0;
        }
        
        /** see ResultFlagType (constants.h) for flag values 
            mostly these flags are for internal purposes - 
            ResultFlag_ErrSet is the possible exception to that
        */
        bool hasResultFlag( int flag ){
            _assertIfNull();
            return (resultFlags & flag) != 0;
        }

        DBClientCursor( DBConnector *_connector, const string &_ns, BSONObj _query, int _nToReturn,
                        int _nToSkip, const BSONObj *_fieldsToReturn, int queryOptions , int bs ) :
                connector(_connector),
                ns(_ns),
                query(_query),
                nToReturn(_nToReturn),
                haveLimit( _nToReturn > 0 && !(queryOptions & QueryOption_CursorTailable)),
                nToSkip(_nToSkip),
                fieldsToReturn(_fieldsToReturn),
                opts(queryOptions),
                batchSize(bs==1?2:bs),
                m(new Message()),
                cursorId(),
                nReturned(),
                pos(),
                data(),
                _ownCursor( true ){
        }
        
        DBClientCursor( DBConnector *_connector, const string &_ns, long long _cursorId, int _nToReturn, int options ) :
                connector(_connector),
                ns(_ns),
                nToReturn( _nToReturn ),
                haveLimit( _nToReturn > 0 && !(options & QueryOption_CursorTailable)),
                opts( options ),
                m(new Message()),
                cursorId( _cursorId ),
                nReturned(),
                pos(),
                data(),
                _ownCursor( true ){
        }            

        virtual ~DBClientCursor();

        long long getCursorId() const { return cursorId; }

        /** by default we "own" the cursor and will send the server a KillCursor
            message when ~DBClientCursor() is called. This function overrides that.
        */
        void decouple() { _ownCursor = false; }
        
        void attach( AScopedConnection * conn );
        
    private:
        friend class DBClientBase;
        friend class DBClientConnection;
        bool init();        
        int nextBatchSize();
        DBConnector *connector;
        string ns;
        BSONObj query;
        int nToReturn;
        bool haveLimit;
        int nToSkip;
        const BSONObj *fieldsToReturn;
        int opts;
        int batchSize;
        auto_ptr<Message> m;
        stack< BSONObj > _putBack;
        int resultFlags;
        long long cursorId;
        int nReturned;
        int pos;
        const char *data;
        void dataReceived();
        void requestMore();
        void exhaustReceiveMore(); // for exhaust
        bool _ownCursor; // see decouple()
        string _scopedHost;

        // Don't call from a virtual function
        void _assertIfNull() const { uassert(13348, "connection died", this); }
    };
    
    /** iterate over objects in current batch only - will not cause a network call
     */
    class DBClientCursorBatchIterator {
    public:
        DBClientCursorBatchIterator( DBClientCursor &c ) : _c( c ), _n() {}
        bool moreInCurrentBatch() { return _c.moreInCurrentBatch(); }
        BSONObj nextSafe() {
            massert( 13383, "BatchIterator empty", moreInCurrentBatch() );
            ++_n;
            return _c.nextSafe();
        }
        int n() const { return _n; }
    private:
        DBClientCursor &_c;
        int _n;
    };
    
} // namespace mongo

#include "undef_macros.h"
