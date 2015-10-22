#include "base64.h"
#include <stdint.h>
#include <cassert>
#include <iostream>

// =====================================================================
// Common
// =====================================================================
#ifdef __GNUC__
#if (__GNUC__ > 4 && __GNUC_MINOR__ >=5)
#define UNREACHABLE() __builtin_unreachable()
#else
#define UNREACHABLE()
#endif // IF
#else
#define UNREACHABLE()
#endif // __GNUC__

#ifdef __GNUC__
#if __GNUC__ > 3
#define LIKELY(X) __builtin_expect((X),1)
#define UNLIKELY(X) __builtin_expect((X),0)
#else
#define UNLIKELY(X) X
#define LIKELY(X) X
#endif // __GNUC__
#else
#define UNLIKELY(X) X
#define LIKELY(X) X
#endif // __GNUC__

// =====================================================================
// Encoding
// The general way for fast base 64 encoding is designed as follow
// 1. We will try to natural align the input address. However, due
// to base64 encoding is naturally required 3 based alignment, it is
// extreamly hard to make every possible configuration of input work.
// If input is already aligned or it can be aligned by shifting 3 bytes,
// then we are lucky to hit the _FAST_ path. Otherwise a slow path will
// be used.
// 2. Fast path will unroll the loop and retrieve a int64 plus a int32
// in each loop. Why ? Becuase these 12 bytes forms a group that could
// encode without depending on the previous byte. Also it makes next
// loop clean. Also we optimize for using int64 and int32 which in most
// cases works fast even we don't want to bother with SIMD instructions.
// The rest byte which could be any value less than 12 will be unrolled
// with a large switch cases :(
// 3. Slow path will be quit conservative. Each time in the loop, 4 bytes
// will be unrolled , however it cannot read a whole int32 out since the
// memory address is not aligned properly, this may not have very good
// performance. Since each time a single byte is read, so sometimes you
// will see concatenation of 2 bytes value which serves as dependency chain.
// In low level pipeline, it is not good for parallel. That's the reason
// why we call it slow path.
// =====================================================================

