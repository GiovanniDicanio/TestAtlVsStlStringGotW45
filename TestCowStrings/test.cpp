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
//      - added alternatively-optimized COW test cases, include COW_AtomicInt2
//          that avoids a secondary allocation by using a single buffer for the
//          len/used/refs values and the string data
//
//      - added non-COW test cases, including one that uses a fast allocator
//          (the original version compared only various thread-safe and -unsafe
//          flavours of COW, and didn't compare plain non-COW)
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

#include <iostream>
#include <fstream>
#include <iomanip>
#include <limits>
#include <algorithm>
#include <string>
#include <atlstr.h>
using namespace std;

//  Test.H contains sample definitions for CriticalSection, Mutex, IntAtomicXxx,
//  FastArena, and Timer. You will need to supply your own definitions here;
//  these classes are highly operating system-specific.
//
#include "test.h"   // *** you must implement this yourself ***



//--- Uncomment exactly one #define corresponding to the test you wish you run,
//    then rebuild and run (using command-line parameters to vary the number
//    of iterations, etc.).

//#define TEST_CONST_COPY       1
//#define TEST_APPEND           1
//#define TEST_OPERATOR         1

//#define TEST_INT_OPS_ONLY     1

#define TEST_MUTATING_COPY_2A 1
//#define TEST_MUTATING_COPY_2B 1



//------------------------------------------------------------------------------
//
//  Non-COW: Here's the original unoptimized version from GotW #43,
//  plus Length() and operator[]() functions.
//
//------------------------------------------------------------------------------

  namespace Plain {

    class String {
    public:
        String();                // start off empty
       ~String();                // free the buffer
        String( const String& ); // take a full copy
        void Clear();
        void Append( char );     // append one character
        size_t Length() const;
        char&  operator[](size_t);

        static int nCopies;
        static int nAllocs;
    private:
        void Reserve( size_t );
        char*    buf_;           // allocated buffer
        size_t   len_;           // length of buffer
        size_t   used_;          // # chars actually used
    };

    int String::nCopies;
    int String::nAllocs;

    String::String() : buf_(0), len_(0), used_(0) { }

    String::~String() { delete[] buf_; }

    String::String( const String& other )
    : buf_(new char[other.len_]),
      len_(other.len_),
      used_(other.used_)
    {
      memcpy( buf_, other.buf_, used_ );
      ++nCopies;
      ++nAllocs;
    }

    inline void String::Clear() {
      delete[] buf_;
      buf_ = 0;
      len_ = 0;
      used_ = 0;
    }

    inline void String::Reserve( size_t n ) {
      if( len_ < n ) {
        size_t needed = static_cast<size_t>(max(len_*1.5, static_cast<double>(n)));

        size_t newlen = needed ? 4 * ((needed-1)/4 + 1) : 0;
        char*  newbuf = newlen ? (++nAllocs, new char[ newlen ]) : 0;
        if( buf_ )
        {
            memcpy( newbuf, buf_, used_ );
        }

        delete[] buf_;  // now all the real work is
        buf_ = newbuf;  //  done, so take ownership
        len_ = newlen;
      }
    }

    inline void String::Append( char c ) {
      Reserve( used_+1 );
      buf_[used_++] = c;
    }

    inline size_t String::Length() const {
      return used_;
    }

    inline char& String::operator[]( size_t n ) {
      return *(buf_+n);
    }

  }





//------------------------------------------------------------------------------
//
// std::string
//
//------------------------------------------------------------------------------

  namespace StdString {

    class String {
    public:
        String();                // start off empty
       ~String();                // free the buffer
        String( const String& ); // take a full copy
        void Clear();
        void Append( char );     // append one character
        size_t Length() const;
        char&  operator[](size_t);

        // *** NOTE: Meaningless for std::string
        static int nCopies;
        static int nAllocs;
    private:
        std::string _s;
    };

    int String::nCopies;
    int String::nAllocs;

    String::String() { }

    String::~String() { }

    String::String( const String& other )
    : _s(other._s)
    {
    }

    inline void String::Clear() {
        _s.clear();
    }

    inline void String::Append( char c ) {
        _s += c;
    }

    inline size_t String::Length() const {
      return _s.size();
    }

    inline char& String::operator[]( size_t n ) {
      return _s[n];
    }

  }


  
