// hostandport.h

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

#include "sock.h"
#include "../db/cmdline.h"
#include "mongoutils/str.h"
 
namespace mongo { 

    using namespace mongoutils;

    /** helper for manipulating host:port connection endpoints. 
      */
    struct HostAndPort { 
        HostAndPort() : _port(-1) { }

        /** From a string hostname[:portnumber] 
            Throws user assertion if bad config string or bad port #.
            */
        HostAndPort(string s);

        /** @param p port number. -1 is ok to use default. */
        HostAndPort(string h, int p /*= -1*/) : _host(h), _port(p) { }

        HostAndPort(const SockAddr& sock ) 
            : _host( sock.getAddr() ) , _port( sock.getPort() ){
        }

        static HostAndPort me() { 
            return HostAndPort("localhost", cmdLine.port);
        }

        bool operator<(const HostAndPort& r) const { return _host < r._host || (_host==r._host&&_port<r._port); }

        /* returns true if the host/port combo identifies this process instance. */
        bool isSelf() const;

        bool isLocalHost() const;

        // @returns host:port
        string toString() const; 

        operator string() const { return toString(); }

        string host() const { return _host; }

        int port() const { return _port >= 0 ? _port : cmdLine.port; }

    private:
        // invariant (except full obj assignment):
        string _host;
        int _port; // -1 indicates unspecified
    };

    /** returns true if strings seem to be the same hostname.
        "nyc1" and "nyc1.acme.com" are treated as the same.
        in fact "nyc1.foo.com" and "nyc1.acme.com" are treated the same - 
        we oly look up to the first period.
    */
    inline bool sameHostname(const string& a, const string& b) {
        return str::before(a, '.') == str::before(b, '.');
    }

    inline bool HostAndPort::isSelf() const { 
        int p = _port == -1 ? CmdLine::DefaultDBPort : _port;
        if( p != cmdLine.port )
            return false;
        
        return sameHostname(getHostName(), _host) || isLocalHost();
    }

    inline string HostAndPort::toString() const {
        stringstream ss;
        ss << _host;
        if( _port != -1 ) ss << ':' << _port;
        return ss.str();
    }

    inline bool HostAndPort::isLocalHost() const { 
        return _host == "localhost" || _host == "127.0.0.1" || _host == "::1";
    }

    inline HostAndPort::HostAndPort(string s) {
        const char *p = s.c_str();
        uassert(13110, "HostAndPort: bad config string", *p);
        const char *colon = strrchr(p, ':');
        if( colon ) {
            int port = atoi(colon+1);
            uassert(13095, "HostAndPort: bad port #", port > 0);
            _host = string(p,colon-p);
            _port = port;
        }
        else {
            // no port specified.
            _host = p;
            _port = -1;
        }
    }

}
