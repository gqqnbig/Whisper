#include "stdafx.h"
#include "testUtils.h"
#include <immintrin.h>
#include <atlfile.h>
#include <atlpath.h>

namespace
{
	using DirectCompute::sTensorDiff;

	__forceinline __m256 load( const float* rsi )
	{
		return _mm256_loadu_ps( rsi );
	}

	__forceinline __m256 load( const uint16_t* rsi )
	{
		const __m128i iv = _mm_load_si128( ( const __m128i* )rsi );
		return _mm256_cvtph_ps( iv );
	}

	__forceinline void loadPartial( const uint16_t* x, const uint16_t* y, size_t count, __m256& fx, __m256& fy )
	{
		__m128i ix, iy;
		switch( count )
		{
		case 1: // load 2 bytes
			ix = _mm_cvtsi32_si128( *x );
			iy = _mm_cvtsi32_si128( *y );
			break;
		case 2: // load 4 bytes
			ix = _mm_cvtsi32_si128( *(const int*)x );
			iy = _mm_cvtsi32_si128( *(const int*)y );
			break;
		case 3: // load 6 bytes
			ix = _mm_cvtsi32_si128( *(const int*)x );
			iy = _mm_cvtsi32_si128( *(const int*)y );
			ix = _mm_insert_epi16( ix, x[ 2 ], 2 );
			iy = _mm_insert_epi16( iy, y[ 2 ], 2 );
			break;
		case 4: // load 8 bytes
			ix = _mm_cvtsi64_si128( *(const int64_t*)x );
			iy = _mm_cvtsi64_si128( *(const int64_t*)y );
			break;
		case 5: // load 10 bytes
			ix = _mm_cvtsi64_si128( *(const int64_t*)x );
			iy = _mm_cvtsi64_si128( *(const int64_t*)y );
			ix = _mm_insert_epi16( ix, x[ 4 ], 4 );
			iy = _mm_insert_epi16( iy, y[ 4 ], 4 );
			break;
		case 6: // load 12 bytes
			ix = _mm_cvtsi64_si128( *(const int64_t*)x );
			iy = _mm_cvtsi64_si128( *(const int64_t*)y );
			ix = _mm_insert_epi32( ix, *(const int*)( x + 4 ), 2 );
			iy = _mm_insert_epi32( iy, *(const int*)( y + 4 ), 2 );
			break;
		case 7: // load 14 bytes
			ix = _mm_cvtsi64_si128( *(const int64_t*)x );
			iy = _mm_cvtsi64_si128( *(const int64_t*)y );
			ix = _mm_insert_epi32( ix, *(const int*)( x + 4 ), 2 );
			iy = _mm_insert_epi32( iy, *(const int*)( y + 4 ), 2 );
			ix = _mm_insert_epi16( ix, x[ 6 ], 6 );
			iy = _mm_insert_epi16( iy, y[ 6 ], 6 );
			break;
		default:
			fx = fy = _mm256_setzero_ps();
			return;
		}

		fx = _mm256_cvtph_ps( ix );
		fy = _mm256_cvtph_ps( iy );
	}

	inline __m128 loadFloat2( const float* rsi )
	{
		return _mm_castpd_ps( _mm_load_sd( (const double*)rsi ) );
	}
	inline __m128 loadFloat3( const float* rsi )
	{
		__m128 f = loadFloat2( rsi );
		f = _mm_insert_ps( f, _mm_load_ss( rsi + 2 ), 0x20 );
		return f;
	}
	__forceinline void loadPartial( const float* x, const float* y, size_t count, __m256& fx, __m256& fy )
	{
		__m128 low1, high1;
		__m128 low2, high2;
		high1 = high2 = _mm_setzero_ps();
		switch( count )
		{
		case 1:
			low1 = _mm_load_ss( x );
			low2 = _mm_load_ss( y );
			break;
		case 2:
			low1 = loadFloat2( x );
			low2 = loadFloat2( y );
			break;
		case 3:
			low1 = loadFloat3( x );
			low2 = loadFloat3( y );
			break;
		case 4:
			low1 = _mm_loadu_ps( x );
			low2 = _mm_loadu_ps( y );
			break;
		case 5:
			low1 = _mm_loadu_ps( x );
			low2 = _mm_loadu_ps( y );
			high1 = _mm_load_ss( x + 4 );
			high2 = _mm_load_ss( y + 4 );
			break;
		case 6:
			low1 = _mm_loadu_ps( x );
			low2 = _mm_loadu_ps( y );
			high1 = loadFloat2( x + 4 );
			high2 = loadFloat2( y + 4 );
			break;
		case 7: // load 14 bytes
			low1 = _mm_loadu_ps( x );
			low2 = _mm_loadu_ps( y );
			high1 = loadFloat3( x + 4 );
			high2 = loadFloat3( y + 4 );
			break;
		default:
			fx = fy = _mm256_setzero_ps();
			return;
		}

		fx = _mm256_setr_m128( low1, high1 );
		fy = _mm256_setr_m128( low2, high2 );
	}