//------------------------------------------------------------------------------
//
// ATL CStringA
//
//------------------------------------------------------------------------------

  namespace AtlString {

    class String {
    public:
        String();                // start off empty
       ~String();                // free the buffer
        String( const String& ); // take a full copy
        void Clear();
        void Append( char );     // append one character
        size_t Length() const;
        char  operator[](size_t) const; // char& not possible on CString

        // *** NOTE: Meaningless for CString
        static int nCopies;
        static int nAllocs;
    private:
        ATL::CStringA _s;
    };

    int String::nCopies;
    int String::nAllocs;

    String::String() { }

    String::~String() { }

    String::String( const String& other )
    : _s(other._s)
    {
    }

    inline void String::Clear() {
        _s.Empty();
    }

    inline void String::Append( char c ) {
        _s += c;
    }

    inline size_t String::Length() const {
      return _s.GetLength();
    }

    inline char String::operator[]( size_t n ) const {
      return _s.GetAt(static_cast<int>(n));
    }

  }





//------------------------------------------------------------------------------
//
//  Non-COW: Same as above, but optimized to use a more efficient allocator
//  instead of the built-in library allocator.
//
//------------------------------------------------------------------------------

  namespace Plain_FastAlloc {

    class String {
    public:
        String();                // start off empty
       ~String();                // free the buffer
        String( const String& ); // take a full copy
        void Clear();
        void Append( char );     // append one character
        size_t Length() const;
        char&  operator[](size_t);

        static int nCopies;
        static int nAllocs;
    private:
        void Reserve( size_t );
        char*    buf_;           // allocated buffer
        size_t   len_;           // length of buffer
        size_t   used_;          // # chars actually used
        static FastArena fa;
    };

    int String::nCopies;
    int String::nAllocs;
    FastArena String::fa( "Plain_FastAlloc" );

    String::String() : buf_(0), len_(0), used_(0) { }

    String::~String() { fa.Deallocate(buf_); }

    String::String( const String& other )
    : buf_((char*)fa.Allocate(other.len_)),
      len_(other.len_),
      used_(other.used_)
    {
      memcpy( buf_, other.buf_, used_ );
      ++nCopies;
      ++nAllocs;
    }

    inline void String::Clear() {
      fa.Deallocate(buf_);
      buf_ = 0;
      len_ = 0;
      used_ = 0;
    }

    inline void String::Reserve( size_t n ) {
      if( len_ < n ) {
        size_t needed = static_cast<size_t>(max(len_*1.5, static_cast<double>(n)));

        size_t newlen = needed ? 4 * ((needed-1)/4 + 1) : 0;
        char*  newbuf = newlen ? (++nAllocs, (char*)fa.Allocate(newlen)) : 0;
        if( buf_ )
        {
            memcpy( newbuf, buf_, used_ );
        }

        fa.Deallocate(buf_); // now all the real work is
        buf_ = newbuf;       //  done, so take ownership
        len_ = newlen;
      }
    }

    inline void String::Append( char c ) {
      Reserve( used_+1 );
      buf_[used_++] = c;
    }

    inline size_t String::Length() const {
      return used_;
    }

    inline char& String::operator[]( size_t n ) {
      return *(buf_+n);
    }

  }


