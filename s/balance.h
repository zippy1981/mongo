// balance.h

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

#pragma once

#include "../pch.h"
#include "../util/background.h"
#include "../client/dbclient.h"
#include "balancer_policy.h"

namespace mongo {
    
    class Balancer : public BackgroundJob {
    public:
        Balancer();
        virtual ~Balancer();

        // BackgroundJob methods

        virtual void run();

        virtual string name() { return "Balancer"; }        

    private:
        /**
         * Returns true iff this mongos process gained (or maintained) the
         * reponsibility moving chunks around.
         */
        bool _shouldIBalance( DBClientBase& conn );
        
        /**
         * Execute the chunk migrations described in 'toBalance' and
         * returns the number of chunks effectively moved.
         */
        int _moveChunks( const vector<BalancerPolicy::ChunkInfoPtr>* toBalance );

        /**
         * Check the health of the master configuration server
         */
        void _ping();
        void _ping( DBClientBase& conn );

        /**
         * @return true if everything is ok
         */
        bool _checkOIDs();

        // internal state

        string          _myid;     // hostname:port of my mongos
        time_t          _started;  // time Balancer starte running
        BalancerPolicy* _policy;   // decide which chunks to move; owned here.

        // non-copyable, non-assignable

        Balancer(const Balancer&);
        Balancer operator=(const Balancer&);
    };
    
    extern Balancer balancer;
}