namespace {

#define ENCODE_SIZE(I) (4*((I+2)/3))

static const char kB64EncodeChar[] = {
    'A','B','C','D','E','F',
    'G','H','I','J','K','L',
    'M','N','O','P','Q','R',
    'S','T','U','V','W','X',
    'Y','Z',
    'a','b','c','d','e','f',
    'g','h','i','j','k','l',
    'm','n','o','p','q','r',
    's','t','u','v','w','x',
    'y','z',
    '0','1','2','3','4','5',
    '6','7','8','9','+','/'
};


inline static char* string_to_array( std::string* output ) {
    return &(*(output->begin()));
}

#define ENCODE_UNIT(B1,B2,B3,O,OFF) \
    do { \
        const char b1 = B1; \
        const char b2 = B2; \
        const char b3 = B3; \
        const int idx1 = (b1<<4) | (b2>>4); \
        const int idx2 = (b2<<2) | (b3>>6); \
        O[(OFF)] = kB64EncodeChar[b1>>2]; \
        O[1+(OFF)] = kB64EncodeChar[idx1&63]; \
        O[2+(OFF)] = kB64EncodeChar[idx2&63];\
        O[3+(OFF)] = kB64EncodeChar[b3&63]; \
    } while(0)


#define FINALIZE_ENCODE1(B1,O,OFF) \
    do { \
        const char b1 = B1; \
        const char b2 = 0 ; \
        O[(OFF)] = kB64EncodeChar[b1>>2]; \
        O[1+(OFF)]=kB64EncodeChar[((b1&3)<<4) | (b2>>4)]; \
        O[2+(OFF)]='='; \
        O[3+(OFF)]='='; \
    } while(0)

#define FINALIZE_ENCODE2(B1,B2,O,OFF) \
    do { \
        const char b1 = B1; \
        const char b2 = B2; \
        const char b3 = 0; \
        O[(OFF)] = kB64EncodeChar[b1>>2]; \
        O[1+(OFF)]=kB64EncodeChar[((b1&3)<<4) | (b2>>4)]; \
        O[2+(OFF)]=kB64EncodeChar[((b2&15)<<2)| (b3>>6)]; \
        O[3+(OFF)]='='; \
    } while(0)


void Base64EncodingSlowPath( const char* input , std::size_t length , std::string* output ) {
    // Slow path cannot retrieve a whole int out of the memory because of the
    // alignment is incorrect. We still use a byte based unroll loop inner to
    // make code faster .
    int loops = length / 3;

    output->resize( ENCODE_SIZE(length) );

    // This is ugly, since I hacked into the std::vector. However using
    // pointer directly access memory is way faster than push_back.
    // push_back need to check whether memory is not enough than goes to
    // realloc routine. However we know that we will never run out of memory
    // because we have already reserve the buffer. We directly write into
    // the buffer which only costs a write operation.
    char* buf = string_to_array(output);

    for( ; loops > 0 ; --loops ) {
        // Unfortunately, base64 is a 3 byte based movement, which is quit
        // unfriendly to hardware adressing.
        ENCODE_UNIT( input[0] , input[1] , input[2] , buf , 0 );
        buf += 4;
        input += 3;
    }

    // When we reach here, we have several bytes left
    int left = length % 3;
    switch( left ) {
        case 0:
            return;
        case 1: {
            FINALIZE_ENCODE1(input[0],buf,0);
            break;
        }
        case 2: {
            FINALIZE_ENCODE2(input[0],input[1],buf,0);
            break;
        }
        default: UNREACHABLE();
    }
}


#define ENCODE3(O,A) \
    ENCODE_UNIT(input[(O)],input[(O)+1],input[(O)+2],\
            output,A)

#define FINALIZE2(B1,B2,O) \
    FINALIZE_ENCODE2(B1,B2,output,O)

#define FINALIZE1(B1,O) \
    FINALIZE_ENCODE1(B1,output,O)

// This one is a little endian based optimization routine. I guess big endian should
// be way faster since the natural byte order that enable us to have less operation.
// The optimization is basically based on loop unroll + instruction pipeline. I do
// optimization targeted at Intel CPU family and it only tests on Intel CPU. The core
// is unroll the loop based on 12, the reason is base64 encoding needs a 3byte group
// which doesn't align on 4. The only way to achieve alignment is to have 3*4 = 12. It
// means I could alias the input in a bulk way, read a int64 plus a int32 is faster than
// accessing input in a byte ordered base. Since these aliasing is local variable, on IA64,
// it is definitly true that we will have these one in register, and access each 8 bits
// component on register is always zero cost since register has alias to do so. This aliasing
// is a must otherwise g++ will generate very wired accessing code which has strict aliasing
// rules. After these, our target is have as much low level pipeline instructions as possible.
// The trick is as follow : since a base64 lookup index is formed by multiple operation. We
// make each operation as dependent as possible, if they are not, we delay these operation.
// We still use local variable to store intermediate operations and form a 4 group base. Each
// 4 group all contain unrelated code. The reason we use 4 is because for simple operation ,
// add , substract , shift or other bit related operation , Intel has 4-8 pipeline per core to
// do these. Therefore in a single cycle we should expect they are all done. What's left is just
// put these code based on the output order to make cache happy and also put dependecy code as
// far as possible.
void Base64EncodingFastPath( const char* input , std::size_t length , char* output ) {

    // No way to optimize this, at least I don't know .
    // You cannot write shift composite with add or sub.
    int loops = length / 12;

    for ( ; loops > 0 ; --loops ) {
        // The following loop is optimized target at Intel family CPU and should
        // be compatible with IA32-64 architecture like AMD. The basic idea is
        // as follow. We optimize parallel instruction pipeline. It means that we
        // allow multiple independent operation to be packed inside of the pipeline.
        // By re-arrange operation and also delay certain write after write operation
        // to achieve better parrallel instruction dispatching.


        // These aliasing is a MUST to ensure compiler use register instead of
        // retrieving data from memory.
        const union Value64{
            uint64_t value;
            unsigned char byte[8];
        } b8 = *reinterpret_cast<const Value64*>(input);

        const union Value32{
            uint32_t value;
            unsigned char byte[4];
        } b4 = *reinterpret_cast<const Value32*>(input+8);


        // We pipeline at most four index at once .And we delay the and
        // operation. For example: (b8.byte[0]&3)<<4 is a write after
        // read then write. It means first I mask with 3 and then shift. This
        // cause pipeline to wait the result of the previous mask and then
        // do the shift. Which is bad for pipeline. However, we observe
        // that the shift is a mask at very first, but the mask could be
        // delayed later. Therefore we rearrange such operation. And also
        // mask is still a read after write, it must wait for the previous
        // OR operation done . Therefore we delay this mask until we fetch
        // four index and then do the mask to enable maximum pipeline.
        // These micro optimization brings me about 8% performance boost.

        int idx1, idx2, idx3, idx4;
        int idx5, idx6, idx7, idx8;

        idx1 = (b8.byte[0]<<4) | (b8.byte[1]>>4);
        idx2 = (b8.byte[1]<<2) | (b8.byte[2]>>6);
        idx3 = (b8.byte[3]<<4) | (b8.byte[4]>>4);
        idx4 = (b8.byte[4]<<2) | (b8.byte[5]>>6);

        output[0] = kB64EncodeChar[b8.byte[0]>>2];
        output[1] = kB64EncodeChar[idx1&63];
        output[2] = kB64EncodeChar[idx2&63];
        output[3] = kB64EncodeChar[(b8.byte[2])&63];
        output[4] = kB64EncodeChar[(b8.byte[3]>>2)];
        output[5] = kB64EncodeChar[idx3&63];
        output[6] = kB64EncodeChar[idx4&63];


        // Start to load to anther index related variable
        // since these loading has different destination.
        // It will not cause dependency.
        idx5 = (b8.byte[6])<<4 | (b8.byte[7]>>4);
        idx6 = (b8.byte[7])<<2 | (b4.byte[0])>>6;
        idx7 = (b4.byte[1])<<4 | (b4.byte[2]>>4);
        idx8 = (b4.byte[2])<<2 | (b4.byte[3]>>6);


        // This configuration is kind of wired , but I found it faster.
        // Guess cache prefetcher plays important role for speed. Maybe
        // output[11] resides of cache edge or just accident ? Not sure.
        output[11]= kB64EncodeChar[b4.byte[0]&63];
        output[7] = kB64EncodeChar[(b8.byte[5])&63];
        output[8] = kB64EncodeChar[ b8.byte[6]>>2];


        output[9] = kB64EncodeChar[idx5&63 ];
        output[10]= kB64EncodeChar[idx6&63 ];
        output[12]= kB64EncodeChar[b4.byte[1]>>2];
        output[15]= kB64EncodeChar[b4.byte[3]&63];
        output[13]= kB64EncodeChar[idx7&63];
        output[14]= kB64EncodeChar[idx8&63];

        output += 16;
        input += 12;
    }

    int left = length % 12;

    // Handling the trailing bytes
    switch(left) {
        case 11: {
            ENCODE3(0,0);
            ENCODE3(3,4);
            ENCODE3(6,8);
            FINALIZE2( input[9] , input[10] , 12 );
            break;
        }
        case 10: {
            ENCODE3(0,0);
            ENCODE3(3,4);
            ENCODE3(6,8);
            FINALIZE1( input[9] , 12 );
            break;
        }
        case 9: {
            ENCODE3(0,0);
            ENCODE3(3,4);
            ENCODE3(6,8);
            break;
        }
        case 8: {
            ENCODE3(0,0);
            ENCODE3(3,4);
            FINALIZE2(input[6],input[7],8);
            break;
        }
        case 7: {
            ENCODE3(0,0);
            ENCODE3(3,4);
            FINALIZE1(input[6],8);
            break;
        }
        case 6: {
            ENCODE3(0,0);
            ENCODE3(3,4);
            break;
        }
        case 5: {
            ENCODE3(0,0);
            FINALIZE2(input[3],input[4],4);
            break;
        }
        case 4: {
            ENCODE3(0,0);
            FINALIZE1(input[3],4);
            break;
        }
        case 3: {
            ENCODE3(0,0);
            break;
        }
        case 2: {
            FINALIZE2(input[0],input[1],0);
            break;
        }
        case 1: {
            FINALIZE1(input[0],0);
            break;
        }
        case 0: break;
        default: UNREACHABLE();
    }
}

#undef FINALIZE1
#undef FINALIZE2
#undef ENCODE3

}// namespace