//==============================================================================
//
//  COW: Initial thread-unsafe implementation.
//
//==============================================================================

  namespace COW_Unsafe {

    #undef  NAME
    #undef  BAGGAGE
    #define NAME    "COW_Unsafe"
    #define BAGGAGE
    #include "common-test.h" //****************************************************

    inline String::~String() {
      if( --data_->refs < 1 ) {
        delete data_;
      }
    }

    inline String::String( const String& other )
    {
      if( other.data_->refs > 0 ) {
        data_ = other.data_;
        ++data_->refs;
      } else {
        data_ = new StringBuf( *other.data_ );
      }
      ++nCopies;
    }

    inline void String::Clear() {
      if( data_->refs > 1 ) {
        --data_->refs;
        data_ = new StringBuf;
      } else {
        data_->Clear();
        data_->refs = 1; // shareable again
      }
    }

    inline void String::EnsureUnique( size_t n ) {
      if( data_->refs > 1 ) {
        StringBuf* newdata = new StringBuf( *data_, n );
        --data_->refs;   // now all the real work is
        data_ = newdata; //  done, so take ownership
      } else {
        data_->Reserve( n );
        data_->refs = 1; // shareable again
      }
    }

  }


//==============================================================================
//
//  COW: Safe implementation, using atomic integer manipulation functions.
//
//==============================================================================

  namespace COW_AtomicInt {

    #undef  NAME
    #undef  BAGGAGE
    #define NAME    "COW_AtomicInt"
    #define BAGGAGE
    #include "common-test.h" //****************************************************

    inline String::~String() {
      if( IntAtomicDecrement( data_->refs ) < 1 ) {
        delete data_;
      }
    }

    inline String::String( const String& other )
    {
      if( IntAtomicCompare( other.data_->refs, 0 ) > 0 ) {
        data_ = other.data_;
        IntAtomicIncrement( data_->refs );
      }
      else {
        data_ = new StringBuf( *other.data_ );
      }
      ++nCopies;
    }

    inline void String::Clear() {
      if( IntAtomicDecrement( data_->refs ) < 1 ) {
        data_->Clear();  // also covers case where two
        data_->refs = 1; //  threads are trying this at once
      }
      else {
        data_ = new StringBuf;
      }
    }

    inline void String::EnsureUnique( size_t n ) {
      if( IntAtomicCompare( data_->refs, 1 ) > 0 ) {
        StringBuf* newdata = new StringBuf( *data_, n );
        if( IntAtomicDecrement( data_->refs ) < 1 ) {
          delete newdata;  // just in case two threads
          data_->refs = 1; //  are trying this at once
        }
        else {             // now all the real work is
          data_ = newdata; //  done, so take ownership
        }
      }
      else {
        data_->Reserve( n );
        data_->refs = 1; // shareable again
      }
    }

  }


