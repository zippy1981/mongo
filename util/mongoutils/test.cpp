<<<<<<< HEAD
// @file test.cpp

/* unit tests for mongoutils 
*/

#include "str.h"

#include "html.h"

#include <assert.h>

using namespace std;
using namespace mongoutils;

int main() {
    string x = str::after("abcde", 'c');
    assert( x == "de" );
    assert( str::after("abcde", 'x') == "" );
}

=======
// @file test.cpp

/*
 *    Copyright 2010 10gen Inc.
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

/* unit tests for mongoutils 
*/

#include "str.h"

#include "html.h"

#include <assert.h>

using namespace std;
using namespace mongoutils;

int main() {
    string x = str::after("abcde", 'c');
    assert( x == "de" );
    assert( str::after("abcde", 'x') == "" );
}

>>>>>>> 6e8fafab5871d442318d8deeda57555eae54a797
