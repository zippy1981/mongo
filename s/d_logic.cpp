// d_logic.cpp

/**
*    Copyright (C) 2008 10gen Inc.
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


/**
   these are commands that live in mongod
   mostly around shard management and checking
 */

#include "pch.h"
#include <map>
#include <string>

#include "../db/commands.h"
#include "../db/jsobj.h"
#include "../db/dbmessage.h"
#include "../db/query.h"

#include "../client/connpool.h"

#include "../util/queue.h"

#include "shard.h"
#include "d_logic.h"

using namespace std;

namespace mongo {

    // -----ShardingState START ----
    
    ShardingState::ShardingState(){
        _enabled = false;
    }
    
    void ShardingState::enable( const string& server ){
        _enabled = true;
        assert( server.size() );
        if ( _configServer.size() == 0 )
            _configServer = server;
        else {
            assert( server == _configServer );
        }
    }
    
    
    bool ShardingState::hasVersion( const string& ns ) const {
        NSVersionMap::const_iterator i = _versions.find(ns);
        return i != _versions.end();
    }
    
    bool ShardingState::hasVersion( const string& ns , ConfigVersion& version ) const {
        NSVersionMap::const_iterator i = _versions.find(ns);
        if ( i == _versions.end() )
            return false;
        version = i->second;
        return true;
    }
    
    ConfigVersion& ShardingState::getVersion( const string& ns ){
        return _versions[ns];
    }
    
    void ShardingState::setVersion( const string& ns , const ConfigVersion& version ){
        _versions[ns] = version;
    }

    ShardingState shardingState;

    // -----ShardingState END ----
    
    // -----ShardedConnectionInfo START ----

    boost::thread_specific_ptr<ShardedConnectionInfo> ShardedConnectionInfo::_tl;

    ShardedConnectionInfo::ShardedConnectionInfo(){
        _id.clear();
    }
    
    ShardedConnectionInfo* ShardedConnectionInfo::get( bool create ){
        ShardedConnectionInfo* info = _tl.get();
        if ( ! info && create ){
            log(1) << "entering shard mode for connection" << endl;
            info = new ShardedConnectionInfo();
            _tl.reset( info );
        }
        return info;
    }

    ConfigVersion& ShardedConnectionInfo::getVersion( const string& ns ){
        return _versions[ns];
    }
    
    void ShardedConnectionInfo::setVersion( const string& ns , const ConfigVersion& version ){
        _versions[ns] = version;
    }

    void ShardedConnectionInfo::setID( const OID& id ){
        _id = id;
    }

    // -----ShardedConnectionInfo END ----

    unsigned long long extractVersion( BSONElement e , string& errmsg ){
        if ( e.eoo() ){
            errmsg = "no version";
            return 0;
        }
        
        if ( e.isNumber() )
            return (unsigned long long)e.number();
        
        if ( e.type() == Date || e.type() == Timestamp )
            return e._numberLong();

        
        errmsg = "version is not a numeric type";
        return 0;
    }

    class MongodShardCommand : public Command {
    public:
        MongodShardCommand( const char * n ) : Command( n ){
        }
        virtual bool slaveOk() const {
            return false;
        }
        virtual bool adminOnly() const {
            return true;
        }
    };
    


    // setShardVersion( ns )
    
    class SetShardVersion : public MongodShardCommand {
    public:
        SetShardVersion() : MongodShardCommand("setShardVersion"){}

        virtual void help( stringstream& help ) const {
            help << " example: { setShardVersion : 'alleyinsider.foo' , version : 1 , configdb : '' } ";
        }
        
        virtual LockType locktype() const { return WRITE; } // TODO: figure out how to make this not need to lock
 
        bool run(const string& , BSONObj& cmdObj, string& errmsg, BSONObjBuilder& result, bool){
            ShardedConnectionInfo* info = ShardedConnectionInfo::get( true );

            bool authoritative = cmdObj.getBoolField( "authoritative" );

            string configdb = cmdObj["configdb"].valuestrsafe();
            { // configdb checking
                if ( configdb.size() == 0 ){
                    errmsg = "no configdb";
                    return false;
                }
                
                if ( shardingState.enabled() ){
                    if ( configdb != shardingState.getConfigServer() ){
                        errmsg = "specified a different configdb!";
                        return false;
                    }
                }
                else {
                    if ( ! authoritative ){
                        result.appendBool( "need_authoritative" , true );
                        errmsg = "first setShardVersion";
                        return false;
                    }
                    shardingState.enable( configdb );
                }
            }
            
            { // setting up ids
                if ( cmdObj["serverID"].type() != jstOID ){
                    // TODO: fix this
                    //errmsg = "need serverID to be an OID";
                    //return 0;
                }
                else {
                    OID clientId = cmdObj["serverID"].__oid();
                    if ( ! info->hasID() ){
                        info->setID( clientId );
                    }
                    else if ( clientId != info->getID() ){
                        errmsg = "server id has changed!";
                        return 0;
                    }
                }
            }
            
            unsigned long long version = extractVersion( cmdObj["version"] , errmsg );
            if ( errmsg.size() ){
                return false;
            }
            
            string ns = cmdObj["setShardVersion"].valuestrsafe();
            if ( ns.size() == 0 ){
                errmsg = "need to speciy fully namespace";
                return false;
            }

            ConfigVersion& oldVersion = info->getVersion(ns);
            unsigned long long& globalVersion = shardingState.getVersion(ns);
            
            if ( version == 0 && globalVersion == 0 ){
                // this connection is cleaning itself
                oldVersion = 0;
                return 1;
            }

            if ( version == 0 && globalVersion > 0 ){
                if ( ! authoritative ){
                    result.appendBool( "need_authoritative" , true );
                    result.appendTimestamp( "globalVersion" , globalVersion );
                    result.appendTimestamp( "oldVersion" , oldVersion );
                    errmsg = "dropping needs to be authoritative";
                    return 0;
                }
                log() << "wiping data for: " << ns << endl;
                result.appendTimestamp( "beforeDrop" , globalVersion );
                // only setting global version on purpose
                // need clients to re-find meta-data
                globalVersion = 0;
                oldVersion = 0;
                return 1;
            }

            if ( version < oldVersion ){
                errmsg = "you already have a newer version";
                result.appendTimestamp( "oldVersion" , oldVersion );
                result.appendTimestamp( "newVersion" , version );
                return false;
            }
            
            if ( version < globalVersion ){
                errmsg = "going to older version for global";
                return false;
            }
            
            if ( globalVersion == 0 && ! cmdObj.getBoolField( "authoritative" ) ){
                // need authoritative for first look
                result.appendBool( "need_authoritative" , true );
                result.append( "ns" , ns );
                errmsg = "first time for this ns";
                return false;
            }

            result.appendTimestamp( "oldVersion" , oldVersion );
            oldVersion = version;
            globalVersion = version;

            result.append( "ok" , 1 );
            return 1;
        }
        
    } setShardVersion;
    