//==============================================================================
//
//  COW: Safe implementation, using atomic integer manipulation functions.
//       AND a single buffer containing both the StringBuf control data.
//
//       The only thing I'm not doing is optimizing the empty-string case,
//       because if I did that here I should also do it in the Plain case.
//       Since all implementations are doing it the same way (not optimizing
//       the empty-string case), they can be meaningfully compared. Besides,
//       the more you optimize the more complicated it gets.
//
//       MORAL: Never start optimizing before you: a) know you need to;
//              and b) know that you actually are!
//
//==============================================================================

  namespace COW_AtomicInt2 {

    //  The layout of the StringBuf object sits in the initial bytes of a
    //  dynamically-allocated char buffer of length sizeof(StringBuf)+len.
    //  If you want to change this "glommed" buffer's size, you have to make a
    //  new one... hence Reserve is no longer a member of StringBuf.
    //
    struct StringBuf {
        size_t   len;
        size_t   used;
        long     refs;
    };

    class String {
    public:
        String();
       ~String();
        String( const String& );
        void   Swap( String& ) throw();
        void   Clear();
        void   Append( char );
        size_t Length() const;
        char&  operator[](size_t);

        static int nCopies;
        static int nAllocs;

    private:
        char* Clone( char* olddata, size_t n = 0 );
        void  Reserve( size_t n );
        void  EnsureUnique( size_t n );
        void  EnsureUnshareable( size_t n );
        char* data_;
    };

    int String::nCopies;
    int String::nAllocs;

    #define LEN(x)   (((StringBuf*)(x))->len)
    #define USED(x)  (((StringBuf*)(x))->used)
    #define REFS(x)  (((StringBuf*)(x))->refs)
    #define BUF(x)   ((x) + sizeof(StringBuf))

    inline String::String()
      : data_( new char[ sizeof(StringBuf) ] )
    {
      ++nAllocs;
      LEN(data_)  = 0;
      USED(data_) = 0;
      REFS(data_) = 1;
    }

    inline String::~String() {
      if( IntAtomicDecrement( REFS(data_) ) < 1 ) {
        delete[] data_;
      }
    }

    inline String::String( const String& other )
    {
      if( IntAtomicCompare( REFS(other.data_), 0 ) > 0 ) {
        data_ = other.data_;
        IntAtomicIncrement( REFS(data_) );
      }
      else {
        data_ = Clone( other.data_ );
      }
      ++nCopies;
    }

    inline void String::Swap( String& other ) throw() {
      swap( data_, other.data_ );
    }

    inline void String::Clear() {
      String tmp;
      Swap( tmp );
    }

    inline void String::Append( char c ) {
      EnsureUnique( USED(data_)+1 );
      BUF(data_)[USED(data_)++] = c;
    }

    inline size_t String::Length() const {
      return USED(data_);
    }

    inline char& String::operator[]( size_t n ) {
      EnsureUnshareable( LEN(data_) );
      return *(BUF(data_)+n);
    }

    inline char* String::Clone( char* data, size_t n ) {
      size_t needed = static_cast<size_t>(max(LEN(data)*1.5, static_cast<double>(n)));

      size_t newlen = needed ? 4 * ((needed-1)/4 + 1) : 0;
      char*  newdata = ( ++nAllocs, new char[ sizeof(StringBuf) + newlen ] );
      memcpy( newdata, data, sizeof(StringBuf)+USED(data) );
      LEN(newdata)  = newlen;
      REFS(newdata) = 1;
      return newdata;
    }

    inline void String::Reserve( size_t n ) {
      if( LEN(data_) < n ) {
        char* newdata = Clone( data_, n );
        delete[] data_;
        data_ = newdata;
      }
    }

    inline void String::EnsureUnique( size_t n ) {
      if( IntAtomicCompare( REFS(data_), 1 ) > 0 ) {
        char* newdata = Clone( data_, n );
        if( IntAtomicDecrement( REFS(data_) ) < 1 ) {
          delete[] newdata; // just in case two threads
          REFS(data_) = 1;  //  are trying this at once
        }
        else {              // now all the real work is
          data_ = newdata;  //  done, so take ownership
        }
      }
      else {
        Reserve( n );
        REFS(data_) = 1; // shareable again
      }
    }

    inline void String::EnsureUnshareable( size_t n ) {
      EnsureUnique( n );
      REFS(data_) = -1;
    }

  }


//==============================================================================
//
//  COW: Safe implementation, using a critical section.
//
//==============================================================================

  namespace COW_CritSec {

    #undef  NAME
    #undef  BAGGAGE
    #define NAME    "COW_CritSec"
    #define BAGGAGE CriticalSection cs
    #include "common-test.h" //****************************************************

    inline String::~String() {
      bool bDelete = false;
      Lock<CriticalSection> l(data_->cs); //---------
      if( --data_->refs < 1 ) {
        bDelete = true;
      }
      l.Unlock(); //---------------------------------
      if( bDelete ) {
        delete data_;
      }
    }

    inline String::String( const String& other )
    {
      Lock<CriticalSection> l(other.data_->cs); //---
      if( other.data_->refs > 0 ) {
        data_ = other.data_;
        ++data_->refs;
        l.Unlock(); //-------------------------------
      }
      else {
        l.Unlock(); //-------------------------------
        data_ = new StringBuf( *other.data_ );
      }
      ++nCopies;
    }

    inline void String::Clear() {
      Lock<CriticalSection> l(data_->cs); //---------
      if( data_->refs > 1 ) {
        --data_->refs;
        l.Unlock(); //-------------------------------
        data_ = new StringBuf;
      } else {
        l.Unlock(); //-------------------------------
        data_->Clear();
        data_->refs = 1; // shareable again
      }
    }

    inline void String::EnsureUnique( size_t n ) {
      Lock<CriticalSection> l(data_->cs); //---------
      if( data_->refs > 1 ) {
        StringBuf* newdata = new StringBuf( *data_, n );
        --data_->refs;
        l.Unlock(); //-------------------------------
        data_ = newdata;
      }
      else {
        l.Unlock(); //-------------------------------
        data_->Reserve( n );
        data_->refs = 1; // shareable again
      }
    }

  }


