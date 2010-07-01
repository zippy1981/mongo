// dbcommands_generic.cpp

/**
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
 * commands suited for any mongo server
 */

#include "pch.h"
#include "query.h"
#include "pdfile.h"
#include "jsobj.h"
#include "../bson/util/builder.h"
#include <time.h>
#include "introspect.h"
#include "btree.h"
#include "../util/lruishmap.h"
#include "../util/md5.hpp"
#include "../util/processinfo.h"
#include "json.h"
#include "repl.h"
#include "repl_block.h"
#include "replpair.h"
#include "commands.h"
#include "db.h"
#include "instance.h"
#include "lasterror.h"
#include "security.h"
#include "queryoptimizer.h"
#include "../scripting/engine.h"
#include "stats/counters.h"
#include "background.h"
#include "../util/version.h"

namespace mongo {

    class CmdBuildInfo : public Command {
    public:
        CmdBuildInfo() : Command( "buildInfo", true, "buildinfo" ) {}
        virtual bool slaveOk() const { return true; }
        virtual bool adminOnly() const { return true; }
        virtual LockType locktype() const { return NONE; } 
        virtual void help( stringstream &help ) const {
            help << "get version #, etc.\n";
            help << "{ buildinfo:1 }";
        }
        bool run(const string& dbname, BSONObj& jsobj, string& errmsg, BSONObjBuilder& result, bool fromRepl ){
            result << "version" << versionString << "gitVersion" << gitVersion() << "sysInfo" << sysInfo();
            result << "bits" << ( sizeof( int* ) == 4 ? 32 : 64 );
            return true;
        }
    } cmdBuildInfo;

    /* for diagnostic / testing purposes. */
    class CmdSleep : public Command { 
    public:
        virtual LockType locktype() const { return READ; } 
        virtual bool adminOnly() const { return true; }
        virtual bool logTheOp() {
            return false;
        }
        virtual bool slaveOk() const {
            return true;
        }
        virtual void help( stringstream& help ) const {
            help << "internal testing command.  Makes db block (in a read lock) for 100 seconds";
        }
        CmdSleep() : Command("sleep") {}
        bool run(const string& ns, BSONObj& cmdObj, string& errmsg, BSONObjBuilder& result, bool fromRepl) {
            sleepsecs(100);
            return true;
        }
    } cmdSleep;

    /* just to check if the db has asserted */
    class CmdAssertInfo : public Command {
    public:
        virtual bool slaveOk() const {
            return true;
        }
        virtual void help( stringstream& help ) const {
            help << "check if any asserts have occurred on the server";
        }
        virtual LockType locktype() const { return WRITE; } 
        CmdAssertInfo() : Command("assertInfo",true,"assertinfo") {}
        bool run(const string& dbname, BSONObj& cmdObj, string& errmsg, BSONObjBuilder& result, bool fromRepl) {
            result.appendBool("dbasserted", lastAssert[0].isSet() || lastAssert[1].isSet() || lastAssert[2].isSet());
            result.appendBool("asserted", lastAssert[0].isSet() || lastAssert[1].isSet() || lastAssert[2].isSet() || lastAssert[3].isSet());
            result.append("assert", lastAssert[AssertRegular].toString());
            result.append("assertw", lastAssert[AssertW].toString());
            result.append("assertmsg", lastAssert[AssertMsg].toString());
            result.append("assertuser", lastAssert[AssertUser].toString());
            return true;
        }
    } cmdAsserts;

    class PingCommand : public Command {
    public:
        PingCommand() : Command( "ping" ){}
        virtual bool slaveOk() const { return true; }
        virtual void help( stringstream &help ) const { help << "a way to check that the server is alive. responds immediately even if server is in a db lock."; }
        virtual LockType locktype() const { return NONE; }
        virtual bool requiresAuth() { return false; }
        virtual bool run(const string& badns, BSONObj& cmdObj, string& errmsg, BSONObjBuilder& result, bool){
            // IMPORTANT: Don't put anything in here that might lock db - including authentication
            return true;
        }
    } pingCmd;
    
    class FeaturesCmd : public Command {
    public:
        FeaturesCmd() : Command( "features", true ){}
        void help(stringstream& h) const { h << "return on build level feature settings"; }
        virtual bool slaveOk() const { return true; }
        virtual bool readOnly(){ return true; }
        virtual LockType locktype() const { return READ; } 
        virtual bool run(const string& ns, BSONObj& cmdObj, string& errmsg, BSONObjBuilder& result, bool fromRepl){
            if ( globalScriptEngine ){
                BSONObjBuilder bb( result.subobjStart( "js" ) );
                result.append( "utf8" , globalScriptEngine->utf8Ok() );
                bb.done();
            }
            if ( cmdObj["oidReset"].trueValue() ){
                result.append( "oidMachineOld" , OID::staticMachine() );
                OID::newState();
            }
            result.append( "oidMachine" , OID::staticMachine() );
            return true;
        }
        
    } featuresCmd;

    class LogRotateCmd : public Command {
    public:
        LogRotateCmd() : Command( "logRotate" ){}
        virtual LockType locktype() const { return NONE; } 
        virtual bool slaveOk() const { return true; }
        virtual bool adminOnly() const { return true; }
        virtual bool run(const string& ns, BSONObj& cmdObj, string& errmsg, BSONObjBuilder& result, bool fromRepl) {
            rotateLogs();
            return 1;
        }        
        
    } logRotateCmd;
    
    class ListCommandsCmd : public Command {
    public:
        virtual void help( stringstream &help ) const { help << "get a list of all db commands"; }
        ListCommandsCmd() : Command( "listCommands", false ){}
        virtual LockType locktype() const { return NONE; } 
        virtual bool slaveOk() const { return true; }
        virtual bool adminOnly() const { return false; }
        virtual bool run(const string& ns, BSONObj& cmdObj, string& errmsg, BSONObjBuilder& result, bool fromRepl) {
            BSONObjBuilder b( result.subobjStart( "commands" ) );
            for ( map<string,Command*>::iterator i=_commands->begin(); i!=_commands->end(); ++i ){
                Command * c = i->second;
                BSONObjBuilder temp( b.subobjStart( c->name.c_str() ) );

                {
                    stringstream help;
                    c->help( help );
                    temp.append( "help" , help.str() );
                }
                temp.append( "lockType" , c->locktype() );
                temp.append( "slaveOk" , c->slaveOk() );
                temp.append( "adminOnly" , c->adminOnly() );
                temp.done();
            }
            b.done();

            return 1;
        }        

    } listCommandsCmd;

}
