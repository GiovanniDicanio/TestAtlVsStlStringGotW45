////////////////////////////////////////////////////////////////////////////////
//
// String Performance Tests Based on Herb Sutter's GotW #45.
//
// by Giovanni Dicanio <giovanni.dicanio@gmail.com>
//
////////////////////////////////////////////////////////////////////////////////


//==============================================================================
//
//  Guru of the Week #45: Sample code, and performance test harness.
//
//  Version 2.0: Changes include the following:
//
//      - changed reference count from unsigned to long, and "unshareable"
//          state is now flagged by a negative number
//
//      - changed IntAtomicGet to IntAtomicCompare
//
//      - inlined everything
//
//      - changed AboutToModify to EnsureUnique and EnsureUnshareable, in order
//          to avoid passing the "bMarkUnshareable" bool as a runtime flag when
//          this is after all known at compile time
//
//      - created fixed-size allocator (i.e., "fast allocator") for StringBuf
//          to eliminate any issues related to double allocation penalties
//
//      - added non-COW test cases, including one that uses a fast allocator
//          (the original version compared only various thread-safe and -unsafe
//          flavours of COW, and didn't compare plain non-COW)
//
//      - added alternatively-optimized COW test cases, include COW_AtomicInt2
//          that avoids a secondary allocation by using a single buffer for the
//          len/used/refs values and the string data
//
//      - added native x86 assembler versions of the AtomicInt functions to
//          show that the Win32 operations were equally efficient (no, there's
//          no function-call overhead)
//
//      - added ability to test copying and Append, in addition to operator[],
//          which also meant adding a function to shrink a string
//
//      - changed from calling copy to calling memcpy... on my compiler,
//          memcpy was about three times faster (indicating that perhaps this
//          implementation ought to specialize copy for builtin types), and
//          the majority of COW's performance advantage came from avoiding the
//          inefficient copy, not from avoiding the allocation
//
//      - a lot of miscellaneous things I probably forgot to document
//
//  Copyright (c) 1998 by H.P.Sutter. All rights reserved. You may download and
//  compile and play with this code to your heart's content, but you may not
//  redistribute it; instead, please just include a link to this code at the
//  official GotW site.
//
//==============================================================================


//------------------------------------------------------------------------------
//
//  Here are good (but mostly platform-specific) sample implementations for
//  CriticalSection, Mutex, IntAtomicXxx (Win32 and inline assembler), Timer,
//  and FastArena.
//
//------------------------------------------------------------------------------

#include <windows.h>


//------------------------------------------------------------------------------

//  Helper class to ensure locks are acquired and released in pairs, even in
//  the presence of exceptions.
//
template<class T>
class Lock
{
public:
  Lock( T& t )
    : t_(t),
      bLocked_(true)
  {
    t_.Lock();
  }

 ~Lock() {
    Unlock();
  }

  void Unlock() {
    if( bLocked_ ) {
      t_.Unlock();
      bLocked_ = false;
    }
  }
private:
  T& t_;
  bool bLocked_;
};

class CriticalSection
{
public:
  CriticalSection() { InitializeCriticalSection( &cs_ ); }
private:
  friend Lock<CriticalSection>;
  void Lock()       { EnterCriticalSection( &cs_ ); }
  void Unlock()     { LeaveCriticalSection( &cs_ ); }
  CRITICAL_SECTION cs_;
};

class Mutex
{
public:
  Mutex()
   : m_(0)
  {
    SECURITY_ATTRIBUTES sec;
    sec.nLength = sizeof( sec );
    sec.lpSecurityDescriptor = 0;
    sec.bInheritHandle = false;
    m_ = CreateMutex( &sec, false, L"" );
  }

  ~Mutex()
  {
    if( m_ )
    {
      CloseHandle(m_);
      m_ = 0;
    }
  }

private:
  friend Lock<Mutex>;
  void Lock()     { WaitForSingleObject( m_, INFINITE ); }
  void Unlock()   { ReleaseMutex( m_ ); }
  HANDLE m_;
};


//------------------------------------------------------------------------------

//  Odd... for some reason InterlockedExchangeAdd is not available in
//  Windows 95, so this code won't run on that OS. It's available under
//  Windows 98 and NT, though. Oh well.
//
inline long IntAtomicCompare( long& i, long v )
{
  long ii = InterlockedExchangeAdd( &i, 0 );
  return ( (ii) < (v) ? -1 : ( (ii) == (v) ? 0 : 1 ) );
}
inline long IntAtomicIncrement( long& i ) { return InterlockedIncrement( &i ); }
inline long IntAtomicDecrement( long& i ) { return InterlockedDecrement( &i ); }



