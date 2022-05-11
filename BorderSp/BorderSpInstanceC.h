#pragma once
#include <windows.h>
#include <string>
#include <iostream>
#include <functional>

inline std::string debugString()
{
#ifdef _DEBUG
    return "debug";
#else
    return "release";
#endif
}

inline std::string compiller()
{
    switch (_MSC_VER)
    {
    case 1930:
        return "2022";
    case 1600:
        return "2010";
    case 1800:
        return "2013";
    default:
        return std::to_string((long long)_MSC_VER);
    }
}

inline void PrintLine(const std::string& message, void* ptr)
{
    std::cout << "(" << compiller() << ") (" << ptr << ") (" << debugString() << ") " << message << std::endl;
}

struct AllocatorC
{
    void* instance;

    void* (__stdcall* CbAlocate)(void* allocator, unsigned int size);
    void(__stdcall* CbFree)(void* allocator, void* instance);

    void* Allocate(unsigned int size)
    {
        return CbAlocate(instance, size);
    }

    void Free(void* ptr)
    {
        CbFree(instance, ptr);
    }

    static void __stdcall NewAllocator(AllocatorC* allocator)
    {
        allocator->instance = allocator;
        allocator->CbAlocate = DoAlocateObject;
        allocator->CbFree = DoFreeObject;
    }

    static void __stdcall NewDeleter(AllocatorC* allocator, void* instance, void(__stdcall* freeObject)(void* allocator, void* instance))
    {
        allocator->instance = instance;
        allocator->CbAlocate = nullptr;
        allocator->CbFree = freeObject;
    }

    static void* __stdcall DoAlocateObject(void* allocator, unsigned int size)
    {
        auto str = new char[size];
        PrintLine(std::string("DoAlocateObject") + std::to_string((long long)allocator), str);
        return str;
    }

    static void __stdcall DoFreeObject(void* allocator, void* instance)
    {
        PrintLine(std::string("DoFreeObject") + std::to_string((long long)allocator), instance);
        delete[] (char*) instance;
    }

};

struct AtomicCounterC
{
    void* counterInstance;
    unsigned long counter;
    unsigned long(__stdcall* CbIncrement)(void* counterInstance, unsigned long* counter);
    unsigned long(__stdcall* CbDecrement)(void* counterInstance, unsigned long* counter);
    unsigned long(__stdcall* CbChangeIf)(void* counterInstance, unsigned long* counter, unsigned long prevValue, unsigned long value);
    unsigned long(__stdcall* CbLoadValue)(void* counterInstance, unsigned long* counter);

    void AddRef()
    {
        CbIncrement(counterInstance, &counter);
    }

    bool DeleteRef()
    {
        return CbDecrement(counterInstance, &counter) > 0;
    }

    bool TryAddRef()
    {
        auto count = CbLoadValue(counterInstance, &counter);
        while (count != 0) {
            const long oldValue = CbChangeIf(counterInstance, &counter, count, count + 1);
            if (oldValue == count) {
                return true;
            }
            count = oldValue;
        }

        return false;
    }

    bool IsZero()
    {
        return CbLoadValue(counterInstance, &counter) == 0;
    }

    static void __stdcall NewAtomicCounter(AtomicCounterC* counter, unsigned long startValue)
    {
        counter->counter = startValue;
        counter->counterInstance = counter;
        counter->CbIncrement = DoIncrement;
        counter->CbDecrement = DoDecrement;
        counter->CbChangeIf = DoChangeIf;
        counter->CbLoadValue = DoLoadValue;
    }

    static unsigned long __stdcall DoIncrement(void* countRefsInstance, unsigned long* counter)
    {
        return InterlockedIncrement(counter);
    }

    static unsigned long __stdcall DoDecrement(void* countRefsInstance, unsigned long* counter)
    {
        return InterlockedDecrement(counter);
    }

    static unsigned long __stdcall DoChangeIf(void* counterInstance, unsigned long* counter, unsigned long prevValue, unsigned long value)
    {
        return InterlockedCompareExchange(counter, value, prevValue);
    }

    static unsigned long __stdcall DoLoadValue(void* counterInstance, unsigned long* counter)
    {
        return static_cast<unsigned long>(__iso_volatile_load32(reinterpret_cast<__int32*>(counter)));
    }

private:
    AtomicCounterC() :
        counter(),
        counterInstance(),
        CbIncrement(),
        CbDecrement(),
        CbChangeIf(),
        CbLoadValue() {}
    AtomicCounterC(const AtomicCounterC&):
        counter(),
        counterInstance(),
        CbIncrement(),
        CbDecrement(),
        CbChangeIf(),
        CbLoadValue()
    {}
    AtomicCounterC& operator=(const AtomicCounterC&) { return *this; }
    AtomicCounterC& operator=(AtomicCounterC&&) { return *this; }

};

