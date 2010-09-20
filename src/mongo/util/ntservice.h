// ntservice.h

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

#if defined(_WIN32)
#include <windows.h>
#include "boost/program_options.hpp"

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS  ((NTSTATUS)0x00000000L)
#endif
#ifndef LSA_LOOKUP_ISOLATED_AS_LOCAL
#define LSA_LOOKUP_ISOLATED_AS_LOCAL 0x80000000
#endif
#ifndef STATUS_SOME_NOT_MAPPED
#define STATUS_SOME_NOT_MAPPED 0x00000107
#endif
#ifndef STATUS_NONE_MAPPED
#define STATUS_NONE_MAPPED 0xC0000073
#endif

namespace mongo {

    typedef bool ( *ServiceCallback )( void );
    bool serviceParamsCheck( boost::program_options::variables_map& params, const std::string dbpath, int argc, char* argv[] );

    class ServiceController {
    public:
        ServiceController();
        virtual ~ServiceController() {}

        static bool installService( const std::wstring& serviceName, const std::wstring& displayName, const std::wstring& serviceDesc, const std::wstring& serviceUser, const std::wstring& servicePassword, const std::string dbpath, int argc, char* argv[] );
        static bool removeService( const std::wstring& serviceName );
        static bool startService( const std::wstring& serviceName, ServiceCallback startService );
        static bool reportStatus( DWORD reportState, DWORD waitHint = 0 );

        static void WINAPI initService( DWORD argc, LPTSTR *argv );
        static void WINAPI serviceCtrl( DWORD ctrlCode );

    protected:
        static std::wstring _serviceName;
        static SERVICE_STATUS_HANDLE _statusHandle;
        static ServiceCallback _serviceCallback;
    };

} // namespace mongo

#endif
