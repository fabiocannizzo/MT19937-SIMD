#define TESTING
#define SIMD_EMULATION

#include "VRandGen.h"

#include "../SFMT-src-1.5.1/SFMT.h"

const uint32_t seedlength = 4;
const uint32_t seedinit[seedlength] = { 0x123, 0x234, 0x345, 0x456 };

const uint64_t nRandomTest = 50ul * 624 * 16;

extern "C" unsigned long genrand_int32();
extern "C" void init_by_array(unsigned long init_key[], int key_length);

template <typename T>
struct GenTraits;

template <size_t VecLen, VRandGenQueryMode QryMode>
struct GenTraits<VMT19937<VecLen, QryMode>>
{
    static const char* name() { return "VMT19937"; }
};

template <size_t VecLen, VRandGenQueryMode QryMode>
struct GenTraits<VSFMT19937<VecLen, QryMode>>
{
    static const char* name() { return "VSFMT19937"; }
};

std::vector<uint32_t> benchmark(nRandomTest + 10000);

void printSome(const std::vector<uint32_t>& v)
{
    std::cout << "\n";
    for (size_t i = 0; i < 16; ++i)
        std::cout << std::setw(10) << v[i] << ((i + 1) % 8 == 0 ? "\n" : " ");
    std::cout << "...\n";
    for (size_t i = 240; i < 240 + 16; ++i)
        std::cout << std::setw(10) << v[i] << ((i + 1) % 8 == 0 ? "\n" : " ");
    std::cout << "...\n";
    for (size_t i = 624 - 16; i < 624; ++i)
        std::cout << std::setw(10) << v[i] << ((i + 1) % 8 == 0 ? "\n" : " ");
    std::cout << "\n";
}

enum EncodeMode {Base64, Hex};

template <size_t nRows, size_t nCols>
void testEncoder(const BinaryMatrix<nRows, nCols>& m, EncodeMode enc)
{
    const char* modename = enc == Base64 ? "base64" : "hex";
    
    BinaryMatrix<nRows, nCols> m2;

    std::cout << "saving matrix to " << modename << " stream\n";
    std::ostringstream os;
    if (enc == Base64)
        m.toBase64(os);
    else
        m.toHex(os);

    std::cout << "first 32 characters of the stream\n";
    std::string s = os.str();
    for (size_t i = 0; i < 32; ++i)
        std::cout << s[i];
    std::cout << "\n";

    std::cout << "reading back the matrix from " << modename << " stream\n";
    std::istringstream is(os.str());
    if (enc == Base64)
        m2.fromBase64(is);
    else
        m2.fromHex(is);

    std::cout << "compare with original matrix\n";
    MYASSERT((m == m2), "error in roundtrip");

    std::cout << "completed\n";
}

template <size_t NBITS>
void testSquare(const BinarySquareMatrix<NBITS>& m)
{
    BinarySquareMatrix<NBITS> m2, m3;

    // slow bit by bit multiplication
    //std::cout << "compute matrix multiplication the classical way\n";
    for (size_t r = 0; r < NBITS; ++r) {
        //std::cout << r << "\n";
        for (size_t c = 0; c < NBITS; ++c) {
            size_t s = 0;
            for (size_t k = 0; k < NBITS; ++k) {
                s ^= m.getBit(r, k) && m.getBit(k, c);
            }
            if (s)
                m2.setBit(r, c);
        }
    }

    const size_t nThreads = 4;
    //std::cout << "compute matrix multiplication vectorially\n";
    std::vector<typename BinarySquareMatrix<NBITS>::buffer_t> buffers(nThreads);
    m3.square(m, buffers);

    MYASSERT((m2 == m3), "error in square");

    //std::cout << "SUCCESS\n";
}

template <size_t nRows, size_t nCols>
void encodingTests()
{
    BinaryMatrix<nRows, nCols> m;
    m.initRand();
    std::cout << "\ngenerated random matrix with size (" << m.s_nBitRows << "x" << m.s_nBitCols << ") with " << m.nnz() << " non zero elements\n";
    m.printBits(0, 0, 10, 32);

    testEncoder(m, Base64);
    testEncoder(m, Hex);
}