//------------------------------------------------------------------------------

class Timer
{
public:
  Timer() : _start( PerfCounter() ) { }
  
  int Elapsed() // milliseconds 
  { 
      const long long finish = PerfCounter();

      double elapsedMilliseconds = ((finish - _start) * 1000.0) / PerfFrequency();

      return static_cast<int>(elapsedMilliseconds);
  }

private:
  const long long _start;

  static long long PerfCounter()
  {
      LARGE_INTEGER li;
      QueryPerformanceCounter(&li);
      return li.QuadPart;
  }

  static long long PerfFrequency()
  {
      LARGE_INTEGER li;
      QueryPerformanceFrequency(&li);
      return li.QuadPart;
  }

};


//------------------------------------------------------------------------------
//
//  A (very) simple fixed-length allocator.

//#define FA_REPORT      1
//#define FA_DEBUG       1
#define FA_THREAD_SAFE 1

class FastArena
{
public:
  FastArena( const char* name = "", size_t n = 3000 )
    : n_( n ? 4*((n-1)/4+1) : 4 ) // make the chunk size a multiple of 4
    , buf_( new char[(n_+sizeof(long))*size] )
#ifdef FA_REPORT
    , current_(0)
    , highest_(0)
    , totalops_(0)
    , name_( name )
#endif
  {
    UNREFERENCED_PARAMETER(name);
    
    for( char* p = buf_; p < buf_ + (n_+sizeof(long))*size; p += (n_+sizeof(long)) )
    {
        *((long*)p) = 0;
    }
  }

  ~FastArena()
  {
#ifdef FA_REPORT
    cout << "FastArena " << setw(Ass0) << name_
         << ": current_=" << current_
         << ", highest_=" << highest_
         << ", totalops_=" << totalops_ << "\n";
#endif
    delete[] buf_;
    buf_ = 0;
  }

  void* Allocate( size_t n )
  {
    if( n > n_ )
    {
#ifdef FA_DEBUG
      cout << "Bad Allocate: size " << n << ", expected at most " << n_ << "\n" << flush;
#endif
      throw bad_alloc();    // ensure we're not getting surprises
    }

    char* p = buf_;
    while( p < (buf_ + (n_+sizeof(long))*size) && *((long*)p) != 0 )
    {
      p += (n_+sizeof(long));
    }

    if( p >= (buf_ + (n_+sizeof(long))*size) )
    {
#ifdef FA_DEBUG
      cout << "Bad Allocate: exhausted, current_=" << current_ << "\n" << flush;
#endif
      throw bad_alloc();
    }

#ifdef FA_REPORT
    ++totalops_;
    if( ++current_ > highest_ ) highest_ = current_;
#endif
#ifdef FA_THREAD_SAFE
    IntAtomicIncrement( *(long*)p );
#else
    *((long*)p) = 1L;
#endif
    return p+sizeof(long);
  }

  void Deallocate( void* p )
  {
    if( p == 0 )            // support "null-pointer, null-operation" semantics
    {
      return;
    }

    if( p < buf_ || p > buf_ + (n_+sizeof(long))*size )
    {
#ifdef FA_DEBUG
      cout << "Bad Deallocate\n" << flush;
#endif
      throw bad_alloc();    // ensure we're not getting surprises
    }

#ifdef FA_REPORT
    ++totalops_;
    --current_;
#endif
#ifdef FA_DEBUG
    if( *(long*)(((char*)p)-sizeof(long)) != 1 )
    {
      cout << "Bad Deallocate: double delete\n" << flush;
    }
#endif
#ifdef FA_THREAD_SAFE
    IntAtomicDecrement( *(long*)(((char*)p)-sizeof(long)) );
#else
    *(long*)(((char*)p)-sizeof(long)) = 0L;
#endif
  }

private:
  static const size_t size;

  size_t n_;
  char*  buf_;
#ifdef FA_REPORT
  size_t current_;  // # currently in use
  size_t highest_;  // highest # currently in use
  size_t totalops_; // total number of allocations/deallocations
  const char* name_;
#endif
};

const size_t FastArena::size = 100;   // # elements