	__forceinline float horizontalMaximum( __m256 v )
	{
		__m128 s = _mm256_extractf128_ps( v, 1 );
		s = _mm_max_ps( s, _mm256_castps256_ps128( v ) );
		s = _mm_max_ps( s, _mm_movehl_ps( s, s ) );
		s = _mm_max_ss( s, _mm_movehdup_ps( s ) );
		return _mm_cvtss_f32( s );
	}

	__forceinline double horizontalSum( __m256 v )
	{
		__m256d d = _mm256_cvtps_pd( _mm256_extractf128_ps( v, 1 ) );
		d = _mm256_add_pd( d, _mm256_cvtps_pd( _mm256_castps256_ps128( v ) ) );

		__m128d s = _mm256_extractf128_pd( d, 1 );
		s = _mm_add_pd( s, _mm256_castpd256_pd128( d ) );
		s = _mm_add_sd( s, _mm_unpackhi_pd( s, s ) );
		return _mm_cvtsd_f64( s );
	}

	__m256 maskInfNan( __m256 diff, __m256 a, __m256 b )
	{
		__m256i ai = _mm256_castps_si256( a );
		__m256i bi = _mm256_castps_si256( b );
		__m256i eqi = _mm256_cmpeq_epi32( ai, bi );
		__m256 eq = _mm256_castsi256_ps( eqi );
		return _mm256_andnot_ps( eq, diff );
	}

	class DiffAcc
	{
		__m256 maxAbs = _mm256_setzero_ps();
		__m256 sumSquares = _mm256_setzero_ps();

	public:

		__forceinline void add( __m256 a, __m256 b )
		{
			const __m256 neg0 = _mm256_set1_ps( -0.0f );
			__m256 diff = _mm256_sub_ps( b, a );
			diff = maskInfNan( diff, a, b );
			sumSquares = _mm256_fmadd_ps( diff, diff, sumSquares );
			const __m256 absDiff = _mm256_andnot_ps( neg0, diff );
			maxAbs = _mm256_max_ps( maxAbs, absDiff );
		}

		__forceinline sTensorDiff reduce( size_t count )
		{
			sTensorDiff res;
			res.maxAbsDiff = horizontalMaximum( maxAbs );
			res.avgDiffSquared = (float)( horizontalSum( sumSquares ) / (double)(int64_t)count );
			res.length = count;
			return res;
		}
	};

	template<class E>
	static sTensorDiff __declspec( noinline ) diffVectors( const E* a, const E* b, size_t length )
	{
		// const E* const aEnd = a + length;
		const E* const aEndAligned = a + ( length / 8 ) * 8;
		const size_t remainder = length % 8;

		DiffAcc acc;
		for( ; a < aEndAligned; a += 8, b += 8 )
			acc.add( load( a ), load( b ) );

		if( remainder != 0 )
		{
			__m256 va, vb;
			loadPartial( a, b, remainder, va, vb );
			acc.add( va, vb );
		}

		return acc.reduce( length );
	}
}

sTensorDiff DirectCompute::computeDiff( const float* a, const float* b, size_t length )
{
	return diffVectors( a, b, length );
}

sTensorDiff DirectCompute::computeDiff( const uint16_t* a, const uint16_t* b, size_t length )
{
	return diffVectors( a, b, length );
}