    class GetShardVersion : public MongodShardCommand {
    public:
        GetShardVersion() : MongodShardCommand("getShardVersion"){}

        virtual void help( stringstream& help ) const {
            help << " example: { getShardVersion : 'alleyinsider.foo'  } ";
        }
        
        virtual LockType locktype() const { return WRITE; } // TODO: figure out how to make this not need to lock

        bool run(const string& , BSONObj& cmdObj, string& errmsg, BSONObjBuilder& result, bool){
            string ns = cmdObj["getShardVersion"].valuestrsafe();
            if ( ns.size() == 0 ){
                errmsg = "need to speciy fully namespace";
                return false;
            }
            
            result.append( "configServer" , shardingState.getConfigServer() );

            result.appendTimestamp( "global" , shardingState.getVersion(ns) );
            
            ShardedConnectionInfo* info = ShardedConnectionInfo::get( false );
            if ( info )
                result.appendTimestamp( "mine" , info->getVersion(ns) );
            else 
                result.appendTimestamp( "mine" , 0 );
            
            return true;
        }
        
    } getShardVersion;
    

    
    bool haveLocalShardingInfo( const string& ns ){
        if ( ! shardingState.enabled() )
            return false;
        
        if ( ! shardingState.hasVersion( ns ) )
            return false;

        return ShardedConnectionInfo::get(false) > 0;
    }

    /**
     * @ return true if not in sharded mode
                     or if version for this client is ok
     */
    bool shardVersionOk( const string& ns , string& errmsg ){
        if ( ! shardingState.enabled() )
            return true;
        
        ShardedConnectionInfo* info = ShardedConnectionInfo::get( false );
        if ( ! info ){
            // this means the client has nothing sharded
            // so this allows direct connections to do whatever they want
            // which i think is the correct behavior
            return true;
        }

        unsigned long long version;    
        if ( ! shardingState.hasVersion( ns , version ) )
            return true;

        unsigned long long clientVersion = info->getVersion(ns);
                
        if ( version == 0 && clientVersion > 0 ){
            stringstream ss;
            ss << "version: " << version << " clientVersion: " << clientVersion;
            errmsg = ss.str();
            return false;
        }
        
        if ( clientVersion >= version )
            return true;
        

        if ( clientVersion == 0 ){
            errmsg = "client in sharded mode, but doesn't have version set for this collection";
            return false;
        }

        errmsg = (string)"your version is too old  ns: " + ns;
        return false;
    }


    bool handlePossibleShardedMessage( Message &m, DbResponse &dbresponse ){
        if ( ! shardingState.enabled() )
            return false;

        int op = m.operation();
        if ( op < 2000 
             || op >= 3000 
             || op == dbGetMore  // cursors are weird
             )
            return false;

        
        const char *ns = m.singleData()->_data + 4;
        string errmsg;
        if ( shardVersionOk( ns , errmsg ) ){
            return false;
        }

        log() << "shardVersionOk failed  ns:" << ns << " " << errmsg << endl;
        
        if ( doesOpGetAResponse( op ) ){
            BufBuilder b( 32768 );
            b.skip( sizeof( QueryResult ) );
            {
                BSONObj obj = BSON( "$err" << errmsg );
                b.append( obj.objdata() , obj.objsize() );
            }
            
            QueryResult *qr = (QueryResult*)b.buf();
            qr->_resultFlags() = QueryResult::ResultFlag_ErrSet | QueryResult::ResultFlag_ShardConfigStale;
            qr->len = b.len();
            qr->setOperation( opReply );
            qr->cursorId = 0;
            qr->startingFrom = 0;
            qr->nReturned = 1;
            b.decouple();

            Message * resp = new Message();
            resp->setData( qr , true );
            
            dbresponse.response = resp;
            dbresponse.responseTo = m.header()->id;
            return true;
        }
        
        const OID& clientID = ShardedConnectionInfo::get(false)->getID();
        massert( 10422 ,  "write with bad shard config and no server id!" , clientID.isSet() );
        
        log() << "got write with an old config - writing back" << endl;

        BSONObjBuilder b;
        b.appendBool( "writeBack" , true );
        b.append( "ns" , ns );
        b.appendBinData( "msg" , m.header()->len , bdtCustom , (char*)(m.singleData()) );
        log() << "writing back msg with len: " << m.header()->len << " op: " << m.operation() << endl;
        queueWriteBack( clientID.str() , b.obj() );

        return true;
    }

}
