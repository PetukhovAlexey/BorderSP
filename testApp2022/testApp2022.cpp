// testApp2022.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <BorderSpInstanceC.h>
#include <vector>


class TestBorderSpExporter
{
public:
    TestBorderSpExporter() :
        m_module(nullptr),
        CbMakeObject(nullptr),
        m_name()
    {}
    TestBorderSpExporter(const std::string &name):
        m_module(nullptr),
        CbMakeObject(nullptr),
        m_name(name)
    {}

public:
    void Load()
    {
        m_module = LoadLibraryA(m_name.c_str());
        CbMakeObject = decltype(CbMakeObject)(GetProcAddress(m_module, "MakeBorderSp"));
    }

public:
    SharedPtr MakeSp()
    {
        return CbMakeObject();
    }

private:
    HMODULE m_module;
    SharedPtr(__stdcall *CbMakeObject)();

    std::string m_name;
};

struct TestBorderSpImporter
{
public:
    void Load(const std::string& name)
    {
        m_exporters.emplace_back(name);
        m_exporters.back().Load();
    }

    SharedPtr MakeSp(int i)
    {
        return m_exporters.at(i).MakeSp();
    }

private:
    std::vector<TestBorderSpExporter> m_exporters;
};


BorderPrintInfo bi;

void TestBorderSp()
{
    TestBorderSpImporter importer;
    importer.Load("testDll2010d");
    importer.Load("testDll2013d");
    importer.Load("testDll2010r");
    importer.Load("testDll2013r");
    SharedPtr ptr0 = bi.MakeCbPtr();
    ptr0->Call();
    SharedPtr ptr1 = importer.MakeSp(0);
    ptr1->Call();
    SharedPtr ptr2 = importer.MakeSp(1);
    ptr2->Call();
    SharedPtr ptr3 = importer.MakeSp(2);
    ptr3->Call();
    SharedPtr ptr4 = importer.MakeSp(3);
    ptr4->Call();
}

int main()
{
    //std::shared_ptr<SomeCallBack> stdspDefaultConstructor;
    //SharedPtr borderspDefaultConstructor;
    //std::shared_ptr<SomeCallBack> stdspNullptrConstructor(nullptr);
    //SharedPtr borderspNullptrConstructor(nullptr);
    ///*
    //std::shared_ptr<SomeCallBack> stdspArrayConstructor();
    //SharedPtr borderspArrayConstructor();
    //*/
    //std::shared_ptr<SomeCallBack> stdspDeleterConstructor(new SomeCallBack(), [](SomeCallBack* p) {delete p; });
    //SharedPtr borderspNullptrConstructor(new SomeCallBack(), [](SomeCallBack* p) {delete p; });

    TestBorderSp();
    std::cout << "Hello World!\n";
}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
