====================================================================================================

              String Performance Tests Based on Herb Sutter's GotW #45

          (original code downloaded from: http://www.gotw.ca/gotw/045.htm)

                 by Giovanni Dicanio <giovanni.dicanio@gmail.com>

====================================================================================================

July 8th, 2016

Main Changes
------------

 - Code fixed to compile cleanly at /W4 with VS2015.

 - Added tests for STL and ATL strings.


More Details on Code Changes
----------------------------

- Renamed "common.cpp" to "common-test.h" (since this file is just #included).

- In test.cpp RUN_TEST macro, created a local variable testString instead of 
  using VC++'s non-standard extension 

- In test.h, used performance counters instead of GetTickCount.

- In test.cpp, added tests for CStringA and std::string.

- General fixes to code to compile cleanly at /W4 with VS2015.

- Defaulted nLoops = 1,000,000 in main().