//==============================================================================
//
//  COW: Safe implementation, using a mutex.
//
//==============================================================================

  namespace COW_Mutex {

    #undef  NAME
    #undef  BAGGAGE
    #define NAME    "COW_Mutex"
    #define BAGGAGE Mutex m
    #include "common-test.h" //****************************************************

    inline String::~String() {
      bool bDelete = false;
      Lock<Mutex> l(data_->m); //-------------------
      if( --data_->refs < 1 ) {
        bDelete = true;
      }
      l.Unlock(); //--------------------------------
      if( bDelete ) {
        delete data_;
      }
    }

    inline String::String( const String& other )
    {
      Lock<Mutex> l(other.data_->m); //-------------
      if( other.data_->refs > 0 ) {
        data_ = other.data_;
        ++data_->refs;
        l.Unlock(); //------------------------------
      }
      else {
        l.Unlock(); //------------------------------
        data_ = new StringBuf( *other.data_ );
      }
      ++nCopies;
    }

    inline void String::Clear() {
      Lock<Mutex> l(data_->m); //-------------------
      if( data_->refs > 1 ) {
        --data_->refs;
        l.Unlock(); //------------------------------
        data_ = new StringBuf;
      }
      else {
        l.Unlock(); //------------------------------
        data_->Clear();
        data_->refs = 1; // shareable again
      }
    }

    inline void String::EnsureUnique( size_t n ) {
      Lock<Mutex> l(data_->m); //-------------------
      if( data_->refs > 1 ) {
        StringBuf* newdata = new StringBuf( *data_, n );
        --data_->refs;
        l.Unlock(); //------------------------------
        data_ = newdata;
      }
      else {
        l.Unlock(); //------------------------------
        data_->Reserve( n );
        data_->refs = 1; // shareable again
      }
    }

  }


//==============================================================================
//
//  Test harness.
//
//==============================================================================

ofstream out( "test.out" ); // to ensure there's a 'counter' side-effect

template<class S>
int Test( S& s, long n, long l )
{
    long i = 0, counter = 0;
    for( i = 0; i < l; ++i )    // initialize s to length l (for copying tests)
    {
        s.Append( 'X' );
    }

    S::nAllocs = 0;
    S::nCopies = 0;

    n /= 25;    // the inner loop has 25 cycles per outer loop, so this will
                //  give us the right number of iterations.
    Timer t;    // *** start timing

    for( i = 0; i < n; ++i )
    {
        for( char c = 'a'; c <= 'y'; ++c )
        {
#if defined TEST_CONST_COPY
            //  Simple const copy (cost: copy + destruct)
            S s2( s );
#elif defined TEST_APPEND
            //  Simple appending
            if( s.Length() > static_cast<size_t>(l) )
            {
                s.Clear();
            }
            s.Append( c );
#elif defined TEST_OPERATOR
            //  Simple nonmutating access
            counter += s[0];
#elif defined TEST_MUTATING_COPY_2A
            //  33% of copies are const (cost: copy ctor + dtor),
            //  rest are modified once (cost: copy ctor + deep copy +
            //                                Append/op[] + dtor)
            S s2( s );
            if( i % 3 == 0 ) {
              counter += s2[0];
            }
            else if( i % 3 == 1 ) {
              s2.Append( c );
            }
#elif defined TEST_MUTATING_COPY_2B
            //  50% of copies are const (cost: copy ctor + dtor),
            //  rest are modified thrice (cost: copy ctor + deep copy +
            //                                  3*Append/op[] + dtor)
            S s2( s );
            if( i % 4 == 0 ) {
              counter += s2[0];
              counter += s2[1];
              counter += s2[2];
            }
            else if( i % 4 == 1 ) {
              s2.Append( c );
              s2.Append( c );
              s2.Append( c );
            }
#endif
        }
    }

    int ret = t.Elapsed();
    out << "counter = " << counter << endl; // don't let the compiler optimize
                                            // away op[] and ruin our test case

    return ret;
}