template <size_t NBits>
void squareTest()
{
    std::cout << "testing multiplication with matrices of size: " << NBits << "\n";
    BinarySquareMatrix<NBits> m;
    for (size_t i = 0; i < 10; ++i) {
        m.resetZero();
        m.initRand();
        testSquare(m);
    }
}

template <size_t...NBits>
void squareTests(std::index_sequence<NBits...>&&)
{
    (squareTest<NBits>(), ...);
}

template <typename Gen>
void testEquivalence(size_t nCommonJumpRepeat, const typename Gen::matrix_t* commonJump, const typename Gen::matrix_t* seqJump, size_t commonJumpSize, size_t sequenceJumpSize)
{
    MYASSERT((commonJumpSize == 0 && nCommonJumpRepeat == 0) || (commonJumpSize > 0 && nCommonJumpRepeat > 0), "(nCommonJumpRepeat>0) <=> commonJumpSize>0 ");

    const size_t VecLen = Gen::s_regLenBits;
    const VRandGenQueryMode QryMode = Gen::s_queryMode;
    const size_t BlkSize = QryMode == QM_Scalar ? 1 : QryMode == QM_Block16 ? 16 : Gen::s_n32InFullState;
    const size_t s_nStates = Gen::s_nStates;
    const size_t s_n32InOneWord = Gen::s_n32InOneWord;

    std::cout << GenTraits<Gen>::name() << ": Testing equivalence of generators with SIMD length " << VecLen
        << ", common jump ahead of " << commonJumpSize << " repeated " << nCommonJumpRepeat << " times, sequence jump size of " << sequenceJumpSize
        << ", block size " << BlkSize << " ... ";

    std::vector<uint32_t> aligneddst(nRandomTest);

    Gen mt(seedinit, seedlength, nCommonJumpRepeat, commonJump, seqJump);
    for (size_t i = 0; i < nRandomTest / BlkSize; ++i)
        if constexpr (QryMode == QM_Scalar)
            aligneddst[i] = mt.genrand_uint32();
        else if constexpr (QryMode == QM_Block16)
            mt.genrand_uint32_blk16(aligneddst.data() + i * BlkSize);
        else if constexpr (QryMode == QM_StateSize)
            mt.genrand_uint32_stateBlk(aligneddst.data() + i * BlkSize);
        else
            NOT_IMPLEMENTED;

    for (size_t i = 0; i < nRandomTest; ++i) {
        uint32_t r2 = aligneddst[i];
        size_t genIndex = (i % (s_n32InOneWord * s_nStates)) / s_n32InOneWord;
        size_t seqIndex = (i % s_n32InOneWord) + (i / (s_n32InOneWord * s_nStates)) * s_n32InOneWord;
        size_t benchmarkindex = seqIndex + commonJumpSize * nCommonJumpRepeat + sequenceJumpSize * genIndex;
        MYASSERT(benchmark[benchmarkindex] == r2, "FAILED!\n"
                << "Difference found: out[" << i << "] = " << r2
                << ", benchmark[" << benchmarkindex  << "] = " << benchmark[benchmarkindex]);
    }

    std::cout << "SUCCESS!\n";
}

void generateBenchmark_MT19937()
{
    unsigned long init[seedlength];
    for (size_t i = 0; i < seedlength; ++i)
        init[i] = seedinit[i];

    std::cout << "Generate MT19937 random numbers with the original C source code ... ";
    init_by_array(init, seedlength);
    for (size_t i = 0, n  = benchmark.size(); i < n; ++i)
        benchmark[i] = (uint32_t)genrand_int32();
    std::cout << "done!\n";
    printSome(benchmark);
}

void generateBenchmark_SFMT19937()
{
    std::cout << "Generate SFMT19937 random numbers with the original C source code ... ";
    sfmt_t sfmtgen;
    sfmt_init_by_array(&sfmtgen, const_cast<uint32_t *>(seedinit), seedlength);
    for (size_t i = 0, n = benchmark.size(); i < n; ++i)
        benchmark[i] = sfmt_genrand_uint32(&sfmtgen);
    std::cout << "done!\n";
    printSome(benchmark);
}