struct RefCounterC
{
    void* instance;
    AllocatorC instanceDeleter; 
    AtomicCounterC refsCounter;
    AtomicCounterC weakPtrsCounter;
    AllocatorC allocator;

    void AddRef()
    {
        refsCounter.AddRef();
    }

    bool DeleteRef()
    {
        if (!refsCounter.DeleteRef())
        {
            instanceDeleter.Free(instance);
            instance = nullptr;
            if (!weakPtrsCounter.DeleteRef())
            {
                Release();
                return false;
            }
        }
        return true;
    }

    void AddWeakRef()
    {
        weakPtrsCounter.AddRef();
    }

    bool DeleteWeakRef()
    {
        if (!weakPtrsCounter.DeleteRef())
        {
            Release();
            return false;
        }
        return true;
    }

    bool TryAddRef()
    {
        return refsCounter.TryAddRef();
    }

private:
    void Release()
    {
        allocator.Free(this);
    }

public:
    static void __stdcall NewRefCounter(RefCounterC* counter, void* instance, AllocatorC* instanceDeleter, AllocatorC* allocator)
    {
        counter->instance = instance;
        counter->instanceDeleter = *instanceDeleter;
        AtomicCounterC::NewAtomicCounter(&counter->refsCounter, 1);
        AtomicCounterC::NewAtomicCounter(&counter->weakPtrsCounter, 1);
        counter->allocator = *allocator;
    }

};

struct SharedPointerInstanceC
{
    AllocatorC allocator;
    RefCounterC* refCounter;
    void* instance;

    void(__stdcall* CbAddWeakRef)(SharedPointerInstanceC* weakPtr);
    bool(__stdcall* CbDeleteWeakRef)(SharedPointerInstanceC* weakPtr);
    void(__stdcall* CbAddRef)(SharedPointerInstanceC* weakPtr);
    bool(__stdcall* CbDeleteRef)(SharedPointerInstanceC* weakPtr);
    bool(__stdcall* CbTryLock)(SharedPointerInstanceC* weakPtr);

    void OnAddWeakRef()
    {
        CbAddWeakRef(this);
    }

    bool OnDeleteWeakRef()
    {
        return CbDeleteWeakRef(this);
    }

    void AddRef()
    {
        CbAddRef(this);
    }

    bool DeleteRef()
    {
        return CbDeleteRef(this);
    }

    void Release()
    {
        allocator.Free(this);
    }

    bool Lock()
    {
        return CbTryLock(this);
    }

    SharedPointerInstanceC* CopyConstructFor(void* instance)
    {
        SharedPointerInstanceC* newSharedPointerInstance = (SharedPointerInstanceC*)allocator.Allocate(sizeof(SharedPointerInstanceC));
        newSharedPointerInstance->allocator = allocator;
        newSharedPointerInstance->refCounter = refCounter;
        newSharedPointerInstance->instance = instance;
        newSharedPointerInstance->CbAddWeakRef = CbAddWeakRef;
        newSharedPointerInstance->CbDeleteWeakRef = CbDeleteWeakRef;
        newSharedPointerInstance->CbAddRef = CbAddRef;
        newSharedPointerInstance->CbDeleteRef = CbDeleteRef;
        newSharedPointerInstance->CbTryLock = CbTryLock;
        return newSharedPointerInstance;
    }

    void Swap(SharedPointerInstanceC& right)
    {
        std::swap(right.allocator, allocator);
        std::swap(right.refCounter, refCounter);
        std::swap(right.instance, instance);
        std::swap(right.CbAddWeakRef, CbAddWeakRef);
        std::swap(right.CbDeleteWeakRef, CbDeleteWeakRef);
        std::swap(right.CbAddRef, CbAddRef);
        std::swap(right.CbDeleteRef, CbDeleteRef);
        std::swap(right.CbTryLock, CbTryLock);
    }