#if defined TEST_INT_OPS_ONLY

#pragma optimize( "g", off )    // the integer loop tests aren't useful with
                                // optimizations on, because the whole first
                                // loop will just get optimized away... we
                                // don't want that, we really want to measure
                                // how long it takes to increment an int!
void TestPlainIntOps( long n, long l )
{
    // -- NOTE: This test is not meaningful unless compiled with
    //          optimizations disabled. See the pragma above, or
    //          change /Ox to /Od in Build.BAT.
    {
      long counter = 0;
      Timer t;
      while( counter < l )
      {
        ++counter;
        ++counter;
        ++counter;
        ++counter;
        ++counter;
        ++counter;
        ++counter;
        ++counter;
        ++counter;
        ++counter;
      }
      cout << "  " << setw(15) << "++plain" << setw(7)
           << t.Elapsed() << "ms, counter=" << counter << endl;
    }
    {
      long counter = l;
      Timer t;
      while( counter > 0 )
      {
        --counter;
        --counter;
        --counter;
        --counter;
        --counter;
        --counter;
        --counter;
        --counter;
        --counter;
        --counter;
      }
      cout << "  " << setw(15) << "--plain" << setw(7)
           << t.Elapsed() << "ms, counter=" << counter << endl;
    }
}
#pragma optimize( "", on )      // reset optimizations to original values

