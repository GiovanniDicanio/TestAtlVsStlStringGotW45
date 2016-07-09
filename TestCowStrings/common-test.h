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
//  Here's the code that's the same for most COW versions
//
//------------------------------------------------------------------------------

    struct StringBuf {
        StringBuf();
       ~StringBuf();
        StringBuf( const StringBuf& other, size_t n = 0 );

        void Clear();
        void Reserve( size_t n );

        char*    buf;
        size_t   len;
        size_t   used;
        long     refs;
        BAGGAGE;

        void* operator new( size_t n );
        void  operator delete( void* p );
    };

    static FastArena fa( NAME, sizeof(StringBuf) );
    void* StringBuf::operator new( size_t n )   { return fa.Allocate( n ); }
    void  StringBuf::operator delete( void* p ) { fa.Deallocate( p ); }

    inline StringBuf::StringBuf() : buf(0), len(0), used(0), refs(1) { }

    inline StringBuf::~StringBuf() { delete[] buf; }

    inline StringBuf::StringBuf( const StringBuf& other, size_t n )
      : buf(0), len(0), used(0), refs(1)
    {
        Reserve( max( other.len, n ) );
        memcpy( buf, other.buf, used );
        used = other.used;
    }

    inline void StringBuf::Clear() {
      delete[] buf;
      buf = 0;
      len = 0;
      used = 0;
    }

    class String {
    public:
        String();
       ~String();
        String( const String& );
        void   Clear();
        void   Append( char );
        size_t Length() const;
        char&  operator[](size_t);

        static int nCopies;
        static int nAllocs;
    private:
        void EnsureUnique( size_t n );
        void EnsureUnshareable( size_t n );
        StringBuf* data_;
    };

    int String::nCopies;
    int String::nAllocs;

    inline void StringBuf::Reserve( size_t n ) {
      if( len < n ) {
        size_t needed = static_cast<size_t>(max(len*1.5, static_cast<double>(n)));

        size_t newlen = needed ? 4 * ((needed-1)/4 + 1) : 0;
        char*  newbuf = newlen ? (++String::nAllocs, new char[ newlen ]) : 0;
        if( buf )
        {
            memcpy( newbuf, buf, used );
        }

        delete[] buf;
        buf = newbuf;
        len = newlen;
      }
    }

    inline String::String() : data_(new StringBuf) { }

    inline void String::Append( char c ) {
      EnsureUnique( data_->used+1 );
      data_->buf[data_->used++] = c;
    }

    inline size_t String::Length() const {
      return data_->used;
    }

    inline char& String::operator[]( size_t n ) {
      EnsureUnshareable( data_->len );
      return *(data_->buf+n);
    }

    inline void String::EnsureUnshareable( size_t n ) {
      EnsureUnique( n );
      data_->refs = -1;
    }