void startTest(const char* name)
{
    std::cout << "\n"
              << std::setw(40) << std::setfill('*') << "" << "\n" 
              << "Test " << name << "\n"
              << std::setw(40) << std::setfill('*') << "" << "\n\n"
              << std::setfill(' ');
}

void testEncoding()
{
    startTest("encoding");
    encodingTests<19937, 19937>();
    encodingTests<19937, 1007>();
    encodingTests<1007, 19937>();
    encodingTests<1007, 1007>();
}

void testSquareMatrix()
{
    startTest("square matrix calculation");
    squareTests(std::index_sequence<1, 5, 8, 13, 16, 20, 28, 32, 36, 60, 64, 68, 85, 126, 128, 150>{});
}

void test_VMT19937()
{
    startTest("VMT19937");

    generateBenchmark_MT19937();

    typedef MT19937Matrix matrix_t;
    typedef std::unique_ptr<MT19937Matrix> pmatrix_t;

    pmatrix_t jumpMatrix1(new matrix_t);                                         // jump ahead 1 element
    pmatrix_t jumpMatrix512(new matrix_t(std::string("./dat/F00009.bits")));     // jump ahead 2^9 (512) elements
    pmatrix_t jumpMatrix1024(new matrix_t(std::string("./dat/F00010.bits")));    // jump ahead 2^10 (1024) elements
    pmatrix_t jumpMatrixPeriod(new matrix_t(std::string("./dat/F19937.bits")));  // jump ahead 2^19937 elements

    testEquivalence<VMT19937<32, QM_Scalar>>(0, nullptr, nullptr, 0, 0);
    testEquivalence<VMT19937<32, QM_Scalar>>(1, &*jumpMatrix1024, nullptr, 1024, 0);
    // two jumps of 512 are equivalent to one jump of 1024
    testEquivalence<VMT19937<32, QM_Scalar>>(2, &*jumpMatrix512, nullptr, 512, 0);
    // since the period is 2^19937-1, after applying a jump matrix of 2^19937, we restart from the sequence from step 1
    testEquivalence<VMT19937<32, QM_Scalar>>(1, &*jumpMatrixPeriod, nullptr, 1, 0);

    testEquivalence<VMT19937<64, QM_Scalar>>(0, nullptr, nullptr, 0, 0);

    testEquivalence<VMT19937<128, QM_Scalar>>(0, nullptr, nullptr, 0, 0);
    testEquivalence<VMT19937<128, QM_Scalar>>(1, &*jumpMatrix1, nullptr, 1, 0);
    testEquivalence<VMT19937<128, QM_Scalar>>(2, &*jumpMatrix1, nullptr, 1, 0);
    testEquivalence<VMT19937<128, QM_Scalar>>(0, nullptr, &*jumpMatrix1, 0, 1);
    testEquivalence<VMT19937<128, QM_Scalar>>(0, nullptr, &*jumpMatrix1024, 0, 1024);
    testEquivalence<VMT19937<128, QM_Scalar>>(1, &*jumpMatrix1, &*jumpMatrix1, 1, 1);
    testEquivalence<VMT19937<128, QM_Scalar>>(2, &*jumpMatrix1, &*jumpMatrix1024, 1, 1024);

    testEquivalence<VMT19937<128, QM_Block16>>(0, nullptr, nullptr, 0, 0);
    testEquivalence<VMT19937<128, QM_Block16>>(0, nullptr, &*jumpMatrix1, 0, 1);
    testEquivalence<VMT19937<128, QM_Block16>>(0, nullptr, &*jumpMatrix1024, 0, 1024);

    testEquivalence<VMT19937<128, QM_StateSize>>(0, nullptr, nullptr, 0, 0);
    testEquivalence<VMT19937<128, QM_StateSize>>(0, nullptr, &*jumpMatrix1, 0, 1);
    testEquivalence<VMT19937<128, QM_StateSize>>(0, nullptr, &*jumpMatrix1024, 0, 1024);

    testEquivalence<VMT19937<256, QM_Scalar>>(0, nullptr, nullptr, 0, 0);
    testEquivalence<VMT19937<256, QM_Scalar>>(1, &*jumpMatrix1, nullptr, 1, 0);
    testEquivalence<VMT19937<256, QM_Scalar>>(2, &*jumpMatrix1, nullptr, 1, 0);
    testEquivalence<VMT19937<256, QM_Scalar>>(0, nullptr, &*jumpMatrix1, 0, 1);
    testEquivalence<VMT19937<256, QM_Scalar>>(0, nullptr, &*jumpMatrix1024, 0, 1024);
    testEquivalence<VMT19937<256, QM_Scalar>>(1, &*jumpMatrix1, &*jumpMatrix1, 1, 1);
    testEquivalence<VMT19937<256, QM_Scalar>>(2, &*jumpMatrix1, &*jumpMatrix1024, 1, 1024);

    testEquivalence<VMT19937<256, QM_Block16>>(0, nullptr, nullptr, 0, 0);
    testEquivalence<VMT19937<256, QM_Block16>>(0, nullptr, &*jumpMatrix1, 0, 1);
    testEquivalence<VMT19937<256, QM_Block16>>(0, nullptr, &*jumpMatrix1024, 0, 1024);
    
    testEquivalence<VMT19937<256, QM_StateSize>>(0, nullptr, nullptr, 0, 0);
    testEquivalence<VMT19937<256, QM_StateSize>>(0, nullptr, &*jumpMatrix1, 0, 1);
    testEquivalence<VMT19937<256, QM_StateSize>>(0, nullptr, &*jumpMatrix1024, 0, 1024);

    testEquivalence<VMT19937<512, QM_Scalar>>(0, nullptr, nullptr, 0, 0);
    testEquivalence<VMT19937<512, QM_Scalar>>(0, nullptr, nullptr, 0, 0);
    testEquivalence<VMT19937<512, QM_Scalar>>(1, &*jumpMatrix1, nullptr, 1, 0);
    testEquivalence<VMT19937<512, QM_Scalar>>(0, nullptr, &*jumpMatrix1, 0, 1);
    testEquivalence<VMT19937<512, QM_Scalar>>(0, nullptr, &*jumpMatrix1024, 0, 1024);
    testEquivalence<VMT19937<512, QM_Scalar>>(1, &*jumpMatrix1, &*jumpMatrix1, 1, 1);
    testEquivalence<VMT19937<512, QM_Scalar>>(2, &*jumpMatrix1, &*jumpMatrix1024, 1, 1024);

    testEquivalence<VMT19937<512, QM_Block16>>(0, nullptr, nullptr, 0, 0);
    testEquivalence<VMT19937<512, QM_Block16>>(0, nullptr, &*jumpMatrix1, 0, 1);
    testEquivalence<VMT19937<512, QM_Block16>>(0, nullptr, &*jumpMatrix1024, 0, 1024);
    
    testEquivalence<VMT19937<512, QM_StateSize>>(0, nullptr, nullptr, 0, 0);
    testEquivalence<VMT19937<512, QM_StateSize>>(0, nullptr, &*jumpMatrix1, 0, 1);
    testEquivalence<VMT19937<512, QM_StateSize>>(0, nullptr, &*jumpMatrix1024, 0, 1024);
}