void TestIntOps( long n, long l )
{
    for( int j = 0; j < n; ++j )
    {
      int nPlain = 0;
      {
        TestPlainIntOps( n, l );
      }
      cout << endl;

      {
        volatile long counter = 0;
        Timer t;
        while( counter < l )
        {
          ++counter;
          ++counter;
          ++counter;
          ++counter;
          ++counter;
          ++counter;
          ++counter;
          ++counter;
          ++counter;
          ++counter;
        }
        cout << "  " << setw(15) << "++volatile" << setw(7)
             << t.Elapsed() << "ms, counter=" << counter << endl;
      }
      {
        volatile long counter = l;
        Timer t;
        while( counter > 0 )
        {
          --counter;
          --counter;
          --counter;
          --counter;
          --counter;
          --counter;
          --counter;
          --counter;
          --counter;
          --counter;
        }
        cout << "  " << setw(15) << "--volatile" << setw(7)
             << t.Elapsed() << "ms, counter=" << counter << endl;
      }
      cout << endl;

      {
        long counter = 0;
        Timer t;
        while( counter < l )
        {
          IntAtomicIncrement(counter);
          IntAtomicIncrement(counter);
          IntAtomicIncrement(counter);
          IntAtomicIncrement(counter);
          IntAtomicIncrement(counter);
          IntAtomicIncrement(counter);
          IntAtomicIncrement(counter);
          IntAtomicIncrement(counter);
          IntAtomicIncrement(counter);
          IntAtomicIncrement(counter);
        }
        cout << "  " << setw(15) << "++atomic" << setw(7)
             << t.Elapsed() << "ms, counter=" << counter << endl;
      }
      {
        long counter = l;
        Timer t;
        while( counter > 0 )
        {
          IntAtomicDecrement(counter);
          IntAtomicDecrement(counter);
          IntAtomicDecrement(counter);
          IntAtomicDecrement(counter);
          IntAtomicDecrement(counter);
          IntAtomicDecrement(counter);
          IntAtomicDecrement(counter);
          IntAtomicDecrement(counter);
          IntAtomicDecrement(counter);
          IntAtomicIncrement(counter);
        }
        cout << "  " << setw(15) << "--atomic" << setw(7)
             << t.Elapsed() << "ms, counter=" << counter << endl;
      }
      cout << endl;

      {
        long counter = 0;
        Timer t;
        while( counter < l )
        {
          IntAtomicIncrementAss(counter);
          IntAtomicIncrementAss(counter);
          IntAtomicIncrementAss(counter);
          IntAtomicIncrementAss(counter);
          IntAtomicIncrementAss(counter);
          IntAtomicIncrementAss(counter);
          IntAtomicIncrementAss(counter);
          IntAtomicIncrementAss(counter);
          IntAtomicIncrementAss(counter);
          IntAtomicIncrementAss(counter);
        }
        cout << "  " << setw(15) << "++atomic_ass" << setw(7)
             << t.Elapsed() << "ms, counter=" << counter << endl;
      }
      {
        long counter = l, result = 0;
        Timer t;
        while( counter > 0 )
        {
          IntAtomicDecrementAss(counter, result);
          IntAtomicDecrementAss(counter, result);
          IntAtomicDecrementAss(counter, result);
          IntAtomicDecrementAss(counter, result);
          IntAtomicDecrementAss(counter, result);
          IntAtomicDecrementAss(counter, result);
          IntAtomicDecrementAss(counter, result);
          IntAtomicDecrementAss(counter, result);
          IntAtomicDecrementAss(counter, result);
          IntAtomicDecrementAss(counter, result);
        }
        cout << "  " << setw(15) << "--atomic_ass" << setw(7)
             << t.Elapsed() << "ms, counter=" << counter << endl;
      }
      cout << endl;
    }
}

#endif

int main( int argc, char* argv[] )
{
    long nRuns = 2, nLoops = 1000 * 1000, nLen = 100;

    if( argc > 1)
    {
        nRuns = atol( argv[1] );
    }
    if( argc > 2)
    {
        nLoops = atol( argv[2] );
    }
    if( argc > 3)
    {
        nLen = atol( argv[3] );
    }

    cout << "Preparing for clean timing runs... ";
    Sleep( 1000 );
    Plain::String throwawayString;
    Test( throwawayString, 10000, 10 ); // throwaway work

#if !defined TEST_INT_OPS_ONLY

    cout << "done.\nRunning " << nLoops << " iterations with strings of length " << nLen << ":\n\n";

    // Create a local variable testString instead of using VC++'s non-standard extension
    // (conversion from X to X&)
    #define RUN_TEST( TEST_NAME ) \
    { \
        TEST_NAME::String testString; \
        cout << "  " << setw(15) << #TEST_NAME; \
        cout << setw(7) << Test(testString, nLoops, nLen ); \
        cout << "ms  copies:" << setw(8) << TEST_NAME::String::nCopies \
             << "  allocs:" << setw(8) << TEST_NAME::String::nAllocs \
             << endl; \
    }


    for( int i = 1; i <= nRuns; ++i )
    {
        RUN_TEST( Plain_FastAlloc );
        RUN_TEST( Plain );
        RUN_TEST( COW_Unsafe );
        RUN_TEST( COW_AtomicInt );
        RUN_TEST( COW_AtomicInt2 );
        RUN_TEST( COW_CritSec );
        RUN_TEST( COW_Mutex );
        
        RUN_TEST( StdString );
        RUN_TEST( AtlString );

        cout << endl;
    }

#else

    cout << "done.\nRunning " << nLoops << " iterations for integer operations:\n\n";
    TestIntOps( nRuns, nLoops );

#endif

    return 0;
}