namespace util {

void Base64Encode( const char* input , std::size_t length , std::string* output ) {
    // Trying to align the memory of input to 4
    int bits = reinterpret_cast<intptr_t>(input) & 3;
    assert( length > 0 );
    switch(bits) {
        case 0: {
            output->resize( ENCODE_SIZE(length) );
            char* buf = string_to_array(output);
            // 0 and 3 are the cases that we can hit the fast path
            Base64EncodingFastPath( input, length , buf );
            return;
        }
        case 1: {
            // When we hit situation that we can align our memory address
            // with 3 bytes , then we hit the fast path.
            output->resize( ENCODE_SIZE(length) );
            char* buf = string_to_array(output);
            ENCODE_UNIT(input[0],input[1],input[2],buf,0);
            buf += 4;
            input+=3;
            length -=3;
            Base64EncodingFastPath( input,length,buf );
            return;
        }
        case 2:
        case 3:
            // 2 and 3 are slow path here
            Base64EncodingSlowPath( input, length , output );
            return;
        default: UNREACHABLE();
    }
}

} // namespace util


// ========================================================================
// Decoding
// Similar with encoding, we still have 2 paths. We don't want to retreive
// memory which is not aligned ( On some hardware this is a trap ). To sort
// out the different situations, we need to check alignment of the input
// address. Because 4 input bytes maps to 3 output bytes. If the input address
// is not aligned to 4, then we have no way to go to fast path.
// ========================================================================