void test_VSFMT19937()
{
    std::cout << "\nTest VSFMT19937\n";

    generateBenchmark_SFMT19937();

    SFMT19937Matrix jumpMatrix4;                                          // jump ahead 4 element
    //MT19937Matrix jumpMatrix512(std::string("./dat/F00009.bits"));      // jump ahead 2^9 (512) elements
    //MT19937Matrix jumpMatrix1024(std::string("./dat/F00010.bits"));     // jump ahead 2^10 (1024) elements
    //MT19937Matrix jumpMatrixPeriod(std::string("./dat/F19937.bits"));   // jump ahead 2^19937 elements

    testEquivalence<VSFMT19937<128, QM_Scalar>>(0, nullptr, nullptr, 0, 0);
    testEquivalence<VSFMT19937<128, QM_Scalar>>(1, &jumpMatrix4, nullptr, 4, 0);
    testEquivalence<VSFMT19937<128, QM_Scalar>>(2, &jumpMatrix4, nullptr, 4, 0);
    //testEquivalence<VSFMT19937<128, QM_Scalar>>(0, nullptr, &jumpMatrix4, 0, 1);
    //testEquivalence<VSFMT19937<128, QM_Scalar>>(0, nullptr, &jumpMatrix1024, 0, 1024);
    testEquivalence<VSFMT19937<128, QM_Block16>>(0, nullptr, nullptr, 0, 0);
    //testEquivalence<VSFMT19937<128, QM_Block16>>(0, nullptr, &jumpMatrix4, 0, 1);
    //testEquivalence<VSFMT19937<128, QM_Block16>>(0, nullptr, &jumpMatrix1024, 0, 1024);
    testEquivalence<VSFMT19937<128, QM_StateSize>>(0, nullptr, nullptr, 0, 0);
    //testEquivalence<VSFMT19937<128, QM_StateSize>>(0, nullptr, &jumpMatrix4, 0, 1);
    //testEquivalence<VSFMT19937<128, QM_StateSize>>(0, nullptr, &jumpMatrix1024, 0, 1024);

    testEquivalence<VSFMT19937<256, QM_Scalar>>(0, nullptr, nullptr, 0, 0);
    //testEquivalence<VSFMT19937<256, QM_Scalar>>(0, nullptr, &jumpMatrix4, 0, 1);
    //testEquivalence<VSFMT19937<256, QM_Scalar>>(0, nullptr, &jumpMatrix1024, 0, 1024);
    testEquivalence<VSFMT19937<256, QM_Block16>>(0, nullptr, nullptr, 0, 0);
    //testEquivalence<VSFMT19937<256, QM_Block16>>(0, nullptr, &jumpMatrix4, 0, 1);
    //testEquivalence<VSFMT19937<256, QM_Block16>>(0, nullptr, &jumpMatrix1024, 0, 1024);
    testEquivalence<VSFMT19937<256, QM_StateSize>>(0, nullptr, nullptr, 0, 0);
    //testEquivalence<VSFMT19937<256, QM_StateSize>>(0, nullptr, &jumpMatrix4, 0, 1);
    //testEquivalence<VSFMT19937<256, QM_StateSize>>(0, nullptr, &jumpMatrix1024, 0, 1024);

    testEquivalence<VSFMT19937<512, QM_Scalar>>(0, nullptr, nullptr, 0, 0);
    //testEquivalence<VSFMT19937<512, QM_Scalar>>(0, nullptr, &jumpMatrix4, 0, 1);
    //testEquivalence<VSFMT19937<512, QM_Scalar>>(0, nullptr, &jumpMatrix1024, 0, 1024);
    testEquivalence<VSFMT19937<512, QM_Block16>>(0, nullptr, nullptr, 0, 0);
    //testEquivalence<VSFMT19937<512, QM_Block16>>(0, nullptr, &jumpMatrix4, 0, 1);
    //testEquivalence<VSFMT19937<512, QM_Block16>>(0, nullptr, &jumpMatrix1024, 0, 1024);
    testEquivalence<VSFMT19937<512, QM_StateSize>>(0, nullptr, nullptr, 0, 0);
    //testEquivalence<VSFMT19937<512, QM_StateSize>>(0, nullptr, &jumpMatrix4, 0, 1);
    //testEquivalence<VSFMT19937<512, QM_StateSize>>(0, nullptr, &jumpMatrix1024, 0, 1024);
}

int main()
{
    try {
        test_VSFMT19937();
        testEncoding();
        testSquareMatrix();
        test_VMT19937();
    }
    catch (const std::exception& e) {
        std::cout << e.what() << "\n";
        return -1;
    }

    return 0;
}
