#include <windows.h>
#include <CppUnitTest.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

TEST_CLASS(BuildSmokeTests)
{
public:
    TEST_METHOD(TestsRunAsX86)
    {
        Assert::AreEqual(size_t{4}, sizeof(void*));
    }
};
