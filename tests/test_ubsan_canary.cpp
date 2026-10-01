// Built only with FT_ENABLE_SANITIZERS, and expected to die (ctest WILL_FAIL).
//
// It overflows a signed int on purpose. UBSan has to stop the process right
// there: by default it reports and carries on, the test exits 0, and ctest shows
// nothing of a passing test's output -- which is how the ASan+UBSan job used to
// stay green whatever UBSan found. Reaching the printf below means that is back.
#include <climits>
#include <cstdio>

int main()
{
    volatile int big = INT_MAX;
    // The overflow is the point of this test.
    // cppcheck-suppress integerOverflow
    const int wrapped = big + 1;
    std::printf("UBSan reported a signed overflow (%d) and let the test go on: "
                "build with -fno-sanitize-recover=undefined\n",
                wrapped);
    return 0;
}
