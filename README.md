# ATL (COW) vs. STL String Performance Tests Based on Herb Sutter's GotW #45

(Original code downloadable from: [http://www.gotw.ca/gotw/045.htm](http://www.gotw.ca/gotw/045.htm))

by Giovanni Dicanio < giovanni.dicanio AT gmail.com >

July 8th, 2016

## Main Changes

- Code modified to compile cleanly at /W4 with VS2015.

- Added tests for **STL** and **ATL** strings.


## More Details on Code Changes

- Renamed file `"common.cpp"` to `"common-test.h"` (since this file is just `#include`d).

- In `test.cpp`'s `RUN_TEST` macro, created a local variable `testString` instead of 
  using VC++'s non-standard extension 

- In `test.h`, used performance counters instead of `GetTickCount()`.

- In `test.cpp`, added tests for `ATL::CStringA` and `std::string`.

- General fixes to code to compile cleanly at /W4 with VS2015.

- Defaulted `nLoops` to 1,000,000 in `main()`.