    static void __stdcall NewSharedPointerInstance(SharedPointerInstanceC* weakPtr, void* instance, AllocatorC* instanceDeleter, AllocatorC* allocator)
    {
        weakPtr->allocator = *allocator;
        weakPtr->refCounter =(RefCounterC*) weakPtr->allocator.Allocate(sizeof(RefCounterC));
        RefCounterC::NewRefCounter(weakPtr->refCounter, instance, instanceDeleter, &weakPtr->allocator);
        weakPtr->instance = instance;
        weakPtr->CbAddWeakRef = DoAddWeakRef;
        weakPtr->CbDeleteWeakRef = DoDeleteWeakRef;
        weakPtr->CbAddRef = DoAddRef;
        weakPtr->CbDeleteRef = DoDeleteRef;
        weakPtr->CbTryLock = DoTryLock;
    }

    static void __stdcall DoAddWeakRef(SharedPointerInstanceC* weakPtr)
    {
        weakPtr->refCounter->AddWeakRef();
    }

    static bool __stdcall DoDeleteWeakRef(SharedPointerInstanceC* weakPtr)
    {
        if (!weakPtr->refCounter->DeleteWeakRef())
        {
            weakPtr->refCounter = nullptr;
            weakPtr->Release();
            return false;
        }
        return true;
    }

    static bool __stdcall DoDeleteRef(SharedPointerInstanceC* weakPtr)
    {
        if (!weakPtr->refCounter->DeleteRef())
        {
            weakPtr->refCounter = nullptr;
            weakPtr->Release();
            return false;
        }
        return true;
    }

    static void __stdcall DoAddRef(SharedPointerInstanceC* weakPtr)
    {
        weakPtr->refCounter->AddRef();
    }

    static bool __stdcall DoTryLock(SharedPointerInstanceC* weakPtr)
    {
        return weakPtr->refCounter->TryAddRef();
    }
};

struct SharedPointerC;

struct WeakPointerC
{
    SharedPointerInstanceC* instance;

    SharedPointerC Lock();

    bool Validate()
    {
        return instance != nullptr;
    }

    void AddRef()
    {
        instance->OnAddWeakRef();
    }

    void DeleteRef()
    {
        if (!instance->OnDeleteWeakRef())
        {
            instance = nullptr;
        }
    }
};

struct SharedPointerC
{
    SharedPointerInstanceC* instance;

    void* GetRef()
    {
        if (Validate())
        {
            return instance->instance;
        }
        return nullptr;
    }

    void AddRef()
    {
        if (Validate())
        {
            instance->AddRef();
        }
    }

    void DeleteRef()
    {
        if (Validate())
        {
            if (!instance->DeleteRef())
            {
                instance = nullptr;
            }
        }
    }

    bool Validate()
    {
        return instance != nullptr;
    }

    WeakPointerC GetWeakPtr()
    {
        WeakPointerC c;
        c.instance = instance;
        c.AddRef();
        return c;
    }

    static void __stdcall NewSharedPointerPtr(SharedPointerC* sharedPtr, void* instance, AllocatorC* instanceDeleter)
    {
        AllocatorC allocator;
        AllocatorC::NewAllocator(&allocator);
        sharedPtr->instance = (SharedPointerInstanceC*)allocator.Allocate(sizeof(SharedPointerInstanceC));
        SharedPointerInstanceC::NewSharedPointerInstance(sharedPtr->instance, instance, instanceDeleter, &allocator);
    }

};

inline SharedPointerC WeakPointerC::Lock()
{
    SharedPointerC res;
    if (Validate())
    {
        if (instance->Lock())
        {
            res.instance = instance;
            return res;
        }
    }
    res.instance = nullptr;
    return res;
}

struct SomeCallBack
{
    void* instance;
    void(__stdcall* CbProc)(void* instance);
    void(__stdcall* CbDeleteProc)(void* instance);

    void Call()
    {
        CbProc(instance);
    }
    void CallBeforeDelete()
    {
        CbDeleteProc(instance);
    }

    static void __stdcall NewSomeCallBack(SomeCallBack* cb, void* instance, void(__stdcall* proc)(void* instance), void(__stdcall* del)(void* instance))
    {
        cb->instance = instance;
        cb->CbProc = proc;
        cb->CbDeleteProc = del;
    }

};

class SharedPtr
{
public:
    typedef SomeCallBack T;
    typedef T Type;
    typedef T _Ty;

private:
    template<typename _Dt>
    class Deleter
    {
    public:
        Deleter(_Dt&& free) :
            m_free(free)
        {}

    public:
        static void _stdcall DefaultFreeVoid(void* instance, void* ptr)
        {
            Deleter* self = ((Deleter*)instance);
            self->DefaultFree((Type*)ptr);
            delete self;
        }

