// cursors.cpp
/*
 *    Copyright (C) 2010 10gen Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#include "pch.h"
#include "cursors.h"
#include "../client/connpool.h"
#include "../db/queryutil.h"
#include "../db/commands.h"

namespace mongo {
    
    // --------  ShardedCursor -----------

    ShardedClientCursor::ShardedClientCursor( QueryMessage& q , ClusteredCursor * cursor ){
        assert( cursor );
        _cursor = cursor;
        
        _skip = q.ntoskip;
        _ntoreturn = q.ntoreturn;
        
        _totalSent = 0;
        _done = false;

        do {
            // TODO: only create _id when needed
            _id = security.getNonce();
        } while ( _id == 0 );

    }

    ShardedClientCursor::~ShardedClientCursor(){
        assert( _cursor );
        delete _cursor;
        _cursor = 0;
    }

    bool ShardedClientCursor::sendNextBatch( Request& r , int ntoreturn ){
        uassert( 10191 ,  "cursor already done" , ! _done );
                
        int maxSize = 1024 * 1024;
        if ( _totalSent > 0 )
            maxSize *= 3;
        
        BufBuilder b(32768);
        
        int num = 0;
        bool sendMore = true;

        while ( _cursor->more() ){
            BSONObj o = _cursor->next();

            b.appendBuf( (void*)o.objdata() , o.objsize() );
            num++;
            
            if ( b.len() > maxSize ){
                break;
            }

            if ( num == ntoreturn ){
                // soft limit aka batch size
                break;
            }

            if ( ntoreturn != 0 && ( -1 * num + _totalSent ) == ntoreturn ){
                // hard limit - total to send
                sendMore = false;
                break;
            }
        }

        bool hasMore = sendMore && _cursor->more();
        log(6) << "\t hasMore:" << hasMore << " wouldSendMoreIfHad: " << sendMore << " id:" << _id << " totalSent: " << _totalSent << endl;
        
        replyToQuery( 0 , r.p() , r.m() , b.buf() , b.len() , num , _totalSent , hasMore ? _id : 0 );
        _totalSent += num;
        _done = ! hasMore;
        
        return hasMore;
    }
    

    CursorCache::CursorCache()
        :_mutex( "CursorCache" ){
    }

    CursorCache::~CursorCache(){
        // TODO: delete old cursors?
    }

    ShardedClientCursorPtr CursorCache::get( long long id ){
        scoped_lock lk( _mutex );
        MapSharded::iterator i = _cursors.find( id );
        if ( i == _cursors.end() ){
            OCCASIONALLY log() << "Sharded CursorCache missing cursor id: " << id << endl;
            return ShardedClientCursorPtr();
        }
        return i->second;
    }
    
    void CursorCache::store( ShardedClientCursorPtr cursor ){
        scoped_lock lk( _mutex );
        _cursors[cursor->getId()] = cursor;
    }
    void CursorCache::remove( long long id ){
        scoped_lock lk( _mutex );
        _cursors.erase( id );
    }

    void CursorCache::storeRef( const string& server , long long id ){
        scoped_lock lk( _mutex );
        _refs[id] = server;
    }
    
    void CursorCache::gotKillCursors(Message& m ){
        int *x = (int *) m.singleData()->_data;
        x++; // reserved
        int n = *x++;
        
        uassert( 13286 , "sent 0 cursors to kill" , n >= 1 );
        uassert( 13287 , "too many cursors to kill" , n < 10000 );
        
        long long * cursors = (long long *)x;
        for ( int i=0; i<n; i++ ){
            long long id = cursors[i];

            string server;            
            {
                scoped_lock lk( _mutex );

                MapSharded::iterator i = _cursors.find( id );
                if ( i != _cursors.end() ){
                    _cursors.erase( i );
                    continue;
                }
                
                MapNormal::iterator j = _refs.find( id );
                if ( j == _refs.end() ){
                    log() << "can't find cursor: " << id << endl;
                    continue;
                }
                server = j->second;
                _refs.erase( j );
            }
            
            assert( server.size() );
            ScopedDbConnection conn( server );
            conn->killCursor( id );
            conn.done();
        }
    }

    void CursorCache::appendInfo( BSONObjBuilder& result ){
        scoped_lock lk( _mutex );
        result.append( "sharded" , (int)_cursors.size() );
        result.append( "refs" , (int)_refs.size() );
        result.append( "total" , (int)(_cursors.size() + _refs.size() ) );
    }

    CursorCache cursorCache;

    class CmdCursorInfo : public Command {
    public:
        CmdCursorInfo() : Command( "cursorInfo", true ) {}
        virtual bool slaveOk() const { return true; }
        virtual void help( stringstream& help ) const {
            help << " example: { cursorInfo : 1 }";
        }
        virtual LockType locktype() const { return NONE; }
        bool run(const string&, BSONObj& jsobj, string& errmsg, BSONObjBuilder& result, bool fromRepl ){
            cursorCache.appendInfo( result );
            return true;
        }
    } cmdCursorInfo;

}
