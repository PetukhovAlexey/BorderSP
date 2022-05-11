#include "stdafx.h"

BorderPrintInfo bi;

extern "C" SharedPtr  __declspec(dllexport) __stdcall MakeBorderSp()
{
    return bi.MakeCbPtr();
}