        void DefaultFree(Type* ptr)
        {
            ptr->CallBeforeDelete();
            PrintLine("do delete with deleter", ptr);
            m_free(ptr);
        }

    private:
        _Dt m_free;
    };

public:
    SharedPtr() :
        m_releaseC()
    {
        AllocatorC deleter;
        AllocatorC::NewDeleter(&deleter, this, DefaultNoFree);
        SharedPointerC::NewSharedPointerPtr(&m_releaseC, nullptr, &deleter);
    }

    SharedPtr(nullptr_t) :
        m_releaseC()
    {
        AllocatorC deleter;
        AllocatorC::NewDeleter(&deleter, this, DefaultNoFree);
        SharedPointerC::NewSharedPointerPtr(&m_releaseC, nullptr, &deleter);
    }

    SharedPtr(Type* ptr):
        m_releaseC()
    {
        AllocatorC deleter;
        AllocatorC::NewDeleter(&deleter, this, DefaultFreeVoid);
        SharedPointerC::NewSharedPointerPtr(&m_releaseC, ptr, &deleter);
    }

    template <class _Ux, class _Dx,
        std::enable_if_t<std::conjunction_v<std::is_move_constructible<_Dx>, std::_Can_call_function_object<_Dx&, _Ux*&>,
        std::_SP_convertible<_Ux, _Ty>>,
        int> = 0>
    SharedPtr(Type* ptr, _Dx free) :
        m_releaseC()
    {
        Setpd(ptr, std::move(free));
    }

    template <class _Ux, class _Dx, class _Alloc,
        std::enable_if_t<std::conjunction_v<std::is_move_constructible<_Dx>, std::_Can_call_function_object<_Dx&, _Ux*&>,
        std::_SP_convertible<_Ux, _Ty>>,
        int> = 0>
        SharedPtr(_Ux* _Px, _Dx _Dt, _Alloc _Ax) { // construct with _Px, deleter, allocator
        _Setpda(_Px, _STD move(_Dt), _Ax);
    }

    SharedPtr(const SharedPtr& cb) :
        m_releaseC()
    {
        Set(cb);
    }

    ~SharedPtr()
    {
        m_releaseC.DeleteRef();
    }

public:
    static void _stdcall DefaultFreeVoid(void* self, void* ptr)
    {
        ((SharedPtr*)self)->DefaultFree((Type*)ptr);
    }

    static void _stdcall DefaultNoFree(void* self, void* ptr)
    {
    }

    void DefaultFree(Type* ptr)
    {
        ptr->CallBeforeDelete();
        PrintLine("do delete", ptr);
        delete ptr;
    }

public:
    SharedPtr& operator=(const SharedPtr& cb)
    {
        Reset(cb);
        return *this;
    }
    SharedPtr& operator=(Type* t)
    {
        Reset(t);
        return *this;
    }

private:
    void Release()
    {
        m_releaseC.DeleteRef();
    }

    void Reset(const SharedPtr& cb)
    {
        Release();
        Set(cb);
    }

    void Set(const SharedPtr& cb)
    {
        m_releaseC = cb.m_releaseC;
        m_releaseC.AddRef();
    }

public:
    Type* operator->()
    {
        return (Type*)m_releaseC.GetRef();
    }

private:
    template <class _UxptrOrNullptr, class _Dx>
    void Setpd(const _UxptrOrNullptr ptr, _Dx _Dt) {
        typedef Deleter<_Dx> DelT;
        auto* deleterInstance = new DelT(std::move(_Dt));
        AllocatorC deleter;
        AllocatorC::NewDeleter(&deleter, deleterInstance, DelT::DefaultFreeVoid);
        SharedPointerC::NewSharedPointerPtr(&m_releaseC, ptr, &deleter);
    }


private:
    SharedPointerC m_releaseC;

};

class BorderPrintInfo
{
public:
    static void __stdcall Print(void* pi)
    {
        ((BorderPrintInfo*)pi)->Print();
    }
    static void __stdcall OnDelete(void* pi)
    {
        ((BorderPrintInfo*)pi)->OnDelete();
    }

public:
    void Print()
    {
        PrintLine("print", this);
    }
    void OnDelete()
    {
        PrintLine("before delete call", this);
    }

public:
    SomeCallBack* MakeCb()
    {
        auto cb = new SomeCallBack();

        SomeCallBack::NewSomeCallBack(cb, this, BorderPrintInfo::Print, BorderPrintInfo::OnDelete);
        return cb;
    }

    SharedPtr MakeCbPtr()
    {
        return MakeCb();
    }
};