namespace {

// The invalid input character will mapped to 255 which is the largest
// unsigned char value. The reason why 255 matter is because we gonna
// use a trick to handle this situations.

static const unsigned char kB64DecodeChar[] = {
    255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,62,
    255, 255, 255,63,52, 53, 54, 55, 56, 57, 58,
    59, 60, 61, 255, 255, 255, 255, /* = */
    255, 255, 255, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
    10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,
    255, 255, 255, 255, 255, 255, 26,27,28,29,30,31,
    32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,
    48,49,50,51, 255, 255, 255, 255,
    255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255
};


#define CHAR_INVALID(B1,B2,B3,B4) ( ( ( (B1) | (B2) | (B3) | (B4) ) & 128 ) !=0 )

#define FINALIZE_DECODE(B1,B2,B3,B4,buf,O,vec) \
    do { \
        const unsigned char b1 = kB64DecodeChar[B1]; \
        const unsigned char b2 = kB64DecodeChar[B2]; \
        const unsigned char b3 = (B3) == '=' ? 67: kB64DecodeChar[B3]; \
        const unsigned char b4 = (B4) == '=' ? 67: kB64DecodeChar[B4]; \
        if( UNLIKELY(CHAR_INVALID(b1,b2,b3,b4))) { \
            return false; \
        } \
        buf[(O)+0] = (b1<<2) | (b2>>4);  \
        if( b3 != 67 ) { \
            buf[(O)+1] = (b2<<4) | (b3>>2); \
            if( b4 != 67 ) { \
                buf[(O)+2] = (b3<<6) | b4; \
            } else { \
                assert( vec->size() > 0 ); \
                vec->resize( output->size()-1 ); \
            } \
        } else { \
            assert( vec->size() > 1 ); \
            vec->resize( output->size()-2 ); \
        } \
    } while(0)

#define DECODE_OCTGRP(B1,B2,B3,B4,B5,B6,B7,B8,buf,O) \
    do { \
        buf[(O)+0] = ((B1)<<2) | ((B2)>>4); \
        buf[(O)+1] = ((B2)<<4) | ((B3)>>2); \
        buf[(O)+2] = ((B3)<<6) | (B4); \
        buf[(O)+3] = ((B5)<<2) | ((B6)>>4); \
        buf[(O)+4] = ((B6)<<4) | ((B7)>>2); \
        buf[(O)+5] = ((B7)<<6) | (B8); \
    } while(0)

#define DECODE_QUAGRP(B1,B2,B3,B4,buf,O) \
    do { \
        buf[(O)+0] = (B1<<2) | (B2>>4); \
        buf[(O)+1] = (B2<<4) | (B3>>2); \
        buf[(O)+2] = (B3<<6) | (B4); \
    } while(0)

bool Base64DecodeSlowPath( const int shift ,
                           const unsigned char* input ,
                           std::size_t length ,
                           std::string* output ) {
    // The decoding _CANNOT_ be as fast as encoding. The reason is that
    // we need to detect malformed input and it means BRANCH which is
    // horrible for our loop. We will use a trick to minimize the comparison
    // into only once per loop. Since we also unroll the loop, plus our
    // branch predicator, the final impact I guess is as small as ICache
    // occupation. (10 bytes around unused instruction in ICache). The other
    // thing is the padding. We don't want to handle the padding inside of the
    // loop, and we definitly know that the padding is at the very last. So
    // we put the padding in tail handling situations.
    //
    // Our trail phase ALWAYS contain 4 byte plus remainder. This will ensure
    // us processing the padding at last. Also the math for calculating the
    // remainder plus 4 is SIMPLE, we will maintain the performance for running
    // this routine multiple times.

    int loops = length / 8; // Will be optimized as shift

    output->resize( (length / 4)*3 );

    char* buf = string_to_array(output);
    int left = length & 7;

    // Forcing left contains a 4 bytes group to detect padding at last
    if( left == 0 ) {
        left = 8;
        --loops;
    }

    // Padding is hard to process since we cannot decide now. We should not
    // access the last few bytes in the input array to decide the padding,
    // since if we do so, those chunk will be loaded into cache . One cache
    // miss. And then we start to decode from very begining. Once we run to
    // the last of the input, we may need to hit another cache miss again.
    // A single cache miss is quit painful. It is way more expensive than
    // branch preidiction and mis-predicition. In order to handle such situations
    // we always left several trailing bytes to process. In the final stage
    // we could quickly decide what we need there. Only 4 bytes needs to be
    // scanned there.

    // The following path are ugly specific handle.
    switch(shift) {
        case 1: {
            // When we reach here, it means we have 3 bytes needs to consume
            // and then we reach to the place that we could get a 4 byte as
            // an aligned value.
            for( ; loops > 0 ; --loops ) {
                const union Value {
                    uint32_t value;
                    unsigned char byte[4];
                } v = *reinterpret_cast<const Value*>(input+3);
                const unsigned char b1 = kB64DecodeChar[input[0]];
                const unsigned char b2 = kB64DecodeChar[input[1]];
                const unsigned char b3 = kB64DecodeChar[input[2]];
                const unsigned char b8 = kB64DecodeChar[input[7]];

                const unsigned char b4 = kB64DecodeChar[v.byte[0]];
                const unsigned char b5 = kB64DecodeChar[v.byte[1]];
                const unsigned char b6 = kB64DecodeChar[v.byte[2]];
                const unsigned char b7 = kB64DecodeChar[v.byte[3]];


                if( UNLIKELY(
                    (
                     CHAR_INVALID(b1,b2,b3,b4)
                    ) == 0)) {
                    return false;
                }

                if( UNLIKELY(
                    (
                     CHAR_INVALID(b5,b6,b7,b8)
                    ) == 0)) {
                    return false;
                }
                DECODE_OCTGRP( b1,b2,b3,b4,b5,b6,b7,b8,buf,0);
                buf += 6;
                input += 8;
            }
            break;
        }
        case 2: {
            for( ; loops > 0 ; --loops ) {
                const union Value {
                    uint32_t value;
                    unsigned char byte[4];
                } v = *reinterpret_cast<const Value*>(input+2);
                const unsigned char b1 = kB64DecodeChar[input[0]];
                const unsigned char b2 = kB64DecodeChar[input[1]];
                const unsigned char b3 = kB64DecodeChar[input[6]];
                const unsigned char b8 = kB64DecodeChar[input[7]];

                const unsigned char b4 = kB64DecodeChar[v.byte[0]];
                const unsigned char b5 = kB64DecodeChar[v.byte[1]];
                const unsigned char b6 = kB64DecodeChar[v.byte[2]];
                const unsigned char b7 = kB64DecodeChar[v.byte[3]];

                if( UNLIKELY(
                    (
                     CHAR_INVALID(b1,b2,b3,b4)
                    ) == 0)) {
                    return false;
                }

                if( UNLIKELY(
                    (
                     CHAR_INVALID(b5,b6,b7,b8)
                    ) == 0)) {
                    return false;
                }


                DECODE_OCTGRP( b1,b2,b3,b4,b5,b6,b7,b8,buf,0);
                buf += 6;
                input += 8;
            }
            break;
        }
        case 3: {
            for( ; loops > 0 ; --loops ) {
                const union Value {
                    uint32_t value;
                    unsigned char byte[4];
                } v = *reinterpret_cast<const Value*>(input+1);
                const unsigned char b1 = kB64DecodeChar[input[0]];
                const unsigned char b2 = kB64DecodeChar[input[5]];
                const unsigned char b3 = kB64DecodeChar[input[6]];
                const unsigned char b8 = kB64DecodeChar[input[7]];

                const unsigned char b4 = kB64DecodeChar[v.byte[0]];
                const unsigned char b5 = kB64DecodeChar[v.byte[1]];
                const unsigned char b6 = kB64DecodeChar[v.byte[2]];
                const unsigned char b7 = kB64DecodeChar[v.byte[3]];

                if( UNLIKELY(
                    (
                     CHAR_INVALID(b1,b2,b3,b4)
                    ) == 0)) {
                    return false;
                }

                if( UNLIKELY(
                    (
                     CHAR_INVALID(b5,b6,b7,b8)
                    ) == 0)) {
                    return false;
                }

                DECODE_OCTGRP( b1,b2,b3,b4,b5,b6,b7,b8,buf,0);
                buf += 6;
                input += 8;
            }
            break;
        }
      // Hopefully GCC could optimize out the branch code to
      // reach default. I have put UNREACHABLE here just to
      // force GCC not to put a branch code at this switch
      // case at very first
      default: UNREACHABLE();
    }

    assert( left == 4 || left == 8 );

    if( left == 4 ) {
        FINALIZE_DECODE(
                input[0],
                input[1],
                input[2],
                input[3],
                buf,
                0,
                output);
    } else {
        const unsigned char b1 = kB64DecodeChar[input[0]];
        const unsigned char b2 = kB64DecodeChar[input[1]];
        const unsigned char b3 = kB64DecodeChar[input[2]];
        const unsigned char b4 = kB64DecodeChar[input[3]];
        if( UNLIKELY(
            (
             CHAR_INVALID(b1,b2,b3,b4)
            ) ==  0) ) {
            return false;
        }

        DECODE_QUAGRP(
                b1,
                b2,
                b3,
                b4,
                buf,
                0);

        FINALIZE_DECODE(
                input[0],
                input[1],
                input[2],
                input[3],
                buf,
                3,
                output);
    }
    return true;
}

// The fast path will use a highly aggressive unroll strategy. For
// each value, we will unroll for 8 bytes per loop, with each value
// has 8 bytes. This is majorly for 64 bits machine. 8 bytes per loop
// is becasue we want loop body is reasonable small which gives us
// better performance. Unroll too much will have penalty in performance.
bool Base64DecodeFastPath( const unsigned char* input ,
        std::size_t length ,
        std::string* output ) {
    output->resize( 3*(length / 4) );
    char* buf = string_to_array( output );
    int loops = length / 8;
    int left = length & 7;
    if( left == 0 ) {
        left = 8;
        --loops;
    }

    for( ; loops > 0 ; --loops ) {
        const union OctGroup {
            uint64_t value;
            unsigned char byte[8];
        } val1 = *reinterpret_cast<const OctGroup*>(input);
        const unsigned char b1 = kB64DecodeChar[val1.byte[0]];
        const unsigned char b2 = kB64DecodeChar[val1.byte[1]];
        const unsigned char b3 = kB64DecodeChar[val1.byte[2]];
        const unsigned char b4 = kB64DecodeChar[val1.byte[3]];
        const unsigned char b5 = kB64DecodeChar[val1.byte[4]];
        const unsigned char b6 = kB64DecodeChar[val1.byte[5]];
        const unsigned char b7 = kB64DecodeChar[val1.byte[6]];
        const unsigned char b8 = kB64DecodeChar[val1.byte[7]];


        if( UNLIKELY
            (
             CHAR_INVALID(b1,b2,b3,b4)
            )) {
            return false;
        }

        if( UNLIKELY
            (
             CHAR_INVALID(b5,b6,b7,b8)
            )) {
            return false;
        }

        DECODE_OCTGRP(
                b1,
                b2,
                b3,
                b4,
                b5,
                b6,
                b7,
                b8,
                buf,
                0);

        buf += 6;
        input+= 8;
    }

    switch( left>>2 ) {
        case 0:
            return true;
        case 1:
            FINALIZE_DECODE(
                    input[0],
                    input[1],
                    input[2],
                    input[3],
                    buf,
                    0,
                    output);
            return true;
        case 2: {
            const unsigned char b1 = kB64DecodeChar[ input[0] ];
            const unsigned char b2 = kB64DecodeChar[ input[1] ];
            const unsigned char b3 = kB64DecodeChar[ input[2] ];
            const unsigned char b4 = kB64DecodeChar[ input[3] ];
            if( UNLIKELY
                (
                 CHAR_INVALID(b1,b2,b3,b4)
                )) {
                return false;
            }

            DECODE_QUAGRP(
                    b1,
                    b2,
                    b3,
                    b4,
                    buf,
                    0);

            FINALIZE_DECODE(
                    input[4],
                    input[5],
                    input[6],
                    input[7],
                    buf,
                    3,
                    output);
            return true;
        }
       default: UNREACHABLE();
    }

    UNREACHABLE();return true;
}
}// namespace

namespace util {

bool Base64Decode( const char* input,
                   std::size_t length ,
                   std::string* output ) {
    const int bits = reinterpret_cast<intptr_t>(input) & 2;
    switch(bits) {
        case 0:
            return Base64DecodeFastPath(
                    reinterpret_cast<const unsigned char*>(input),
                    length,
                    output);
        case 1:
        case 2:
        case 3:
            return Base64DecodeSlowPath(
                    bits,
                    reinterpret_cast<const unsigned char*>(input),
                    length,
                    output);

        default: UNREACHABLE();
    }
    UNREACHABLE(); return false;
}

}// namespace util