void DirectCompute::sTensorDiff::print( const char* what ) const
{
	logDebug( u8"%s: length %zu, maxAbsDiff = %g, avgDiffSquared = %g", what, length, maxAbsDiff, avgDiffSquared );
}
void DirectCompute::sTensorDiff::print() const
{
	logDebug( u8"%zu elements, maxAbsDiff = %g, avgDiffSquared = %g", length, maxAbsDiff, avgDiffSquared );
}

HRESULT DirectCompute::dbgWriteBinaryFile( LPCTSTR fileName, const void* rsi, size_t cb )
{
	CPath path;
	path.m_strPath = LR"(C:\Temp\2remove\Whisper)";
	path.Append( fileName );

	CAtlFile file;
	CHECK( file.Create( path, GENERIC_WRITE, 0, CREATE_ALWAYS ) );
	CHECK( file.Write( rsi, (DWORD)cb ) );
	CHECK( file.Flush() );
	return S_OK;
}

#include "Tensor.h"

sTensorDiff DirectCompute::computeDiff( const Tensor& a, const Tensor& b )
{
	assert( isSameShapeAndLayout( a, b ) );
	const eDataType dt = a.getType();
	assert( dt == b.getType() );
	switch( dt )
	{
	case eDataType::FP32:
	{
		std::vector<float> v1, v2;
		a.download( v1 );
		b.download( v2 );
		assert( v1.size() == v2.size() );
#if 0
		const size_t firstZero = std::find( v2.begin(), v2.end(), 0.0f ) - v2.begin();
		
		std::vector<float> delta;
		delta.resize( v1.size() );
		for( size_t i = 0; i < v1.size(); i++ )
			delta[ i ] = std::abs( v1[ i ] - v2[ i ] );
		const size_t maxIndex = std::max_element( delta.begin(), delta.end() ) - delta.begin();
#endif
		return computeDiff( v1.data(), v2.data(), v1.size() );
	}
	}
	throw E_NOTIMPL;
}

using namespace DirectCompute;

void PrintUniqueTensorSizes::printImpl( const std::array<uint32_t, 8>& a )
{
	auto pair = set.emplace( a );
	if( !pair.second )
		return;	// was already there

	const __m128i rhs = _mm_loadu_si128( ( const __m128i* ) ( &a[ 4 ] ) );

	if( _mm_testz_si128( rhs, rhs ) )
	{
		logDebug( u8"%s: [ %i, %i, %i, %i ]", what,
			a[ 0 ], a[ 1 ], a[ 2 ], a[ 3 ] );
	}
	else
	{
		logDebug( u8"%s: [ %i, %i, %i, %i ], [ %i, %i, %i, %i ]", what,
			a[ 0 ], a[ 1 ], a[ 2 ], a[ 3 ], a[ 4 ], a[ 5 ], a[ 6 ], a[ 7 ] );
	}
}

void PrintUniqueTensorSizes::print( const Tensor& lhs, const Tensor& rhs )
{
	std::array<uint32_t, 8> arr;
	__m128i* const rdi = ( __m128i* )arr.data();
	_mm_storeu_si128( rdi, lhs.sizeVec() );
	_mm_storeu_si128( rdi + 1, rhs.sizeVec() );

	printImpl( arr );
}

void PrintUniqueTensorSizes::print( const int* lhs, const int* rhs )
{
	std::array<uint32_t, 8> arr;
	__m128i* const rdi = ( __m128i* )arr.data();
	_mm_storeu_si128( rdi, load16( lhs ) );
	_mm_storeu_si128( rdi + 1, load16( rhs ) );

	printImpl( arr );
}

void PrintUniqueTensorSizes::print( const Tensor& lhs )
{
	std::array<uint32_t, 8> arr;
	__m128i* const rdi = ( __m128i* )arr.data();
	_mm_storeu_si128( rdi, lhs.sizeVec() );
	_mm_storeu_si128( rdi + 1, _mm_setzero_si128() );

	printImpl( arr );
}

#include "testUtilsC.h"

void printUniqueTensorSize( const char* name, const int* lhs, const int* rhs )
{
	using TS = DirectCompute::PrintUniqueTensorSizes;
	static std::unordered_map<std::string, TS> map;
	TS& ts = map.try_emplace( name, name ).first->second;
	ts.print( lhs, rhs );
}