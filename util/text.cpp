// text.h

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

#include "stdafx.h"
#include <string.h>

namespace mongo {
    #if defined(_WIN32)

	std::string toUtf8String(const std::wstring& wide)
	{
		if (wide.size() > boost::integer_traits<int>::const_max)
			throw std::length_error(
					"Wide string cannot be more than INT_MAX characters long.");
		if (wide.size() == 0)
			return "";

		// Calculate necessary buffer size
		int len = ::WideCharToMultiByte(
			CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), 
			NULL, 0, NULL, NULL);

		// Perform actual conversion
		if (len > 0)
		{
			std::vector<char> buffer(len);
			len = ::WideCharToMultiByte(
					CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
					&buffer[0], static_cast<int>(buffer.size()), NULL, NULL);
			if (len > 0)
			{
					assert(len == static_cast<int>(buffer.size()));
					return std::string(&buffer[0], buffer.size());
			}
		}

		throw boost::system::system_error(
			::GetLastError(), boost::system::system_category);
	}

	std::wstring toWideString(const char *s) {
        std::basic_ostringstream<TCHAR> buf;
        buf << s;
        return buf.str();
    }

    #endif
} // namespace mongo